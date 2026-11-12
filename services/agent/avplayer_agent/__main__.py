"""Console entry point for the gRPC agent service."""

from __future__ import annotations

import sys

from .server import serve
from .settings import agent_bind, agent_port


def main() -> int:
    port = agent_port()
    server = serve(port=port, bind=agent_bind())
    print(f"avplayer agent gRPC server listening on {agent_bind()}:{port}", file=sys.stderr)
    try:
        server.wait_for_termination()
    except KeyboardInterrupt:
        server.stop(0)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
