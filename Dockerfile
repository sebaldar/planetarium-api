# syntax=docker/dockerfile:1

# ---------- STAGE 1: BUILDER ----------
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
      g++ make nlohmann-json3-dev zlib1g-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY engine ./engine
COPY service ./service
COPY build.sh ./build.sh

# Compila libXML.so / libsystem.so (in /usr/lib) + il motore astronomico
# (VSOP87 + ELP82B + geometria) + il servizio HTTP, tutto in un binario
# statico verso le due .so. Vedi build.sh per il dettaglio dei passi
# (validati manualmente prima di essere automatizzati qui).
RUN ./build.sh /app /usr/lib /app/planetarium-svc

# ---------- STAGE 2: RUNTIME ----------
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
      zlib1g ca-certificates wget \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -r -u 10001 -m -d /app -s /usr/sbin/nologin planetarium

COPY --from=builder /usr/lib/libXML.so /usr/lib/libsystem.so /usr/lib/
RUN ldconfig

COPY --from=builder /app/planetarium-svc /app/planetarium-svc

USER planetarium
WORKDIR /app

ENV VSOP_DIR=/app/data/ \
    PLANETARIUM_PORT=8484

EXPOSE 8484
VOLUME ["/app/data"]

HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
  CMD wget -qO- http://127.0.0.1:8484/api/v1/health >/dev/null || exit 1

ENTRYPOINT ["/app/planetarium-svc"]
