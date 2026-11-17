"""Real MySQL + RabbitMQ/Kafka: competing MCP workers, expiry fencing, projections."""
import json,os,signal,subprocess,tempfile,time,urllib.request,uuid
from pathlib import Path
from urllib.parse import urlparse
import pymysql
ROOT=Path(__file__).resolve().parents[1]
def wait(fn,timeout=80):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  try:
   value=fn()
   if value:return value
  except (OSError,ValueError):pass
  time.sleep(.1)
 raise AssertionError('integration deadline')
def main():
 dsn=os.environ.get('AVPLAYER_MYSQL_DSN','mysql://avplayer:avplayer@127.0.0.1:3306/avplayer');u=urlparse(dsn)
 db=pymysql.connect(host=u.hostname,port=u.port or 3306,user=u.username,password=u.password,database=u.path[1:],autocommit=True,charset='utf8mb4')
 def sql(query,args=()):
  db.ping(reconnect=True)
  with db.cursor() as c:c.execute(query,args);return c.fetchall()
 with tempfile.TemporaryDirectory(prefix='mysql-shared-') as tmp:
  folder=Path(tmp);session='mysql-'+uuid.uuid4().hex[:12];media=uuid.uuid4().hex;source=folder/'storage'/session/media;source.mkdir(parents=True);os.link(ROOT/'resource/bench-1080p60.mp4',source/'original.mp4')
  topic='mysql-'+uuid.uuid4().hex[:12];projection=topic+'.audit';config=folder/'config.json';config.write_text(json.dumps({'server':{'port':51881},'logging':{'log_file_path':str(folder/'mcp.log'),'log_console_output':False},'job_agent':{'db_path':'ignored-mysql'}}))
  env=dict(os.environ,AVPLAYER_MYSQL_DSN=dsn,AVPLAYER_STORAGE_ROOT=str(folder/'storage'),MCP_MEDIA_ROOT=str(ROOT/'thirdParty/MCP'),MCP_MEDIA_SLOTS='1',MCP_JOB_LEASE_SECONDS='3',MCP_RABBIT_PORT=os.environ.get('MCP_RABBIT_PORT','5673'),MCP_KAFKA_BROKERS=os.environ.get('MCP_KAFKA_BROKERS','127.0.0.1:19092'),MCP_RABBIT_QUEUE=topic+'.tasks',MCP_KAFKA_TOPIC=topic+'.events',MCP_KAFKA_GROUP=topic+'.group',MCP_EVENT_PROJECTION=projection)
  processes=[];logs=[]
  def start(port):
   log=open(folder/f'mcp-{port}.log','a');logs.append(log);p=subprocess.Popen([str(ROOT/'build/mcp/MCP_Server'),'--config',str(config),'--host','127.0.0.1','--port',str(port)],env=env,cwd=folder,stdout=log,stderr=subprocess.STDOUT);processes.append(p);wait(lambda:rpc('tools/list',{},port));return p
  def rpc(method,params,port):
   request=urllib.request.Request(f'http://127.0.0.1:{port}/jsonrpc',data=json.dumps({'jsonrpc':'2.0','id':1,'method':method,'params':params}).encode(),headers={'Content-Type':'application/json'})
   with urllib.request.urlopen(request,timeout=5) as r:return json.load(r)['result']
  def tool(name,args,port=51881):return json.loads(rpc('tools/call',{'name':name,'arguments':args},port)['content'][0]['text'])
  def status(job,port=51881):return tool('avplayer_job_get',{'session_id':session,'job_id':job},port)
  def submit(name,args,port=51881):
   job=uuid.uuid4().hex;result=tool(name,{'session_id':session,'media_id':media,'job_id':job,'arguments':args,'timeout_seconds':120},port);assert result['success'],result;return job
  try:
   first=start(51881)
   job=submit('transcode_video',{'height':1080});wait(lambda:status(job).get('state')=='running')
   old=sql('SELECT execution_version,lease_owner FROM mcp_jobs WHERE job_id=%s',(job,))[0];os.kill(first.pid,signal.SIGSTOP)
   second=start(51882)
   newstatus=wait(lambda:(s if s.get('state')=='succeeded' else None) if (s:=status(job,51882)) else None,100)
   version=sql('SELECT execution_version FROM mcp_jobs WHERE job_id=%s',(job,))[0][0];assert version>old[0]
   artifact=Path(newstatus['result']['path']);assert artifact.exists() and f'-v{version}-' in artifact.name
   os.kill(first.pid,signal.SIGCONT);time.sleep(2)
   assert status(job,51882)['result']['path']==str(artifact)
   print('PASS: expired stopped worker fenced; successor owns versioned output; old worker cannot overwrite',flush=True)
   # Verify atomic claimant behavior under concurrent independent workers.
   ids=[submit('export_video',{'start_ms':0,'end_ms':1000},51882) for _ in range(4)]
   for id in ids:wait(lambda:status(id,51882).get('state')=='succeeded')
   assert all(row[0]==1 for row in sql('SELECT attempts FROM mcp_jobs WHERE job_id IN (%s,%s,%s,%s)',tuple(ids)))
   print('PASS: competing MCP processes share MySQL tasks without duplicate claims',flush=True)
   cancelled=submit('transcode_video',{'height':1080},51882);tool('avplayer_job_cancel',{'session_id':session,'job_id':cancelled},51882);wait(lambda:status(cancelled,51882)['state']=='cancelled')
   failed=submit('export_video',{'start_ms':2000,'end_ms':0},51882);wait(lambda:status(failed,51882).get('state')=='failed')
   print('PASS: shared cancellation and terminal failures',flush=True)
   # Event publication and consumer projection are both MySQL-backed.
   log=open(folder/'audit.log','w');logs.append(log);consumer=subprocess.Popen([str(ROOT/'build/mcp/mcp_event_consumer')],env=env,cwd=folder,stdout=log,stderr=subprocess.STDOUT);processes.append(consumer)
   expected=lambda:sql("SELECT count(*) FROM event_outbox WHERE source='mcp.kafka' AND topic=%s AND delivered=1",(env['MCP_KAFKA_TOPIC'],))[0][0]
   wait(lambda:expected()>0 and sql('SELECT count(*) FROM consumed_events WHERE projection_name=%s',(projection,))[0][0]==expected())
   count=expected();consumer.terminate();consumer.wait(timeout=10)
   replay=dict(env,MCP_KAFKA_GROUP=topic+'.replay');consumer=subprocess.Popen([str(ROOT/'build/mcp/mcp_event_consumer')],env=replay,cwd=folder,stdout=log,stderr=subprocess.STDOUT);processes.append(consumer)
   time.sleep(4);assert sql('SELECT count(*) FROM consumed_events WHERE projection_name=%s',(projection,))[0][0]>=count
   first.terminate();first.wait(timeout=12);first=start(51881);assert status(job)['state']=='succeeded'
   assert not list(folder.rglob('*.sqlite*'))
   print('PASS: MySQL outbox/audit, process restart and no new SQLite files',flush=True)
  except Exception:
   for file in folder.rglob('*.log'):print(file.name,file.read_text(errors='replace')[-2500:])
   raise
  finally:
   for process in processes:
    if process.poll() is None:
     try:os.kill(process.pid,signal.SIGCONT)
     except ProcessLookupError:pass
     process.terminate()
   for process in processes:
    try:process.wait(timeout=12)
    except subprocess.TimeoutExpired:process.kill();process.wait()
   for log in logs:log.close()
 db.close()
if __name__=='__main__':main()
