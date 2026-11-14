FROM debian:bookworm AS build
RUN apt-get update && apt-get install -y --no-install-recommends cmake g++ make libboost-dev nlohmann-json3-dev libgrpc++-dev protobuf-compiler-grpc libprotobuf-dev libmariadb-dev-compat libhiredis-dev libzookeeper-mt-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY proto proto
COPY services/gateway services/gateway
COPY libs/service-discovery libs/service-discovery
RUN cmake -S services/gateway -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libgrpc++1.51 libprotobuf32 libmariadb3 libhiredis0.14 libzookeeper-mt2 && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/web_gateway /usr/local/bin/web_gateway
WORKDIR /app
CMD ["web_gateway"]
