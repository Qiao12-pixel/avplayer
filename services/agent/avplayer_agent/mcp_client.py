"""Client for the self-hosted C++ MCP JSON-RPC endpoint (not Streamable HTTP)."""
import json
import os
import threading
import time
import urllib.request
import uuid

LIGHT_TOOLS = {'get_media_context', 'get_transcript', 'save_transcript', 'set_edit_plan'}
HEAVY_TASKS = {'extract_audio', 'export_video', 'export_random_clip', 'export_plan',
               'transcode_video', 'transcribe_subtitle'}


class McpClient:
    def __init__(self):
        self.url = os.environ.get('AVPLAYER_MCP_URL', 'http://127.0.0.1:8081/jsonrpc')
        self.lock = threading.Lock()
        self.tools = None

    def request(self, method, params, notification=False):
        identifier = uuid.uuid4().hex
        body = {'jsonrpc': '2.0', 'method': method, 'params': params}
        if not notification:
            body['id'] = identifier
        request = urllib.request.Request(self.url, data=json.dumps(body, ensure_ascii=False).encode(),
                                         headers={'Content-Type': 'application/json'}, method='POST')
        with urllib.request.urlopen(request, timeout=15) as response:
            raw = response.read()
        if notification:
            return None
        value = json.loads(raw)
        if value.get('id') != identifier or value.get('jsonrpc') != '2.0':
            raise ValueError('invalid MCP response correlation')
        if 'error' in value:
            raise RuntimeError(str(value['error'].get('message', 'MCP error')))
        return value['result']

    def catalog(self):
        with self.lock:
            if self.tools is None:
                self.request('initialize', {'protocolVersion': '2024-11-05',
                    'capabilities': {}, 'clientInfo': {'name': 'avplayer-agent', 'version': '0.2.0'}})
                self.request('notifications/initialized', {}, notification=True)
                entries = self.request('tools/list', {}).get('tools', [])
                self.tools = {tool['name']: tool for tool in entries}
            return self.tools

    def call(self, tool, arguments):
        if tool not in self.catalog():
            raise ValueError('MCP tool unavailable: ' + tool)
        result = self.request('tools/call', {'name': tool, 'arguments': arguments})
        texts = [item['text'] for item in result.get('content', []) if item.get('type') == 'text']
        if not texts:
            raise ValueError('MCP tool returned no structured JSON text')
        value = json.loads(texts[0])
        if not isinstance(value, dict):
            raise ValueError('invalid MCP tool payload')
        if result.get('isError'):
            value['success'] = False
            value.setdefault('error', 'MCP tool execution failed')
        return value


_client = None
_client_lock = threading.Lock()


def client():
    global _client
    with _client_lock:
        if _client is None or _client.url != os.environ.get('AVPLAYER_MCP_URL', 'http://127.0.0.1:8081/jsonrpc'):
            _client = McpClient()
        return _client


def call_tool(tool, session_id, media_id, arguments):
    try:
        return client().call(tool, {'session_id': session_id, 'media_id': media_id, 'arguments': arguments})
    except Exception as error:
        return {'success': False, 'error': f'MCP tool call failed: {error}'}


def model_tools():
    """Only user-facing tool schemas; identity is injected by Agent, never the model."""
    entries = client().catalog()
    result = []
    for name in ('export_video', 'export_random_clip', 'export_plan', 'transcode_video'):
        if name in entries:
            tool = entries[name]
            schema = tool['inputSchema']['properties']['arguments']
            result.append({'name': name, 'description': tool['description'], 'input_schema': schema})
    return result


def run_task(task, session_id, media_id, arguments, on_state=None, cancel_requested=None):
    job_id = uuid.uuid4().hex
    try:
        timeout = int(os.environ.get('AVPLAYER_TASK_TIMEOUT', '600'))
        if not 1 <= timeout <= 1800:
            raise ValueError('task timeout must be 1..1800')
        mcp = client()
        status = mcp.call(task, {'session_id': session_id, 'media_id': media_id,
            'arguments': arguments, 'job_id': job_id, 'timeout_seconds': timeout})
        key = {'session_id': session_id, 'job_id': job_id}
        deadline = time.monotonic() + timeout + 60
        last = None
        while True:
            if status.get('state') != last:
                if on_state:
                    on_state(job_id, status.get('state', 'failed'), status.get('worker', ''))
                last = status.get('state')
            if status.get('state') == 'succeeded':
                result = status.get('result', {})
                result['job_id'] = job_id
                return result
            if not status.get('success') or status.get('state') in ('failed', 'cancelled'):
                return {'success': False, 'error': status.get('error', 'Worker failed'), 'job_id': job_id}
            if (cancel_requested and cancel_requested()) or time.monotonic() >= deadline:
                mcp.call('avplayer_job_cancel', key)
                return {'success': False, 'error': 'task cancellation requested', 'job_id': job_id}
            time.sleep(.2)
            status = mcp.call('avplayer_job_get', key)
    except Exception as error:
        return {'success': False, 'error': f'MCP Worker call failed: {error}', 'job_id': job_id}
