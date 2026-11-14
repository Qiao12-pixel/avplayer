"""Two independent Web protocol sessions: concurrent uploads, WS/WebRTC and controls.
No Qt, no browser renderer; launches isolated unified server and Agent.
"""
import asyncio
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import uuid
import aiohttp
import grpc
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'services/agent'))
from avplayer_agent import agent_pb2 as pb,agent_pb2_grpc as rpc

async def check(storage):
    base='http://127.0.0.1:51065'
    identities=['web-'+uuid.uuid4().hex for _ in range(2)]
    async with aiohttp.ClientSession() as http:
        async def upload(index):
            path=ROOT/'resource'/('bench-720p60.mp4' if index==0 else 'input.mp4')
            with path.open('rb') as file:
                async with http.post(base+'/upload',data=file,headers={'X-Session-Id':identities[index],'X-File-Name':path.name}) as response:
                    result=await response.json();assert response.status==200,result
                    return result['media_id']
        media=await asyncio.gather(upload(0),upload(1));assert media[0]!=media[1]
        for i in range(2):assert list((storage/identities[i]/media[i]).glob('original.*'))
        print('PASS: two simultaneous uploads have separate session/media directories',flush=True)
        async def control(i,action,position=0):
            async with http.post(base+'/control',json={'session_id':identities[i],'media_id':media[i],'action':action,'position':position}) as response:
                data=await response.json();assert response.status==200 and data['success'],data
                return data
        class Proxy(rpc.StreamingServiceServicer):
            async def Signal(self,requests,context):
                async with http.ws_connect(base.replace('http','ws')+'/signal') as ws:
                    async def writer():
                        async for item in requests:
                            await ws.send_json({'kind':item.kind,'session_id':item.session_id,'media_id':item.media_id,'sdp':item.sdp,'candidate':item.candidate,'mid':item.mid})
                    task=asyncio.create_task(writer())
                    try:
                        async for frame in ws:
                            if frame.type==aiohttp.WSMsgType.TEXT:
                                data=json.loads(frame.data);assert data.get('kind')!='error',data
                                yield pb.SignalMessage(kind=data.get('kind',''),sdp=data.get('sdp',''),candidate=data.get('candidate',''),mid=data.get('mid',''))
                    finally:task.cancel();await asyncio.gather(task,return_exceptions=True)
            async def Control(self,request,context):
                async with http.post(base+'/control',json={'session_id':request.session_id,'media_id':request.media_id,'action':request.action,'position':request.position}) as response:
                    return pb.ControlAck(**(await response.json()))
        server=grpc.aio.server();rpc.add_StreamingServiceServicer_to_server(Proxy(),server)
        port=server.add_insecure_port('127.0.0.1:0');await server.start();clients=[]
        try:
            for i in range(2):
                clients.append(await asyncio.create_subprocess_exec(str(ROOT/'cmake-build-debug/services/media/streaming_smoke_client'),f'127.0.0.1:{port}',identities[i],media[i],'10',stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE))
            for i in range(2):
                for attempt in range(70):
                    try:
                        state=await control(i,'status')
                        if state['duration_ms']>0:break
                    except AssertionError:pass
                    await asyncio.sleep(.1)
                else:raise AssertionError('media not ready')
            await asyncio.sleep(1)
            before=await control(1,'status')
            assert not (await control(0,'pause'))['playing']
            await asyncio.sleep(.6)
            after=await control(1,'status')
            assert after['playing'] and after['position_ms']>before['position_ms'],(before,after)
            await control(0,'seek',.3)
            assert (await control(0,'play'))['playing']
            assert (await control(1,'status'))['playing']
            print('PASS: pause/seek/resume in page A does not pause page B',flush=True)
            # Binding is verified through distinct session Runtime states in the
            # real Agent via the C++ upload registration, plus separate WS replies.
            sockets=[await http.ws_connect(base.replace('http','ws')+'/agent') for _ in range(2)]
            try:
                for i,ws in enumerate(sockets):await ws.send_json({'type':'tool_catalog','session_id':identities[i],'request_id':str(i),'payload':{'tools':[]}})
                for i,ws in enumerate(sockets):
                    frame=await ws.receive(timeout=15);assert frame.type==aiohttp.WSMsgType.TEXT
                    value=json.loads(frame.data);assert value.get('session_id')==identities[i],value
                print('PASS: separate Agent WebSocket response identities',flush=True)
            finally:
                for ws in sockets:await ws.close()
            for process in clients:
                out,err=await asyncio.wait_for(process.communicate(),40);assert process.returncode==0,err.decode()
                values=dict(line[8:].split('=',1) for line in out.decode().splitlines() if line.startswith('[bench] '))
                assert float(values['video_packets'])>0 and float(values['audio_kbps'])>0,values
            print('PASS: both WebSocket-negotiated WebRTC streams receive H264/Opus and record',flush=True)
        finally:
            for process in clients:
                if process.returncode is None:process.kill();await process.wait()
            await server.stop(0)


def main():
    with tempfile.TemporaryDirectory(prefix='avplayer-two-tabs-') as tmp:
        path=Path(tmp);storage=path/'storage'
        env=dict(os.environ,PYTHONPATH=str(ROOT/'services/agent'),AVPLAYER_PROJECT_ROOT=str(ROOT),AVPLAYER_STORAGE_ROOT=str(storage),
            AVPLAYER_AGENT_PORT='51061',AVPLAYER_AGENT_SERVER='127.0.0.1:51061',AVPLAYER_DATA_SERVER='127.0.0.1:51066',
            AVPLAYER_DATA_SERVER_ADDRESS='127.0.0.1:51066',AVPLAYER_MEDIA_SERVER_ADDRESS='127.0.0.1:51062',AVPLAYER_WEB_GATEWAY_PORT='51065',
            AVPLAYER_ZK_HOSTS='',AVPLAYER_REDIS_URL='',AVPLAYER_MYSQL_DSN='')
        processes=[];logs=[]
        try:
            for name,command in [('agent',[ROOT/'.venv/bin/python','-m','avplayer_agent']),('server',[ROOT/'cmake-build-debug/services/server/avplayer_server'])]:
                log=open(path/(name+'.log'),'w');logs.append(log)
                processes.append(subprocess.Popen(list(map(str,command)),env=env,stdout=log,stderr=subprocess.STDOUT))
            channel=grpc.insecure_channel('127.0.0.1:51066');grpc.channel_ready_future(channel).result(timeout=10)
            agent=grpc.insecure_channel('127.0.0.1:51061');grpc.channel_ready_future(agent).result(timeout=10)
            asyncio.run(check(storage))
            print('ALL TWO-WEB-SESSION CHECKS PASSED',flush=True)
        finally:
            for process in processes:process.terminate()
            for process in processes:
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait()
            for log in logs:log.close()
if __name__=='__main__':main()
