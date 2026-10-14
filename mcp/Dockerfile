# syntax=docker/dockerfile:1
#
# Immagine dell'adapter MCP (Streamable HTTP). Non contiene il motore
# astronomico: è un client HTTP sottile verso planetarium-api (vedi
# ../Dockerfile e ../README.md). Context di build: questa cartella (mcp/).

# ---------- STAGE 1: BUILDER ----------
FROM node:22-bookworm-slim AS builder

WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci

COPY tsconfig.json ./
COPY src ./src
RUN npm run build

# ---------- STAGE 2: RUNTIME ----------
FROM node:22-bookworm-slim

RUN useradd -r -u 10002 -m -d /app -s /usr/sbin/nologin planetarium-mcp

WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --omit=dev && npm cache clean --force

COPY --from=builder /app/dist ./dist

USER planetarium-mcp

# MCP_BIND_ADDR va sovrascritto a 0.0.0.0 dall'ambiente (default nel codice
# è 127.0.0.1, pensato per l'uso locale senza container): il controllo
# d'accesso qui è demandato al publish della porta in docker-compose.yml
# (bind solo sull'IP WireGuard dell'host), esattamente come planetarium-api.
ENV MCP_TRANSPORT=http \
    MCP_HTTP_PORT=8485 \
    MCP_BIND_ADDR=0.0.0.0

EXPOSE 8485

# node:22-bookworm-slim non include wget (a differenza della runtime image
# del servizio principale, dove viene installato apposta): si usa il fetch
# globale di Node per non aggiungere un layer apt solo per l'healthcheck.
HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
  CMD node -e "fetch('http://127.0.0.1:8485/healthz').then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"

ENTRYPOINT ["node", "dist/index.js"]
