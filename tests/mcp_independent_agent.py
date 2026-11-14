"""Agent→independent MCP inference/tools; no AVPlayer gateway/media/Worker."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import grpc
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'services/agent'))
from avplayer_agent import agent_pb2 as pb,agent_pb2_grpc as rpc

def main():
    mcp=Path(os.environ.get('AVPLAYER_MCP_ROOT',ROOT/'thirdParty/MCP'))
    with tempfile.TemporaryDirectory(prefix='agent-independent-mcp-') as tmp:
        path=Path(tmp);folder=path/'storage'/'agent-mcp'/'speech';folder.mkdir(parents=True)
        os.link(ROOT/'resource/input.mp4',folder/'original.mp4')
        config=path/'mcp.json';config.write_text(json.dumps({'server':{'port':50881},'logging':{'log_file_path':str(path/'mcp.log'),'log_console_output':False},'job_agent':{'db_path':str(path/'jobs.db')}}))
        env=dict(os.environ,AVPLAYER_PROJECT_ROOT=str(ROOT),MCP_MEDIA_ROOT=str(mcp),AVPLAYER_STORAGE_ROOT=str(path/'storage'),AVPLAYER_MCP_URL='http://127.0.0.1:50881/jsonrpc',AVPLAYER_DATA_SERVER='127.0.0.1:1',AVPLAYER_WORKER_SERVERS='127.0.0.1:1',AVPLAYER_WORKER_PYTHON=str(ROOT/'.venv/bin/python'),PYTHONPATH=str(ROOT/'services/agent'),AVPLAYER_AGENT_PORT='50861',AVPLAYER_AI_PROVIDER='local')
        procs=[];logs=[]
        try:
            for name,cmd in [('mcp',[Path(os.environ.get('AVPLAYER_MCP_BIN', ROOT/'build/mcp/MCP_Server')),'--config',config,'--host','127.0.0.1','--port','50881']),('agent',[ROOT/'.venv/bin/python','-m','avplayer_agent'])]:
                log=open(path/(name+'-process.log'),'w');logs.append(log)
                procs.append(subprocess.Popen(list(map(str,cmd)),env=env,cwd=path,stdout=log,stderr=subprocess.STDOUT))
            time.sleep(2)
            channel=grpc.insecure_channel('127.0.0.1:50861');grpc.channel_ready_future(channel).result(timeout=10)
            client=rpc.AgentServiceStub(channel)
            assert client.RegisterMedia(pb.MediaRegistration(session_id='agent-mcp',media_id='speech'),timeout=5).success
            stop=threading.Event()
            def messages():
                yield pb.ClientMessage(type='task',session_id='agent-mcp',request_id='independent',payload=pb.Struct(json='{"task":"transcribe_subtitle"}'))
                stop.wait(120)
            stream=client.Converse(messages(),timeout=120)
            try:
                for message in stream:
                    value=json.loads(message.payload.json or '{}')
                    if message.type=='task_result':
                        assert value.get('success'),value
                        assert value.get('segments')
                        assert (folder/'transcript.srt').is_file()
                        print('PASS: Agent→independent MCP→native extraction→Whisper→MCP subtitle save',flush=True)
                        print('segments:',len(value['segments']),flush=True)
                        break
                else:raise AssertionError('no result')
            finally:stop.set();stream.cancel()
        except Exception:
            for log in path.rglob('*.log'):print(log.name,log.read_text(errors='replace')[-1800:],file=sys.stderr)
            raise
        finally:
            for p in procs:p.terminate()
            for p in procs:
                try:p.wait(timeout=8)
                except subprocess.TimeoutExpired:p.kill();p.wait()
            for log in logs:log.close()
if __name__=='__main__':main()
