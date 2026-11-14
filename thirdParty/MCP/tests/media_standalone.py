"""Independent media tools: starts only MCP, no AVPlayer C++ service."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import urllib.request
import uuid
ROOT=Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix='mcp-media-') as tmp:
        directory=Path(tmp);store=directory/'storage'
        media=store/'standalone'/'video';media.mkdir(parents=True)
        subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','testsrc2=size=320x180:rate=30','-f','lavfi','-i','sine=frequency=1000','-t','6','-c:v','libx264','-c:a','aac',str(media/'original.mp4')],check=True)
        config=directory/'config.json';config.write_text(json.dumps({'server':{'port':50781},'logging':{'log_file_path':str(directory/'server.log'),'log_console_output':False},'job_agent':{'db_path':str(directory/'jobs.db')}}))
        env=dict(os.environ,AVPLAYER_STORAGE_ROOT=str(store),MCP_MEDIA_ROOT=str(ROOT),AVPLAYER_DATA_SERVER='127.0.0.1:1',AVPLAYER_WORKER_SERVERS='127.0.0.1:1')
        log=open(directory/'process.log','w')
        process=subprocess.Popen([str(Path(os.environ.get('AVPLAYER_MCP_BIN',ROOT.parents[1]/'build/mcp/MCP_Server'))),'--config',str(config),'--host','127.0.0.1','--port','50781'],env=env,cwd=directory,stdout=log,stderr=subprocess.STDOUT)
        def rpc(method,params):
            request=urllib.request.Request('http://127.0.0.1:50781/jsonrpc',data=json.dumps({'jsonrpc':'2.0','id':1,'method':method,'params':params}).encode(),headers={'Content-Type':'application/json'})
            with urllib.request.urlopen(request,timeout=10) as response:return json.load(response)['result']
        def call(name,args):
            result=rpc('tools/call',{'name':name,'arguments':args})
            return json.loads(result['content'][0]['text'])
        def tool(name,args=None,**extra):return call(name,dict(session_id='standalone',media_id='video',arguments=args or {},**extra))
        def wait(job):
            for _ in range(400):
                result=call('avplayer_job_get',{'session_id':'standalone','job_id':job})
                if result['state'] in ('succeeded','failed','cancelled'):return result
                time.sleep(.05)
            raise AssertionError('timeout')
        try:
            for _ in range(100):
                try:rpc('initialize',{});break
                except Exception:time.sleep(.1)
            catalog=rpc('tools/list',{})['tools'];assert len([t for t in catalog if t['name'] in ('get_media_context','export_video','avplayer_job_get')])==3
            assert tool('get_media_context')['loaded']
            segments=[{'start_ms':0,'end_ms':1000,'text':'native MCP'}]
            assert tool('save_transcript',{'segments':segments})['success']
            assert tool('get_transcript')['text']=='[0-1000] native MCP'
            assert (media/'transcript.srt').exists()
            assert tool('set_edit_plan',{'segments':segments})['success']
            assert not tool('save_transcript',{'segments':[{'start_ms':10,'end_ms':0,'text':'bad'}]})['success']
            print('PASS: independent context/subtitle/plan tools',flush=True)
            for name,args in [('extract_audio',{}),('export_video',{'start_ms':0,'end_ms':1000}),('export_random_clip',{'duration_ms':1000}),('export_plan',{'segments':[{'start_ms':0,'end_ms':1000}]}),('transcode_video',{'height':360})]:
                job=uuid.uuid4().hex;accepted=tool(name,args,job_id=job);assert accepted['success'],accepted
                assert tool(name,args,job_id=job)['job_id']==job
                result=wait(job);assert result['success'],result
                value=result['result'];paths=value.get('paths',[value.get('path')]);assert all(Path(path).is_file() for path in paths)
            print('PASS: native extraction/export/plan/transcode without AVPlayer services',flush=True)
            job=uuid.uuid4().hex;assert tool('transcode_video',{'height':1080},job_id=job)['success']
            call('avplayer_job_cancel',{'session_id':'standalone','job_id':job})
            assert wait(job)['state']=='cancelled'
            denied=call('avplayer_job_get',{'session_id':'other','job_id':job});assert not denied['success']
            print('PASS: idempotency, cancellation, session isolation',flush=True)
        finally:
            process.terminate()
            try:process.wait(timeout=8)
            except subprocess.TimeoutExpired:process.kill();process.wait()
            log.close()
if __name__=='__main__':main()
