import io
import unittest
from unittest.mock import patch
from avplayer_agent.runtime import AgentRuntime
from avplayer_agent.session_manager import SessionState, SharedOllama
from avplayer_agent.storage import SessionStorage
import tempfile
from pathlib import Path


class WorkerBoundaryTests(unittest.TestCase):
    def test_upload_during_task_does_not_change_its_media(self):
        with tempfile.TemporaryDirectory() as tmp:
            state = SessionState('web', SessionStorage(Path(tmp)), SharedOllama())
            state.active_media_id = 'first'
            state.runtime._active_request_id = 'request-1'
            with patch('avplayer_agent.session_manager.run_task', return_value={'success': True}) as call:
                state._execute_tool('export_video', {}, 'web')
                state.active_media_id = 'second'
                state._execute_tool('export_plan', {}, 'web')
                self.assertEqual([args.args[2] for args in call.call_args_list], ['first', 'first'])
                state.runtime._active_request_id = 'request-2'
                state._execute_tool('export_video', {}, 'web')
                self.assertEqual(call.call_args.args[2], 'second')

    def test_transcription_calls_worker_and_saves_result(self):
        calls = []
        segments = [{'start_ms': 0, 'end_ms': 1000, 'text': 'hello'}]
        def execute(tool, arguments, session):
            calls.append(tool)
            if tool == 'get_media_context':
                return {'success': True, 'loaded': True, 'path': 'media'}
            if tool == 'transcribe_subtitle':
                return {'success': True, 'segments': segments}
            if tool == 'save_transcript':
                return {'success': True, 'path': '/tmp/transcript.srt'}
            raise AssertionError('Agent must not run audio extraction or inference locally')
        runtime = AgentRuntime(io.StringIO(), tool_executor=execute)
        runtime.local_speech_configured = lambda: True
        self.assertEqual(runtime._transcribe('web')['segments'], segments)
        self.assertEqual(calls, ['get_media_context', 'transcribe_subtitle', 'save_transcript'])

if __name__ == '__main__':
    unittest.main()
