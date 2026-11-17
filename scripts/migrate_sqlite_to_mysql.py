#!/usr/bin/env python3
"""Explicit offline migration. Never deletes SQLite inputs or rewrites artifacts.
Run the new backend/MCP once first to create MySQL tables; stop all producers
and consumers before importing. Requires pymysql in the invoking interpreter.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3
from urllib.parse import urlparse,unquote
import pymysql


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--kind',required=True,choices=['jobs','playback','audit','history','job-agent'])
    parser.add_argument('--sqlite',required=True,type=Path)
    parser.add_argument('--projection',default='mcp-audit-v1')
    parser.add_argument('--topic',help='defaults to the event topic for the selected kind')
    parser.add_argument('--queue',default='avplayer.media.tasks')
    parser.add_argument('--stopped',action='store_true',help='confirm all old/new task and publisher processes are stopped')
    args=parser.parse_args()
    args.topic=args.topic or ('avplayer.media.events' if args.kind=='jobs' else 'avplayer.playback.events')
    if not args.stopped:parser.error('stop task/publisher/consumer processes first; pass --stopped to confirm')
    uri=urlparse(os.environ.get('AVPLAYER_MYSQL_DSN',''))
    if uri.scheme!='mysql':parser.error('AVPLAYER_MYSQL_DSN must identify the shared MySQL database')
    if not args.sqlite.is_file():parser.error('SQLite input file not found')
    old=sqlite3.connect(args.sqlite.resolve().as_uri()+'?mode=ro',uri=True)
    old.row_factory=sqlite3.Row
    db=pymysql.connect(host=uri.hostname,port=uri.port or 3306,user=unquote(uri.username or ''),password=unquote(uri.password or ''),database=uri.path[1:],charset='utf8mb4',autocommit=False)
    namespace=hashlib.sha256(str(args.sqlite.resolve()).encode()).hexdigest()[:16]
    imported=0
    def outbox(cursor,source,key,topic,payload,token):
        event=hashlib.sha256((namespace+':'+str(token)).encode()).hexdigest()[:32]
        payload=dict(payload,event_id=payload.get('event_id',event))
        cursor.execute('INSERT IGNORE INTO event_outbox(event_id,source,aggregate_id,topic,payload) VALUES(%s,%s,%s,%s,%s)',(event,source,key,topic,json.dumps(payload,ensure_ascii=False)))
    try:
        with db.cursor() as cursor:
            if args.kind=='jobs':
                for row in old.execute('SELECT * FROM media_jobs'):
                    request=json.loads(row['request']);status=json.loads(row['status']);state=status['state']
                    if state=='running':state='queued';status.update(state=state,worker='',error='offline migration: previous execution revoked')
                    if row['cancelled']:state='cancelled';status.update(state=state,success=False)
                    # Do not use INSERT IGNORE: an ID collision with live MySQL state must abort.
                    cursor.execute('SELECT request_json FROM mcp_jobs WHERE job_id=%s',(row['id'],))
                    existing=cursor.fetchone()
                    if existing:
                        if json.loads(existing[0])!=request:raise ValueError('conflicting existing job ID: '+row['id'])
                        continue
                    cursor.execute('INSERT INTO mcp_jobs(job_id,session_id,media_id,queue_name,request_json,status_json,state,cancel_requested,attempts,execution_version,result_json,error_text) VALUES(%s,%s,%s,%s,%s,%s,%s,%s,%s,1,%s,%s)',(row['id'],row['session'],request['media_id'],args.queue,json.dumps(request),json.dumps(status),state,row['cancelled'],row['attempts'],json.dumps(status.get('result',{})),status.get('error','')))
                    if state=='queued':outbox(cursor,'mcp.rabbit',row['id'],args.queue,{'job_id':row['id']},'job:'+row['id'])
                    imported+=1
                for row in old.execute('SELECT * FROM media_event_outbox WHERE sent=0'):
                    payload=json.loads(row['payload']);payload['event_id']=str(row['id'])+':'+row['job']
                    outbox(cursor,'mcp.kafka',row['job'],args.topic,payload,'task-event:'+str(row['id']))
            elif args.kind=='playback':
                names={row[0] for row in old.execute("SELECT name FROM sqlite_master WHERE type='table'")}
                # Import retained raw events back into Kafka for a fresh shared projection.
                if 'playback_events' in names:
                    for row in old.execute('SELECT payload FROM playback_events'):
                        value=json.loads(row['payload']);outbox(cursor,'avplayer.playback',value.get('playback_id') or value['session_id']+':'+value['media_id'],args.topic,value,'playback:'+value['event_id']);imported+=1
                if 'playback_outbox' in names:
                    for row in old.execute('SELECT payload FROM playback_outbox WHERE sent=0'):
                        value=json.loads(row['payload']);outbox(cursor,'avplayer.playback',value.get('playback_id') or value['session_id']+':'+value['media_id'],args.topic,value,'playback:'+value['event_id']);imported+=1
            elif args.kind=='audit':
                for row in old.execute('SELECT event_id,payload FROM media_events'):
                    cursor.execute('INSERT IGNORE INTO consumed_events(projection_name,event_id,payload) VALUES(%s,%s,%s)',(args.projection,row['event_id'],row['payload']));imported+=1
            else:
                tables=['tool_call_history'] if args.kind=='history' else ['resume_profiles','job_posts','match_results','interview_records','application_records']
                for table in tables:
                    for row in old.execute('SELECT * FROM '+table):
                        # Preserve relational IDs. Collision aborts; never replace another user's rows.
                        columns=list(row.keys());placeholders=','.join(['%s']*len(columns))
                        cursor.execute('INSERT INTO `'+table+'` ('+','.join('`'+c+'`' for c in columns)+') VALUES('+placeholders+')',tuple(row));imported+=1
        db.commit();print(f'Imported {imported} records. Source SQLite preserved. Start publishers/consumers to rebuild projections.')
    except Exception:db.rollback();raise
    finally:db.close();old.close()

if __name__=='__main__':main()
