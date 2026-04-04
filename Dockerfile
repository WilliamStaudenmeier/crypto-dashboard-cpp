FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
  ca-certificates \
  cmake \
  g++ \
  make \
  openssl \
  libssl-dev \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build --config Release -j

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
  ca-certificates \
  openssl \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/build/crypto_dashboard_cpp /app/crypto_dashboard_cpp
COPY --from=build /app/static /app/static

ENV PORT=8080
EXPOSE 8080

CMD ["/app/crypto_dashboard_cpp"]
