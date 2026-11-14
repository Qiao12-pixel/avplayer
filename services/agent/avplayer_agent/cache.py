"""Compatibility API for the single C++-owned transcript cache."""
from .data_client import execute
class TranscriptCache:
    def __init__(self, *args, **kwargs): pass
    def get(self, session_id, media_id):
        return execute('cache_get', session_id=session_id, media_id=media_id)
    def put(self, session_id, media_id, segments):
        execute('cache_put', session_id=session_id, media_id=media_id, segments=segments)
    def drop(self, session_id, media_id):
        execute('cache_drop', session_id=session_id, media_id=media_id)
_TRANSCRIPTS = TranscriptCache()
def transcript_cache(): return _TRANSCRIPTS
