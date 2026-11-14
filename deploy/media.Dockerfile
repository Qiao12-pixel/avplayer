# Build-oriented image: compiles the C++ media service inside the repository
# checkout. It intentionally keeps the toolchain so the image can be rebuilt
# and debugged; split this into a slimmer runtime stage when packaging for
# production.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git pkg-config ca-certificates \
        libgrpc++-dev protobuf-compiler protobuf-compiler-grpc libprotobuf-dev \
        qt6-base-dev qt6-multimedia-dev \
        libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
        libopus-dev libzookeeper-mt-dev libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY proto /src/proto
COPY libs /src/libs
COPY services/media /src/services/media

RUN cmake -S /src/services/media -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --target media_server -j"$(nproc)"

ENTRYPOINT ["/build/media_server"]
