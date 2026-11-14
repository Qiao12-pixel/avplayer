"""Real Redis/MySQL persistence + Agent reuse without inference.
Writes isolated test rows and temporary files; requires local DB credentials.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import uuid
import grpc
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'services/agent'))
from avplayer_agent import agent_pb2 as pb,agent_pb2_grpc as rpc


def main():
    processes=[];logs=[]
    with tempfile.TemporaryDirectory(prefix='avplayer-results-') as tmp:
        folder=Path(tmp);session='results-'+uuid.uuid4().hex[:12];media=uuid.uuid4().hex
        source=folder/'storage'/session/media;source.mkdir(parents=True);(source/'original.mp4').write_bytes(b'media existence check; no decode in cached tasks')
        env=dict(os.environ,PYTHONPATH=str(ROOT/'services/agent'),AVPLAYER_PROJECT_ROOT=str(ROOT),MCP_MEDIA_ROOT=str(ROOT/'thirdParty/MCP'),AVPLAYER_STORAGE_ROOT=str(folder/'storage'),
            AVPLAYER_WEB_GATEWAY_PORT='51165',AVPLAYER_DATA_SERVER='127.0.0.1:51166',AVPLAYER_DATA_SERVER_ADDRESS='127.0.0.1:51166',
            AVPLAYER_MEDIA_SERVER_ADDRESS='127.0.0.1:51162',AVPLAYER_AGENT_PORT='51161',AVPLAYER_AGENT_SERVER='127.0.0.1:51161',AVPLAYER_MCP_URL='http://127.0.0.1:51181/jsonrpc',
            AVPLAYER_REDIS_URL=os.environ.get('AVPLAYER_REDIS_URL','redis://127.0.0.1:6379/0'),
            AVPLAYER_MYSQL_DSN=os.environ.get('AVPLAYER_MYSQL_DSN','mysql://avplayer:avplayer@127.0.0.1:3306/avplayer'),AVPLAYER_ZK_HOSTS='',AVPLAYER_AI_PROVIDER='local',AVPLAYER_WORKER_PYTHON=str(ROOT/'.venv/bin/python'))
        config=folder/'mcp.json';config.write_text(json.dumps({'server':{'port':51181},'logging':{'log_file_path':str(folder/'mcp.log'),'log_console_output':False},'job_agent':{'db_path':str(folder/'jobs.db')}}))
        def launch(command,name):
            log=open(folder/(name+'.log'),'w');logs.append(log)
            process=subprocess.Popen(list(map(str,command)),env=env,cwd=folder,stdout=log,stderr=subprocess.STDOUT);processes.append(process);return process
        def connect():
            channel=grpc.insecure_channel('127.0.0.1:51166');grpc.channel_ready_future(channel).result(timeout=15);return rpc.StorageServiceStub(channel)
        def redis(command,*args):
            return subprocess.check_output(['redis-cli','--no-auth-warning','-u',env['AVPLAYER_REDIS_URL'],command,*args],text=True).strip()
        def execute(client,action,**kwargs):
            return json.loads(client.Execute(pb.Struct(json=json.dumps({'action':action,'session_id':session,'media_id':media,**kwargs})),timeout=5).json)
        def cache_key(task,variant=''):
            return f'avplayer:result:{session}:{media}:{task}:{variant}'
        try:
            server=launch([ROOT/'cmake-build-debug/services/server/avplayer_server'],'server')
            client=connect()
            segments=[{'start_ms':0,'end_ms':1000,'text':'持久化测试字幕'}]
            saved=execute(client,'result_put',task='transcribe_subtitle',payload={'success':True,'segments':segments,'result':'saved subtitles'})
            assert saved=={'mysql_saved':True,'redis_cached':True},saved
            transcript=execute(client,'result_get',task='transcribe_subtitle');assert transcript['segments']==segments
            assert execute(client,'load_transcript')==segments
            variant=hashlib.sha256(('local\n'+transcript['text']).encode()).hexdigest()
            saved=execute(client,'result_put',task='summarize_video',variant=variant,payload={'success':True,'result':'持久化摘要'})
            assert saved['mysql_saved'] and saved['redis_cached'],saved
            assert int(redis('TTL',cache_key('summarize_video',variant)))>0
            other=client.Execute(pb.Struct(json=json.dumps({'action':'result_get','session_id':'other-session','media_id':media,'task':'summarize_video','variant':variant})),timeout=5)
            assert json.loads(other.json) is None
            assert execute(client,'result_get',task='summarize_video',variant='changed-subtitles') is None
            print('PASS: complete subtitles + summary in real MySQL/Redis, TTL, session and version isolation',flush=True)
            for task,v in [('transcribe_subtitle',''),('summarize_video',variant)]:redis('DEL',cache_key(task,v))
            server.terminate();server.wait(timeout=5)
            server=launch([ROOT/'cmake-build-debug/services/server/avplayer_server'],'server-restarted');client=connect()
            assert execute(client,'result_get',task='transcribe_subtitle')['segments']==segments
            assert execute(client,'result_get',task='summarize_video',variant=variant)['result']=='持久化摘要'
            assert redis('EXISTS',cache_key('summarize_video',variant))=='1'
            print('PASS: backend restart + Redis eviction restores from MySQL and refills cache',flush=True)
            launch([ROOT/'build/mcp/MCP_Server','--config',config,'--host','127.0.0.1','--port','51181'],'mcp')
            launch([ROOT/'.venv/bin/python','-m','avplayer_agent'],'agent')
            channel=grpc.insecure_channel('127.0.0.1:51161');grpc.channel_ready_future(channel).result(timeout=15);agent=rpc.AgentServiceStub(channel)
            agent.RegisterMedia(pb.MediaRegistration(session_id=session,media_id=media),timeout=5)
            time.sleep(.5)
            for task in ['transcribe_subtitle','summarize_video']:
                stop=threading.Event();request_id=uuid.uuid4().hex
                def messages():
                    yield pb.ClientMessage(type='task',session_id=session,request_id=request_id,payload=pb.Struct(json=json.dumps({'task':task})))
                    stop.wait(30)
                stream=agent.Converse(messages(),timeout=20)
                try:
                    for message in stream:
                        if message.type=='task_result':
                            value=json.loads(message.payload.json);assert value.get('success') and value.get('cached'),value
                            if task=='transcribe_subtitle':assert value['segments']==segments
                            else:assert value['result']=='持久化摘要'
                            break
                    else:raise AssertionError('no task result')
                finally:stop.set();stream.cancel()
            assert not list(source.glob('jobs/*/request.json'))
            print('PASS: Agent reuses both stored results without model/Whisper jobs',flush=True)
            # Generate real new results, then verify the automatically persisted data.
            media=uuid.uuid4().hex
            source=folder/'storage'/session/media;source.mkdir(parents=True)
            os.link(ROOT/'resource/input.mp4',source/'original.mp4')
            agent.RegisterMedia(pb.MediaRegistration(session_id=session,media_id=media),timeout=5)
            generated=set()
            for task in ['transcribe_subtitle','summarize_video','transcribe_subtitle','summarize_video']:
                repeated = task in generated
                stop=threading.Event();request_id=uuid.uuid4().hex
                def messages():
                    yield pb.ClientMessage(type='task',session_id=session,request_id=request_id,payload=pb.Struct(json=json.dumps({'task':task})))
                    stop.wait(180)
                stream=agent.Converse(messages(),timeout=180)
                try:
                    for message in stream:
                        if message.type=='task_result':
                            value=json.loads(message.payload.json);assert value.get('success'),value
                            if repeated:assert value.get('cached'),value
                            else:
                                assert value.get('persistence')=={'mysql_saved':True,'redis_cached':True},value
                            break
                    else:raise AssertionError('no generated task result')
                finally:stop.set();stream.cancel()
                generated.add(task)
            stored=execute(client,'result_get',task='transcribe_subtitle')
            assert len(stored['segments'])>0
            variant=hashlib.sha256(('local\n'+stored['text']).encode()).hexdigest()
            assert execute(client,'result_get',task='summarize_video',variant=variant)['result']
            print('PASS: real Whisper/Qwen results automatically persisted; repeat requests reuse both',flush=True)

        finally:
            for process in reversed(processes):
                if process.poll() is None:process.terminate()
            for process in processes:
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait()
            for log in logs:log.close()
if __name__=='__main__':main()
