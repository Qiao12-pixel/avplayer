import io
import unittest
from unittest.mock import patch
from langchain_core.messages import AIMessage
from avplayer_agent.runtime import AgentRuntime


class StoredResultTests(unittest.TestCase):
    def test_cached_transcription_skips_speech_and_returns_segments(self):
        runtime=AgentRuntime(io.StringIO())
        runtime._tool_executor=lambda *args: {'success':True,'loaded':True,'path':'media'}
        value={'success':True,'segments':[{'start_ms':0,'end_ms':1000,'text':'hello'}]}
        runtime.result_store=lambda *args,**kwargs:value
        with patch.object(runtime,'local_speech_configured',side_effect=AssertionError('must not check inference')):
            self.assertTrue(runtime._transcribe('web')['cached'])

    def test_cached_summary_skips_model(self):
        runtime=AgentRuntime(io.StringIO())
        transcript={'success':True,'text':'[0-1000] hello','source_path':'media','has_timestamps':True}
        def store(action,task,**kwargs):
            return transcript if task=='transcribe_subtitle' else {'success':True,'result':'saved summary'}
        runtime.result_store=store
        with patch.object(runtime,'_model',side_effect=AssertionError('must not invoke model')):
            self.assertEqual(runtime._perform('summarize_video',{},'web')['result'],'saved summary')

    def test_new_summary_is_persisted_and_variant_tracks_transcript(self):
        runtime=AgentRuntime(io.StringIO())
        calls=[]
        transcript={'success':True,'text':'[0-1000] hello','source_path':'media','has_timestamps':True}
        def store(action,task,payload=None,variant=''):
            calls.append((action,task,payload,variant))
            if task=='transcribe_subtitle':return dict(transcript)
            return {'mysql_saved':True,'redis_cached':True} if action=='put' else None
        runtime.result_store=store
        with patch.object(runtime,'_model') as model,patch.object(runtime,'_require_cpp',return_value={'path':'media'}):
            model.return_value.invoke.return_value=AIMessage(content='new summary')
            result=runtime._perform('summarize_video',{},'web')
            self.assertTrue(result['persistence']['mysql_saved'])
            first=[c for c in calls if c[0]=='put'][0][3]
            transcript['text']='[0-1000] changed'
            runtime._perform('summarize_video',{},'web')
            second=[c for c in calls if c[0]=='put'][-1][3]
            self.assertNotEqual(first,second)
