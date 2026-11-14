"""Validate the running dev stack through Vite HTTP/WS; Qt is never launched.

Uses real WebRTC receiver, local Whisper/Qwen, native MCP tools and downloads.
Run scripts/dev.sh first. --base-url defaults to http://localhost:5173.
"""
import argparse
import asyncio
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid
import aiohttp
import grpc
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'services/agent'))
from avplayer_agent import agent_pb2 as pb,agent_pb2_grpc as rpc

async def main(base_url,report_path):
    report={'base_url':base_url,'started':time.strftime('%Y-%m-%d %H:%M:%S'),'checks':[]}
    session_id='full-test-'+uuid.uuid4().hex[:10]
    def checked(name,**details):
        report['checks'].append({'name':name,'success':True,**details})
        print('PASS:',name,details,flush=True)
        report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2))
    async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=300)) as session:
        async with session.get(base_url) as response:
            assert response.status==200 and 'AVPlayer' in await response.text()
        async with session.get(base_url+'/config') as response:
            config=await response.json();assert isinstance(config['ice_servers'],list)
        checked('Web page and same-origin config')
        clip=ROOT/'resource/input.mp4'
        async with session.post(base_url+'/upload',data=clip.open('rb'),headers={'X-Session-Id':session_id,'X-File-Name':'input.mp4'}) as response:
            uploaded=await response.json();assert response.status==200,uploaded
        media=uploaded['media_id'];report.update(session_id=session_id,media_id=media)
        checked('HTTP streaming upload + Agent media binding',bytes=uploaded['size_bytes'])
        async def control(action,position=0):
            async with session.post(base_url+'/control',json={'session_id':session_id,'media_id':media,'action':action,'position':position}) as response:
                value=await response.json();assert response.status==200 and value['success'],value
                return value
        class BrowserProxy(rpc.StreamingServiceServicer):
            async def Signal(self,requests,context):
                async with session.ws_connect(base_url.replace('http','ws',1)+'/signal') as ws:
                    async def send():
                        async for m in requests:
                            await ws.send_json({'kind':m.kind,'session_id':m.session_id,'media_id':m.media_id,'sdp':m.sdp,'candidate':m.candidate,'mid':m.mid})
                    task=asyncio.create_task(send())
                    try:
                        async for frame in ws:
                            if frame.type==aiohttp.WSMsgType.TEXT:
                                data=json.loads(frame.data)
                                if data.get('kind')=='error':raise RuntimeError(data)
                                yield pb.SignalMessage(kind=data.get('kind',''),sdp=data.get('sdp',''),candidate=data.get('candidate',''),mid=data.get('mid',''))
                    finally:
                        task.cancel();await asyncio.gather(task,return_exceptions=True)
            async def Control(self,request,context):
                value=await control(request.action,request.position)
                return pb.ControlAck(**value)
        proxy=grpc.aio.server();rpc.add_StreamingServiceServicer_to_server(BrowserProxy(),proxy)
        port=proxy.add_insecure_port('127.0.0.1:0');await proxy.start()
        recording=''
        try:
            process=await asyncio.create_subprocess_exec(str(ROOT/'cmake-build-debug/services/media/streaming_smoke_client'),f'127.0.0.1:{port}',session_id,media,'8',stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
            try:
                deadline=time.monotonic()+15
                while True:
                    try:state=await control('status');break
                    except AssertionError:
                        if time.monotonic()>deadline:raise
                        await asyncio.sleep(.2)
                assert state['duration_ms']>0
                assert not (await control('pause'))['playing']
                await control('seek',.2)
                assert (await control('play'))['playing']
                checked('WebSocket signaling and HTTP status/pause/seek/play',duration_ms=state['duration_ms'])
                out,err=await asyncio.wait_for(process.communicate(),45)
                assert process.returncode==0,err.decode()
                metrics={}
                for line in out.decode().splitlines():
                    if line.startswith('[bench] '):
                        key,value=line[8:].split('=',1);metrics[key]=value
                assert float(metrics.get('video_packets','0'))>0 and float(metrics.get('audio_kbps','0'))>0,metrics
                recording=metrics.get('recording_path','')
                assert recording
                checked('Real H264/Opus WebRTC receive and server recording',metrics=metrics)
            finally:
                if process.returncode is None:process.kill();await process.wait()
        finally:await proxy.stop(0)
        async def download(path):
            relative='clips/'+Path(path).name if Path(path).parent.name=='clips' else Path(path).name
            async with session.get(base_url+'/download',params={'session_id':session_id,'media_id':media,'file':relative}) as response:
                data=await response.read();assert response.status==200 and data
                return len(data)
        checked('Recording HTTP download',bytes=await download(recording))
        async with session.ws_connect(base_url.replace('http','ws',1)+'/agent') as ws:
            await ws.send_json({'type':'tool_catalog','session_id':session_id,'payload':{'tools':[]}})
            async def request(kind,name,arguments=None):
                request_id=uuid.uuid4().hex
                payload={'task':name} if kind=='task' else {'goal':name}
                await ws.send_json({'type':kind,'request_id':request_id,'session_id':session_id,'payload':payload,'arguments':arguments or {}})
                start=time.monotonic();status=[]
                while True:
                    frame=await ws.receive(timeout=240)
                    if frame.type!=aiohttp.WSMsgType.TEXT:raise AssertionError(('Agent stream closed',frame.type))
                    item=json.loads(frame.data)
                    if item.get('request_id')!=request_id:continue
                    data=item.get('payload',{})
                    if item.get('type')=='status':status.append(data.get('stage',''))
                    if item.get('type')=='error':raise AssertionError(data)
                    if item.get('type') in ('task_result','final'):
                        if kind=='task':assert data.get('success'),data
                        checked('Agent '+(name if kind=='task' else 'goal tool orchestration'),seconds=round(time.monotonic()-start,2),stages=sorted(set(status)))
                        return data
            transcript=await request('task','transcribe_subtitle')
            assert transcript.get('segments')
            checked('MCP transcript persisted and downloadable',segments=len(transcript['segments']),bytes=await download('transcript.srt'))
            summary=await request('task','summarize_video');assert summary.get('result','').strip()
            search=await request('task','semantic_search',{'query':transcript['segments'][0]['text']});assert search.get('result','').strip()
            plan=await request('task','smart_edit',{'instruction':'选取视频开头一段有明确字幕时间范围的内容，只返回1段，时长不超过5秒。'})
            assert plan.get('segments')
            exported=await request('task','export_plan',{'segments':plan['segments']})
            assert exported.get('paths')
            checked('Smart-edit clips downloaded',files=len(exported['paths']),bytes=[await download(path) for path in exported['paths']])
            transcode=await request('task','transcode_video',{'height':360});assert transcode.get('path')
            checked('Transcoded artifact downloaded',bytes=await download(transcode['path']))
            before_goal=set((ROOT/'storage'/session_id/media/'clips').glob('*.mp4'))
            goal=await request('goal','请调用 export_video 工具，将当前视频从0毫秒到1000毫秒导出成片段，完成后报告文件路径，不需要分析字幕。')
            assert goal.get('text','').strip()
            new_artifacts=set((ROOT/'storage'/session_id/media/'clips').glob('*.mp4'))-before_goal
            assert new_artifacts, 'Agent returned final text but produced no requested clip'
            outputs=list((ROOT/'storage'/session_id/media/'clips').glob('*.mp4'))
            checked('Native MCP output codecs inspected',artifacts=len(outputs))
            for path in outputs:
                probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_entries','stream=codec_name,width,height','-of','json',str(path)]))
                assert any(s.get('codec_name')=='h264' for s in probe['streams'])
        report['success']=True
        report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2))
        print('ALL NON-QT STACK FLOWS PASSED',flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--base-url',default='http://localhost:5173');parser.add_argument('--report',default='/tmp/avplayer-full-stack-report.json');args=parser.parse_args()
    asyncio.run(main(args.base_url,Path(args.report)))
