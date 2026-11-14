FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1

RUN apt-get update \
    && apt-get install -y --no-install-recommends libgomp1 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY scripts/gen_proto.sh /app/scripts/gen_proto.sh
COPY proto /app/proto
COPY services/agent /app/services/agent

# Generate the Python stubs inside the package before installing it, so the
# wheel contains the exact proto contract from this repository revision.
RUN pip install grpcio-tools \
    && bash /app/scripts/gen_proto.sh \
    && pip install /app/services/agent \
    && find /app -name __pycache__ -type d -prune -exec rm -rf {} +

CMD ["avplayer-agent"]
