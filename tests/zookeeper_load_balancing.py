"""Real ZooKeeper + two real media processes + gateway + WebRTC clients.

Requires a dedicated ZooKeeper endpoint via AVPLAYER_TEST_ZK (default 52181).
Starts only its own isolated C++ processes, writes artifacts into a temp folder.
No Python ZooKeeper client or fake backend is used.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import urllib.request

import grpc

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'services/agent'))
from avplayer_agent import agent_pb2 as pb, agent_pb2_grpc as rpc

BUILD = Path(os.environ.get('AVPLAYER_BUILD_DIR', ROOT / 'cmake-build-debug'))
ZK = os.environ.get('AVPLAYER_TEST_ZK', '127.0.0.1:52181')
PROCESSES = []
LOGS = []


def wait(predicate, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            result = predicate()
            if result:
                return result
        except (OSError, ValueError, grpc.RpcError):
            pass
        time.sleep(.1)
    raise AssertionError('condition not met within deadline')


def health():
    with urllib.request.urlopen('http://127.0.0.1:50465/health', timeout=2) as r:
        return json.load(r)['routing']


def start(binary, env, log):
    output = open(log, 'w')
    LOGS.append(output)
    p = subprocess.Popen([str(binary)], env=env, stdout=output, stderr=subprocess.STDOUT)
    PROCESSES.append(p)
    return p


def stop(p):
    if p.poll() is None:
        p.kill()
        p.wait(timeout=10)


def main():
    with tempfile.TemporaryDirectory(prefix='avplayer-zk-lb-') as tmp:
        folder = Path(tmp)
        base = os.environ.copy()
        base.update(AVPLAYER_ZK_HOSTS=ZK,
                    AVPLAYER_ZK_MEDIA_PATH='/avplayer-test-' + str(os.getpid()) + '/media',
                    AVPLAYER_STORAGE_ROOT=str(folder / 'storage'),
                    AVPLAYER_WEB_GATEWAY_PORT='50465',
                    AVPLAYER_DATA_SERVER='127.0.0.1:50466',
                    AVPLAYER_DATA_SERVER_ADDRESS='127.0.0.1:50466',
                    AVPLAYER_MYSQL_DSN='', AVPLAYER_REDIS_URL='')
        try:
            start(BUILD/'services/gateway/web_gateway', base, folder/'gateway.log')
            wait(lambda: health())
            channel = grpc.insecure_channel('127.0.0.1:50466')
            stub = rpc.StreamingServiceStub(channel)
            def reject():
                try:
                    next(stub.Signal(iter([pb.SignalMessage(kind='offer', session_id='x', media_id='x')]), timeout=3))
                    raise AssertionError('empty registry must reject')
                except grpc.RpcError as e:
                    assert e.code() == grpc.StatusCode.UNAVAILABLE, e
            reject()
            medias=[]
            for port in [50462,50463]:
                env = dict(base, AVPLAYER_MEDIA_SERVER_ADDRESS=f'127.0.0.1:{port}',
                           AVPLAYER_MEDIA_ADVERTISE=f'127.0.0.1:{port}')
                medias.append(start(BUILD/'services/media/media_server',env,folder/f'media-{port}.log'))
            wait(lambda: len(health()['backends']) == 2)
            clip = ROOT/'resource/bench-720p60.mp4'
            if not clip.exists():
                clip = ROOT/'resource/input.mp4'
            for i in range(6):
                path = folder/'storage'/f'lb-{i}'/'video'
                path.mkdir(parents=True)
                os.link(clip, path/'original.mp4')
            def client(i, seconds=8):
                output = open(folder/f'client-{i}.log','w')
                LOGS.append(output)
                p = subprocess.Popen([str(BUILD/'services/media/streaming_smoke_client'),
                                      '127.0.0.1:50466', f'lb-{i}', 'video', str(seconds)],
                                     stdout=output, stderr=subprocess.STDOUT)
                PROCESSES.append(p)
                return p
            clients=[client(i) for i in range(4)]
            snapshot=wait(lambda: (h if sum(x['active'] for x in h['sessions'])==4 else None)
                          if (h:=health()) else None)
            assert sorted(x['active'] for x in snapshot['sessions'])==[2,2],snapshot
            print('PASS: four real WebRTC sessions split 2/2', flush=True)
            for i in range(4):
                request = pb.ControlRequest(session_id=f'lb-{i}',media_id='video',action='status')
                wait(lambda: stub.Control(request, timeout=5).success)
                ack, call=stub.Control.with_call(request,timeout=5)
                assert ack.success,ack
                metadata=dict(call.initial_metadata())
                assert metadata['x-avplayer-media-backend'] in snapshot['backends']
            # Duplicate logical sessions must not overwrite live bindings.
            try:
                next(stub.Signal(iter([pb.SignalMessage(kind='offer',session_id='lb-0',media_id='video')]),timeout=3))
                raise AssertionError('duplicate accepted')
            except grpc.RpcError as e:
                assert e.code()==grpc.StatusCode.ALREADY_EXISTS,e
            print('PASS: sticky control routing + duplicate rejection',flush=True)
            for p in clients:
                assert p.wait(timeout=40)==0, (folder/'gateway.log').read_text()
            wait(lambda: not health()['sessions'])
            print('PASS: H.264/Opus receive, recording, disconnect cleanup',flush=True)
            stop(medias[0])
            wait(lambda: health()['backends']==['127.0.0.1:50463'])
            p=client(4,3)
            wait(lambda: health()['sessions'])
            ack,call=stub.Control.with_call(pb.ControlRequest(session_id='lb-4',media_id='video',action='status'),timeout=5)
            wait(lambda: stub.Control(pb.ControlRequest(session_id='lb-4',media_id='video',action='status'),timeout=5).success)
            ack,call=stub.Control.with_call(pb.ControlRequest(session_id='lb-4',media_id='video',action='status'),timeout=5)
            assert ack.success and dict(call.initial_metadata())['x-avplayer-media-backend']=='127.0.0.1:50463'
            assert p.wait(timeout=30)==0
            print('PASS: SIGKILL + ephemeral expiry, new stream uses survivor',flush=True)
            stop(medias[1]);wait(lambda: not health()['backends']);reject()
            print('PASS: no backend returns UNAVAILABLE; no random fallback',flush=True)
            env=dict(base,AVPLAYER_MEDIA_SERVER_ADDRESS='127.0.0.1:50462',AVPLAYER_MEDIA_ADVERTISE='127.0.0.1:50462')
            start(BUILD/'services/media/media_server',env,folder/'media-restart.log')
            wait(lambda: health()['backends']==['127.0.0.1:50462'])
            p = client(5, 3)
            wait(lambda: health()['sessions'])
            ack = stub.Control(pb.ControlRequest(session_id='lb-5',media_id='video',action='status'), timeout=5)
            wait(lambda: stub.Control(pb.ControlRequest(session_id='lb-5',media_id='video',action='status'),timeout=5).success)
            assert p.wait(timeout=30)==0
            print('PASS: restarted instance discovered and accepts WebRTC',flush=True)
            # Fail a backend while it owns an active session: route must disappear.
            p = client(0, 5)
            wait(lambda: health()['sessions'])
            ack = stub.Control(pb.ControlRequest(session_id='lb-0',media_id='video',action='status'), timeout=5)
            wait(lambda: stub.Control(pb.ControlRequest(session_id='lb-0',media_id='video',action='status'),timeout=5).success)
            for process in PROCESSES:
                if process.args == [str(BUILD/'services/media/media_server')] and process.poll() is None:
                    stop(process)
            wait(lambda: not health()['sessions'])
            ack = stub.Control(pb.ControlRequest(session_id='lb-0',media_id='video',action='status'), timeout=5)
            assert not ack.success
            p.wait(timeout=25)
            print('PASS: active backend death cancels stream and clears route',flush=True)
            print('ALL ZOOKEEPER LOAD BALANCING CHECKS PASSED',flush=True)
        except Exception:
            for log in folder.glob('*.log'):
                print(log.name,log.read_text(errors='replace')[-2500:],file=sys.stderr)
            raise
        finally:
            for p in reversed(PROCESSES):stop(p)
            for log in LOGS:log.close()

if __name__=='__main__':
    main()
