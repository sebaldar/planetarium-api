# reverse-proxy/

Config nginx per esporre `planetarium-api` e `planetarium-mcp` (che girano
su dedalus, dietro WireGuard) come `api.planetarium.baldoweb.it` e
`mcp.planetarium.baldoweb.it`. Questi file vanno **sulla VM proxy
pubblica** (quella con nginx + certbot in docker compose, es.
`www.baldoweb.it`), non su dedalus — dedalus resta raggiungibile solo
via WG, mai sull'interfaccia pubblica. Vedi il README del repo
principale, sezione "Deploy: nginx + WireGuard", per la topologia
completa.

## File

| File | Cosa fa |
|---|---|
| `api.planetarium.baldoweb.it.conf` | vhost 80→443 + reverse proxy verso `10.10.0.3:8484` |
| `mcp.planetarium.baldoweb.it.conf` | vhost 80→443 + reverse proxy verso `10.10.0.3:8485` (Streamable HTTP) |
| `secrets.conf.example` | template del segreto nginx→backend, **non** il file reale |

## 1. DNS

Prima di emettere il certificato, verificare che entrambi i sottodomini
puntino già all'IP della VM proxy:

```sh
dig api.planetarium.baldoweb.it +short
dig mcp.planetarium.baldoweb.it +short
```

## 2. Copiare i vhost

Copiare i due `.conf` nella cartella che il docker-compose di nginx su
quella VM monta come `conf.d` (il nome esatto dipende da quel
docker-compose, non da questo repo).

## 3. Certificato

Il comando che hai già preparato espande il certificato esistente
`www.baldoweb.it` aggiungendo i due nuovi sottodomini come SAN — è
corretto, nginx deve però già rispondere su porta 80 con i due vhost
sopra (per la sfida `/.well-known/acme-challenge/`) prima di lanciarlo:

```sh
docker compose run --rm certbot certonly --webroot \
  -w /var/www/certbot \
  --cert-name www.baldoweb.it \
  -d mcp.planetarium.baldoweb.it \
  -d api.planetarium.baldoweb.it \
  --expand
```

Il certificato risultante resta in
`/etc/letsencrypt/live/www.baldoweb.it/{fullchain.pem,privkey.pem}` —
è lo stesso path già usato dagli altri vhost su quel cert-name, i due
file `.conf` qui lo referenziano già così.

## 4. Segreto nginx→backend

**Non** generare chiavi nuove con `openssl rand -hex 32` per questo
step: `PLANETARIUM_API_KEY` e `MCP_API_KEY` sono già stati generati e
sono nel `.env` di dedalus (usato da `docker-compose.yml` per avviare
`planetarium-api`/`planetarium-mcp`) — nginx deve iniettare **quegli
stessi valori**, altrimenti il backend risponde 401 a ogni richiesta.

Sulla VM proxy, creare `/etc/nginx/secrets/planetarium.conf` (permessi
`600`, `root:root`, mai in git) copiando `secrets.conf.example` e
incollando i due valori reali presi dal `.env` di dedalus:

```nginx
set $planetarium_api_key "<valore di PLANETARIUM_API_KEY su dedalus>";
set $planetarium_mcp_key "<valore di MCP_API_KEY su dedalus>";
```

## 5. Reload

```sh
docker compose exec nginx nginx -t   # sintassi
docker compose exec nginx nginx -s reload
```

## 6. Verifica

```sh
curl https://api.planetarium.baldoweb.it/api/v1/health
curl https://mcp.planetarium.baldoweb.it/healthz
```

Entrambe non richiedono chiave (liveness check) e devono rispondere
`{"status":"ok"}`. Un client MCP reale va invece puntato su
`https://mcp.planetarium.baldoweb.it/mcp` con header `X-API-Key` uguale
al valore di `MCP_API_KEY`.
