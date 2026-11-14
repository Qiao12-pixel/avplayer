"""Internal RPC adapter; MySQL implementation is exclusively in C++."""
from .data_client import execute
class Database:
    def __init__(self, *args, **kwargs): pass
    def record_session(self, session_id):
        execute('record_session', session_id=session_id)
    def record_media(self, session_id, media_id, file_name, size_bytes):
        execute('record_media', session_id=session_id, media_id=media_id, file_name=file_name, size_bytes=size_bytes)
    def save_transcript(self, session_id, media_id, segments):
        execute('save_transcript', session_id=session_id, media_id=media_id, segments=segments)
    def record_task(self, session_id, media_id, task, success, result):
        execute('record_task', session_id=session_id, media_id=media_id, task=task, success=success, result=result)
    def load_transcript(self, media_id):
        return execute('load_transcript', media_id=media_id)
    def list_media(self, session_id):
        return execute('list_media', session_id=session_id) or []
_DATABASE = Database()
def database(): return _DATABASE
