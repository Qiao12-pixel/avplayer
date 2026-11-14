"""Owns a temporary ZooKeeper process; verifies disconnect/session recovery.

Set ZK_SERVER to zkServer(.sh). Does not touch any existing ZooKeeper instance.
"""
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

import grpc
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'services/agent'))
from avplayer_agent import agent_pb2 as pb,agent_pb2_grpc as rpc


def wait(predicate, timeout=30):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        try:
            value=predicate()
            if value:return value
        except (OSError,ValueError,grpc.RpcError):pass
        time.sleep(.1)
    raise AssertionError('recovery deadline exceeded')


def main():
    command=os.environ.get('ZK_SERVER') or shutil.which('zkServer')
    if not command:raise RuntimeError('set ZK_SERVER to your zkServer.sh')
    build=Path(os.environ.get('AVPLAYER_BUILD_DIR',ROOT/'cmake-build-debug'))
    processes=[];handles=[];zk=None
    with tempfile.TemporaryDirectory(prefix='avplayer-zk-recovery-') as tmp:
        path=Path(tmp);(path/'conf').mkdir()
        (path/'conf/zoo.cfg').write_text(f'tickTime=1000\ndataDir={path}/data\nclientPort=52182\nadmin.enableServer=false\n')
        def launch(cmd,env=None,group=False,name='process'):
            log=open(path/(name+'.log'),'a');handles.append(log)
            p=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=group)
            if not group:processes.append(p)
            return p
        def start_zk():
            p=launch([command,'--config',str(path/'conf'),'start-foreground'],env=dict(os.environ,JMXDISABLE='true',ZOO_LOG_DIR=str(path)),group=True,name='zk')
            def port():
                with socket.create_connection(('127.0.0.1',52182),timeout=.3):return True
            wait(port);return p
        def stop_zk(p):
            if p and p.poll() is None:
                os.killpg(p.pid,signal.SIGTERM)
                try:p.wait(timeout=10)
                except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait()
        def health():
            with urllib.request.urlopen('http://127.0.0.1:50565/health',timeout=2) as r:return json.load(r)['routing']
        env=dict(os.environ,AVPLAYER_ZK_HOSTS='127.0.0.1:52182',AVPLAYER_ZK_MEDIA_PATH='/recovery/media',
                 AVPLAYER_MEDIA_SERVER_ADDRESS='127.0.0.1:50562',AVPLAYER_MEDIA_ADVERTISE='127.0.0.1:50562',
                 AVPLAYER_DATA_SERVER_ADDRESS='127.0.0.1:50566',AVPLAYER_DATA_SERVER='127.0.0.1:50566',
                 AVPLAYER_WEB_GATEWAY_PORT='50565',AVPLAYER_STORAGE_ROOT=str(path/'storage'),AVPLAYER_MYSQL_DSN='',AVPLAYER_REDIS_URL='')
        try:
            zk=start_zk()
            launch([str(build/'services/media/media_server')],env,name='media')
            launch([str(build/'services/gateway/web_gateway')],env,name='gateway')
            wait(lambda:health()['backends']==['127.0.0.1:50562'])
            clip=ROOT/'resource/bench-1080p60.mp4'
            folder=path/'storage'/'recovery'/'video';folder.mkdir(parents=True);os.link(clip,folder/'original.mp4')
            client=launch([str(build/'services/media/streaming_smoke_client'),'127.0.0.1:50566','recovery','video','25'],name='client')
            wait(lambda:health()['sessions'])
            channel=grpc.insecure_channel('127.0.0.1:50566');stub=rpc.StreamingServiceStub(channel)
            request=pb.ControlRequest(session_id='recovery',media_id='video',action='status')
            wait(lambda:stub.Control(request,timeout=2).success)
            stop_zk(zk)
            wait(lambda:not health()['discovery_connected'])
            assert stub.Control(request,timeout=3).success
            try:
                next(stub.Signal(iter([pb.SignalMessage(kind='offer',session_id='new',media_id='video')]),timeout=3))
                raise AssertionError('new allocation allowed while disconnected')
            except grpc.RpcError as e:assert e.code()==grpc.StatusCode.UNAVAILABLE,e
            print('PASS: ZooKeeper outage rejects new sessions but preserves existing control',flush=True)
            # Longer than negotiated six-second session timeout.
            time.sleep(8)
            zk=start_zk()
            wait(lambda:health()['discovery_connected'] and health()['backends']==['127.0.0.1:50562'])
            assert stub.Control(request,timeout=3).success
            assert client.wait(timeout=40)==0
            print('PASS: expired ZooKeeper sessions reconnect/re-register; active WebRTC completes',flush=True)
        except Exception:
            for log in path.glob('*.log'):print(log.name,log.read_text(errors='replace')[-3000:],file=sys.stderr)
            raise
        finally:
            for p in reversed(processes):
                if p.poll() is None:p.terminate()
            for p in processes:
                try:p.wait(timeout=5)
                except subprocess.TimeoutExpired:p.kill();p.wait()
            stop_zk(zk)
            for handle in handles:handle.close()
if __name__=='__main__':main()
