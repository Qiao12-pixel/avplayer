import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch
from avplayer_agent.db import Database
from avplayer_agent.session_manager import SessionState,SharedOllama
from avplayer_agent.storage import SessionStorage

class DataRpcTests(unittest.TestCase):
    def test_database_failure_is_safe(self):
        with patch('avplayer_agent.db.execute',return_value=None):
            self.assertIsNone(Database().load_transcript('m1'))
            self.assertEqual(Database().list_media('web'),[])
    def test_task_is_sent_to_cpp(self):
        with patch('avplayer_agent.db.execute') as call:
            Database().record_task('web','m1','summary',True,'ok')
            call.assert_called_once_with('record_task',session_id='web',media_id='m1',task='summary',success=True,result='ok')
    def test_final_result_is_recorded_for_task_media(self):
        with tempfile.TemporaryDirectory() as tmp:
            state=SessionState('web',SessionStorage(Path(tmp)),SharedOllama())
            state.active_media_id='m1'
            with patch('avplayer_agent.session_manager.database') as database:
                state.publish({'type':'task_result','payload':{'task':'summary','success':True,'result':'ok'}})
                database.return_value.record_task.assert_called_once_with('web','m1','summary',True,'ok')
