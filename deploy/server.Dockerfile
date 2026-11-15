FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git pkg-config ca-certificates \
    libgrpc++-dev protobuf-compiler protobuf-compiler-grpc libprotobuf-dev \
    qt6-base-dev qt6-multimedia-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
    libopus-dev libcurl4-openssl-dev libssl-dev libzookeeper-mt-dev libboost-dev nlohmann-json3-dev \
    default-libmysqlclient-dev libhiredis-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY proto /src/proto
COPY libs /src/libs
COPY services/media /src/services/media
COPY services/server /src/services/server
RUN cmake -S services/server -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --target avplayer_server -j2
ENTRYPOINT ["/build/avplayer_server"]
