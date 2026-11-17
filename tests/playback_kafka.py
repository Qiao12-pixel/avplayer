"""Real unified backend→Kafka→statistics; verifies replay without double counts.
Kafka must run externally. Uses isolated native services and generated consumer group.
"""
import json
import os
from pathlib import Path
import pymysql
from urllib.parse import urlparse
import subprocess
import tempfile
import time
import urllib.request
import uuid
import sys
import grpc
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'services/agent'))
from avplayer_agent import agent_pb2 as pb,agent_pb2_grpc as rpc

def wait(predicate,timeout=50):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        try:
            result=predicate()
            if result:return result
        except (OSError,pymysql.Error,ValueError):pass
        time.sleep(.1)
    raise AssertionError('playback event deadline exceeded')

def main():
    with tempfile.TemporaryDirectory(prefix='avplayer-playback-kafka-') as tmp:
        root=Path(tmp);media=root/'storage'/'playback-test'/'video';media.mkdir(parents=True)
        os.link(ROOT/'resource/bench-720p60.mp4',media/'original.mp4')
        topic='test-playback-'+uuid.uuid4().hex[:12]
        env=dict(os.environ,AVPLAYER_KAFKA_BROKERS=os.environ.get('AVPLAYER_KAFKA_BROKERS','127.0.0.1:9092'),AVPLAYER_KAFKA_TOPIC=topic,
             AVPLAYER_KAFKA_GROUP=topic+'.stats',AVPLAYER_STATS_PROJECTION=topic+'.projection',AVPLAYER_MYSQL_DSN=os.environ.get('AVPLAYER_MYSQL_DSN','mysql://avplayer:avplayer@127.0.0.1:3306/avplayer'),
             AVPLAYER_STORAGE_ROOT=str(root/'storage'),AVPLAYER_WEB_GATEWAY_PORT='51765',AVPLAYER_DATA_SERVER_ADDRESS='127.0.0.1:51766',
             PYTHONPATH=str(ROOT/'services/agent'),AVPLAYER_AGENT_PORT='51761',AVPLAYER_AGENT_SERVER='127.0.0.1:51761',AVPLAYER_DATA_SERVER='127.0.0.1:51766',AVPLAYER_MEDIA_SERVER_ADDRESS='127.0.0.1:51762',AVPLAYER_REDIS_URL='',AVPLAYER_ZK_HOSTS='')
        processes=[];logs=[]
        def launch(binary,name,config=env):
            log=open(root/(name+'.log'),'w');logs.append(log);process=subprocess.Popen([str(ROOT/'cmake-build-debug/services/server'/binary)],env=config,cwd=root,stdout=log,stderr=subprocess.STDOUT);processes.append(process);return process
        def health():
            with urllib.request.urlopen('http://127.0.0.1:51765/health',timeout=2) as response:return json.load(response)
        def stats():
            with urllib.request.urlopen('http://127.0.0.1:51765/playback-stats',timeout=2) as response:return json.load(response)
        dsn=urlparse(env['AVPLAYER_MYSQL_DSN'])
        def query(sql,args=()):
            db=pymysql.connect(host=dsn.hostname,port=dsn.port or 3306,user=dsn.username,password=dsn.password,database=dsn.path[1:],autocommit=True)
            try:
                with db.cursor() as cursor:cursor.execute(sql,args);return cursor.fetchall()
            finally:db.close()
        def count():return query('SELECT count(*) FROM consumed_events WHERE projection_name=%s',(env['AVPLAYER_STATS_PROJECTION'],))[0][0]
        try:
            server=launch('avplayer_server','server');consumer=launch('playback_event_consumer','consumer')
            wait(health)
            log=open(root/'agent.log','w');logs.append(log)
            agent=subprocess.Popen([str(ROOT/'.venv/bin/python'),'-m','avplayer_agent'],env=env,cwd=root,stdout=log,stderr=subprocess.STDOUT);processes.append(agent)
            connection=grpc.insecure_channel('127.0.0.1:51761');grpc.channel_ready_future(connection).result(timeout=10)
            upload=urllib.request.Request('http://127.0.0.1:51765/upload',data=b'upload event fixture',headers={'X-Session-Id':'event-upload','X-File-Name':'fixture.bin'})
            with urllib.request.urlopen(upload,timeout=10) as response:uploaded=json.load(response)
            data_channel=grpc.insecure_channel('127.0.0.1:51766');storage=rpc.StorageServiceStub(data_channel)
            stored=storage.Execute(pb.Struct(json=json.dumps({'action':'result_put','session_id':'event-upload','media_id':uploaded['media_id'],'task':'summarize_video','payload':{'success':True,'result':'result event fixture'}})),timeout=5)
            assert json.loads(stored.json)['mysql_saved']
            wait(lambda:stats().get('event_counts',{}).get('media.upload_completed')==1 and stats().get('event_counts',{}).get('ai.result_persisted')==1)
            print('PASS: upload and AI persistence outcomes enter project Kafka topic',flush=True)
            # Real receiver drives pause/play/record_start/record_stop at end.
            receiver=subprocess.Popen([str(ROOT/'cmake-build-debug/services/media/streaming_smoke_client'),'127.0.0.1:51766','playback-test','video','8'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            processes.append(receiver)
            wait(lambda:stats().get('active_connections')==1,15)
            request=urllib.request.Request('http://127.0.0.1:51765/control',data=json.dumps({'session_id':'playback-test','media_id':'video','action':'seek','position':.2}).encode(),headers={'Content-Type':'application/json'})
            with urllib.request.urlopen(request,timeout=5) as response:assert json.load(response)['success']
            out,err=receiver.communicate(timeout=40)
            assert receiver.returncode==0,err
            print('PASS: live active-connection statistics and seek event',flush=True)
            required={'playback.requested','playback.connected','playback.paused','playback.resumed','playback.closed','recording.started','recording.completed','playback.seeked'}
            state=wait(lambda: (s if required.issubset(s.get('event_counts',{})) and s['active_connections']==0 else None) if (s:=stats()) else None)
            assert all(state['event_counts'][key]==1 for key in required),state
            assert state['sessions_observed']==1 and state['completed_connection_ms']>0,state
            print('PASS: actual WebRTC connection/control/recording lifecycle becomes Kafka statistics',state,flush=True)
            # Save the high-water mark, then replay into the same projection.
            expected=count();consumer.terminate();consumer.wait(timeout=10)
            replay=launch('playback_event_consumer','replay',dict(env,AVPLAYER_KAFKA_GROUP=topic+'.replay'))
            wait(lambda:sum('[playback-stats] committed' in line for line in (root/'replay.log').read_text().splitlines())>=expected)
            assert stats()['event_counts']['playback.connected']==1
            assert stats()['sessions_observed']==1
            print('PASS: Kafka new-group replay is idempotent in shared MySQL projection',flush=True)
            replay.terminate();replay.wait(timeout=10)
            # A fresh projection must be rebuilt solely from retained Kafka events.
            fresh=topic+'.rebuilt-projection'
            rebuilt=launch('playback_event_consumer','rebuilt',dict(env,AVPLAYER_KAFKA_GROUP=topic+'.rebuild',AVPLAYER_STATS_PROJECTION=fresh))
            def rebuilt_ok():
                rows=query("SELECT event_count FROM playback_event_counts WHERE projection_name=%s AND event_type='recording.completed'",(fresh,))
                return rows and rows[0][0]==1
            wait(rebuilt_ok)
            print('PASS: new MySQL projection rebuilt from Kafka history',flush=True)
            rebuilt.terminate();rebuilt.wait(timeout=10)
            # Kafka outage: producer keeps durable data while playback still runs.
            server.terminate();server.wait(timeout=10)
            server=launch('avplayer_server','offline-kafka',dict(env,AVPLAYER_KAFKA_BROKERS='127.0.0.1:1'))
            wait(health)
            result=subprocess.run([str(ROOT/'cmake-build-debug/services/media/streaming_smoke_client'),'127.0.0.1:51766','playback-test','video','2'],capture_output=True,text=True,timeout=40)
            assert result.returncode==0,result.stderr
            assert query("SELECT count(*) FROM event_outbox WHERE source='avplayer.playback' AND topic=%s AND delivered=0",(topic,))[0][0]>0
            server.terminate();server.wait(timeout=10)
            server=launch('avplayer_server','recovered')
            consumer=launch('playback_event_consumer','recovered-consumer')
            def recovered():
                s=stats();return s if s.get('event_counts',{}).get('playback.connected')==2 and s['active_connections']==0 else None
            wait(recovered)
            print('PASS: Kafka outage does not interrupt playback; outbox recovers after restart',flush=True)
            print('ALL BACKEND PLAYBACK KAFKA CHECKS PASSED',flush=True)
        except Exception:
            for log in root.glob('*.log'):print(log.name,log.read_text(errors='replace')[-2000:])
            raise
        finally:
            for process in processes:
                if process.poll() is None:process.terminate()
            for process in processes:
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait()
            for log in logs:log.close()
if __name__=='__main__':main()
