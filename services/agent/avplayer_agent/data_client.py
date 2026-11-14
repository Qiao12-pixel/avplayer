"""Internal gRPC client. Storage drivers and implementations live in C++."""
import json
import os
import grpc
from . import agent_pb2, agent_pb2_grpc

def execute(action: str, **values):
    try:
        with grpc.insecure_channel(os.environ.get('AVPLAYER_DATA_SERVER', 'localhost:50066')) as channel:
            result = agent_pb2_grpc.StorageServiceStub(channel).Execute(
                agent_pb2.Struct(json=json.dumps({'action': action, **values}, ensure_ascii=False)), timeout=3)
            return json.loads(result.json or 'null')
    except (grpc.RpcError, ValueError):
        return None
