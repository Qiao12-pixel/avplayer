import unittest
from unittest.mock import patch
from avplayer_agent import mcp_client

class McpBoundaryTests(unittest.TestCase):
    def test_light_call_binds_identity_outside_model_arguments(self):
        with patch.object(mcp_client,'client') as factory:
            factory.return_value.call.return_value={'success':True}
            mcp_client.call_tool('get_media_context','trusted','media',{'session_id':'injected'})
            args=factory.return_value.call.call_args.args[1]
            self.assertEqual(args['session_id'],'trusted')
            self.assertEqual(args['arguments']['session_id'],'injected')
    def test_heavy_call_submits_and_polls_mcp(self):
        with patch.object(mcp_client,'client') as factory,patch.object(mcp_client.time,'sleep'):
            factory.return_value.call.side_effect=[{'success':True,'state':'queued'},
                {'success':True,'state':'succeeded','result':{'success':True,'path':'output'}}]
            result=mcp_client.run_task('export_video','s','m',{'duration_ms':1000})
            self.assertTrue(result['success'])
            calls=factory.return_value.call.call_args_list
            self.assertEqual([c.args[0] for c in calls],['export_video','avplayer_job_get'])
            self.assertEqual(calls[0].args[1]['job_id'],calls[1].args[1]['job_id'])
    def test_unavailable_mcp_never_falls_back(self):
        with patch.object(mcp_client,'client',side_effect=ConnectionError('offline')):
            self.assertFalse(mcp_client.call_tool('get_transcript','s','m',{})['success'])
    def test_model_schema_hides_session_and_job_identity(self):
        tool={'description':'clip','inputSchema':{'properties':{'arguments':{'type':'object','properties':{'duration_ms':{'type':'integer'}}}}}}
        with patch.object(mcp_client,'client') as factory:
            factory.return_value.catalog.return_value={'export_video':tool}
            schema=mcp_client.model_tools()[0]['input_schema']
            self.assertNotIn('session_id',schema['properties'])
            self.assertIn('duration_ms',schema['properties'])
