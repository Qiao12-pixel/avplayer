"""Result storage RPC only; Redis/MySQL access stays in the C++ backend."""
from .data_client import execute


def get_result(session_id, media_id, task, variant=''):
    result = execute('result_get', session_id=session_id, media_id=media_id, task=task, variant=variant)
    return result if isinstance(result, dict) and result.get('success') else None


def put_result(session_id, media_id, task, payload, variant=''):
    return execute('result_put', session_id=session_id, media_id=media_id,
                   task=task, variant=variant, payload=payload)
