import unittest
from unittest.mock import patch
from avplayer_agent.cache import TranscriptCache

class CacheRpcTests(unittest.TestCase):
    def test_cache_read_is_cpp_rpc(self):
        segments = [{'start_ms':0,'end_ms':1000,'text':'hello'}]
        with patch('avplayer_agent.cache.execute',return_value=segments) as call:
            self.assertEqual(TranscriptCache().get('web','m1'),segments)
            call.assert_called_once_with('cache_get',session_id='web',media_id='m1')
    def test_cache_write_is_cpp_rpc(self):
        with patch('avplayer_agent.cache.execute') as call:
            TranscriptCache().put('web','m1',[])
            call.assert_called_once_with('cache_put',session_id='web',media_id='m1',segments=[])
    def test_cache_failure_is_miss(self):
        with patch('avplayer_agent.cache.execute',return_value=None):
            self.assertIsNone(TranscriptCache().get('web','m1'))
    def test_drop_preserves_session_identity(self):
        with patch('avplayer_agent.cache.execute') as call:
            TranscriptCache().drop('web','m1')
            call.assert_called_once_with('cache_drop',session_id='web',media_id='m1')
