import os,subprocess,time,json,asyncio,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'services/agent'))
from avplayer_agent import agent_pb2 as pb,agent_pb2_grpc as rpc
import grpc,aiohttp
root=Path.cwd();env=os.environ.copy();env.update(PYTHONPATH=str(root/'services/agent'),AVPLAYER_AGENT_PORT='50361',AVPLAYER_DATA_SERVER='localhost:50366',AVPLAYER_DATA_SERVER_ADDRESS='127.0.0.1:50366',AVPLAYER_AGENT_SERVER='localhost:50361',AVPLAYER_MEDIA_SERVER='localhost:50362',AVPLAYER_MEDIA_SERVER_ADDRESS='127.0.0.1:50362',AVPLAYER_WEB_GATEWAY_PORT='50365',AVPLAYER_STORAGE_ROOT=str(root/'storage'),AVPLAYER_REDIS_URL=os.environ.get('AVPLAYER_REDIS_URL','redis://127.0.0.1:6379/0'),AVPLAYER_MYSQL_DSN=os.environ.get('AVPLAYER_MYSQL_DSN','mysql://avplayer:avplayer@127.0.0.1:3306/avplayer'))
ps=[]
async def test():
 async with aiohttp.ClientSession() as session:
  async with session.get('http://localhost:50365/health') as r:assert r.status==200
  async with session.post('http://localhost:50365/upload',data=(root/'resource/bench-1080p60.mp4').read_bytes(),headers={'X-Session-Id':'cpp-migration','X-File-Name':'video.mp4'}) as r:
   handle=await r.json();assert r.status==200,handle
  print('upload',handle,flush=True)
  async with session.get(f'http://localhost:50365/download?session_id=cpp-migration&media_id={handle["media_id"]}&file=original.mp4') as r:
   assert r.status==200; assert len(await r.read())==handle['size_bytes']
  channel=grpc.insecure_channel('localhost:50366');storage=rpc.StorageServiceStub(channel)
  def execute(action,**kw):return json.loads(storage.Execute(pb.Struct(json=json.dumps({'action':action,**kw})),timeout=5).json)
  segments=[{'start_ms':0,'end_ms':1000,'text':'C++ 数据迁移验证'}];mid=handle['media_id']
  execute('cache_put',session_id='cpp-migration',media_id=mid,segments=segments)
  assert execute('cache_get',session_id='cpp-migration',media_id=mid)==segments
  assert execute('cache_get',session_id='other-session',media_id=mid) is None
  execute('save_transcript',session_id='cpp-migration',media_id=mid,segments=segments)
  assert execute('load_transcript',media_id=mid)==segments
  print('Redis round-trip + isolation, MySQL transcript round-trip OK',flush=True)
  async with session.ws_connect('http://localhost:50365/agent') as ws:
   await ws.send_json({'type':'tool_catalog','session_id':'cpp-migration','payload':{'tools':[]}})
   msg=await ws.receive(timeout=10);assert msg.type==aiohttp.WSMsgType.TEXT,msg
   print('Agent WebSocket OK',flush=True)
  class Proxy(rpc.StreamingServiceServicer):
   async def Signal(self, requests, context):
    async with session.ws_connect('http://localhost:50365/signal') as ws:
     async def writer():
      async for m in requests:
       await ws.send_json({'kind':m.kind,'session_id':m.session_id,'media_id':m.media_id,'sdp':m.sdp,'candidate':m.candidate,'mid':m.mid})
     task=asyncio.create_task(writer())
     try:
      async for frame in ws:
       if frame.type==aiohttp.WSMsgType.TEXT:
        d=json.loads(frame.data)
        yield pb.SignalMessage(kind=d.get('kind',''),sdp=d.get('sdp',''),candidate=d.get('candidate',''),mid=d.get('mid',''))
     finally:
      task.cancel()
      await asyncio.gather(task,return_exceptions=True)
   async def Control(self, request, context):
    async with session.post('http://localhost:50365/control',json={'session_id':request.session_id,'media_id':request.media_id,'action':request.action,'position':request.position}) as response:
     d=await response.json();return pb.ControlAck(**d)
  proxy=grpc.aio.server();rpc.add_StreamingServiceServicer_to_server(Proxy(),proxy);proxy.add_insecure_port('127.0.0.1:50367');await proxy.start()
  result=await asyncio.to_thread(subprocess.run,[str(root/'cmake-build-debug/services/media/streaming_smoke_client'),'localhost:50367','cpp-migration',mid,'3'],capture_output=True,text=True,timeout=40)
  await proxy.stop(0)
  print(result.stdout,flush=True);assert result.returncode==0,result.stderr
  async with session.post('http://localhost:50365/control',json={'session_id':'missing','media_id':'missing','action':'status'}) as r:assert r.status==400
  async with session.get('http://localhost:50365/download?session_id=x&media_id=x&file=../../../../etc/passwd') as r:assert r.status==404
  print('ALL MIGRATION CHECKS PASSED',flush=True)
try:
 for name,cmd in [('agent',['.venv/bin/python','-m','avplayer_agent']),('gateway',['cmake-build-debug/services/gateway/web_gateway']),('media',['cmake-build-debug/services/media/media_server'])]:
  ps.append(subprocess.Popen(cmd,env=env,stdout=open('/tmp/cpp-migration-'+name+'.log','w'),stderr=subprocess.STDOUT))
 time.sleep(3);asyncio.run(test())
finally:
 for p in ps:p.terminate()
 for p in ps:
  try:p.wait(timeout=5)
  except subprocess.TimeoutExpired:p.kill()
