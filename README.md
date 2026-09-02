# planetarium-api

Microservizio HTTP stateless che espone il motore astronomico C++
(VSOP87 + ELP82B) di BALDOWEB: dato un giorno giuliano (JD) e la
posizione di un osservatore sulla Terra, calcola ascensione retta,
declinazione, altezza, azimut, elongazione e distanza dei corpi
celesti principali.

## Origine del codice

Il motore in `engine/` è copiato una tantum da `BALDOWEB/cpp` (sottoinsieme:
`libPlanetarium/{VSOP,elp82b,date,timer,color}`, `geometria`, `libXML`,
`libSystem`, `Utils`, `CMat`). Non è un riferimento live: se in BALDOWEB
viene corretto un bug nel motore (VSOP87/ELP82B/geometria), va riportato
qui a mano. Il file `engine/PositionService.cpp` è invece scritto per
questo progetto, estraendo e adattando la logica di
`WSsrv::computeCelestialPositions` (BALDOWEB/cpp/libPlanetarium/WSsrv.cpp)
rimossa la dipendenza da sessione/socket e da `SkyGrid` (che richiede
MariaDB, non necessaria alle funzioni base di questa API — vedi sotto).

I dati in `data/{vsop87,elp}` sono le tavole VSOP87/ELP82B (dataset
astronomico pubblico, immutabile), vendorizzate qui per rendere il
repo autosufficiente: non serve che `BALDOWEB/` esista sulla macchina
di build/deploy.

**Nota volutamente esclusa**: nessuna dipendenza da MariaDB/MySQL. Nel
codice originale serve solo a `SkyGrid` (lookup costellazioni/stelle
vicine), non al calcolo di posizione. Se in futuro si vorrà
quell'estra, va valutato se serva davvero un RDBMS o basti un indice
in memoria.

## Struttura

```
engine/       motore di calcolo C++ (copiato da BALDOWEB/cpp) + PositionService
service/      servizio HTTP (cpp-httplib + nlohmann/json), main.cpp
data/         tavole VSOP87 + ELP82B (~47MB), montate come volume ro
mcp/          adapter MCP (Node/TS), client HTTP sottile verso il servizio
build.sh      build dell'engine + servizio, usato in locale e nel Dockerfile
```

## Build & run locali (senza Docker)

Richiede `g++`, `make`, il pacchetto `nlohmann-json3-dev` (header di sistema
`<nlohmann/json.hpp>`) e `zlib1g-dev`.

```sh
./build.sh                      # produce ./planetarium-svc, installa le .so in /usr/lib (serve sudo)
VSOP_DIR=./data/ ./planetarium-svc
```

## Build & run con Docker (consigliato)

```sh
cp .env.example .env
docker compose up -d --build
curl "http://127.0.0.1:8484/api/v1/positions?jd=2460600.5&lat=45.4642&lon=9.19&bodies=sun,moon"
```

Immagine finale: ~90MB (debian:bookworm-slim, nessuna dipendenza Node/V8/MySQL).

## API

### `GET /api/v1/health`
Liveness check, non richiede API key.

### `GET /api/v1/bodies`
Elenco dei nomi di corpo accettati.

### `GET /api/v1/positions`

| Parametro | Obbligatorio | Note |
|---|---|---|
| `jd`     | sì | giorno giuliano, es. `2460600.5` |
| `lat`    | sì | latitudine osservatore, gradi |
| `lon`    | sì | longitudine osservatore, gradi |
| `height` | no | altezza s.l.m., metri (default 0) |
| `bodies` | no | CSV tra `sun,moon,mercury,venus,mars,jupiter,saturn,uranus,neptune` (default: tutti) |

```json
{
  "jd": 2460600.5,
  "observer": { "lat": 45.4642, "lon": 9.19, "height": 0 },
  "bodies": [
    {
      "name": "moon",
      "ra_deg": 43.39, "ra_hms": "2h53m34.37s",
      "dec_deg": 21.97, "dec_dms": "21°58'18.92''",
      "altitude_deg": -21.38, "azimuth_deg": 341.61,
      "elongation_deg": -118.27,
      "distance_au": 0.00250, "distance_km": 373777.5,
      "visible": false
    }
  ]
}
```

### `GET /api/v1/sun/events`

Prossima alba/tramonto a partire da `jd` (fino a 2 giorni di ricerca in
avanti). Scansione grossolana + bisezione su
`Earth::sunAngleOverOrizont(t,lat,lon) + 0.83` (rifrazione/semidiametro,
stessa costante usata dal motore originale) — non usa `Earth::sunRise`
direttamente, che nel codice originale cerca solo entro ±0.01 giorni dal
seed e non è quindi adatta a una ricerca da un punto qualsiasi.

| Parametro | Obbligatorio |
|---|---|
| `jd`  | sì |
| `lat` | sì |
| `lon` | sì |

```json
{
  "jd": 2461286.0,
  "observer": { "lat": 45.4642, "lon": 9.19 },
  "sunrise": { "found": true, "jd": 2461286.6996412035 },
  "sunset":  { "found": true, "jd": 2461286.2497916670 }
}
```

### `GET /api/v1/sun/seasons`

Equinozi e solstizi per un anno solare. Wrapper diretto di
`Earth::{march,june,september,december}{Equinox,Solstice}` (root-finding
via CMat, già nel motore).

| Parametro | Obbligatorio |
|---|---|
| `year` | sì |

```json
{
  "year": 2026,
  "march_equinox_jd": 2461120.1160331178,
  "june_solstice_jd": 2461212.8511357964,
  "september_equinox_jd": 2461306.5044089514,
  "december_solstice_jd": 2461396.369008027
}
```

### `GET /api/v1/moon/phase`

Fase lunare corrente e prossima occorrenza delle 4 fasi principali.

⚠️ **Non usa `Moon::angleME`/`Moon::phases` del motore originale**: verificato
empiricamente che `angleME` non è monotona (usa `fabs()` su una differenza
di longitudini normalizzate indipendentemente, che la fa "ripiegare"
invece di progredire 0→360 nel ciclo sinodico), e di conseguenza anche il
root-finding di `Moon::phases` converge in modo inaffidabile (un seed nel
futuro può restituire un risultato nel passato — riprodotto durante il
test). La fase qui è ricavata dalle longitudini eclittiche di Sole e Luna
(`sun.orbitLBR`/`moon.orbitAlfaELP`, le stesse già usate e validate in
`/positions`): `fase = (L_luna - L_sole) mod 360`, verificata monotona su
un mese sinodico campionato punto per punto. Le prossime occorrenze sono
trovate con lo stesso schema scan+bisezione di `/sun/events`.

| Parametro | Obbligatorio |
|---|---|
| `jd` | sì |

`angle_me_deg`: 0°=nuova, 90°=primo quarto, 180°=piena, 270°=ultimo quarto.

```json
{
  "jd": 2461286.0,
  "angle_me_deg": 246.057,
  "illumination": 0.7029,
  "phase_name": "gibbosa calante",
  "next_last_quarter_jd": 2461287.828,
  "next_new_moon_jd": 2461294.645,
  "next_first_quarter_jd": 2461302.365,
  "next_full_moon_jd": 2461310.202
}
```

Nota: i quattro campi `next_*` sono indipendenti (ciascuno è "la prossima
occorrenza di quella fase specifica da `jd`"), quindi il loro ordine
cronologico dipende da dove ci si trova nel ciclo al momento della
richiesta — non sono necessariamente in sequenza nuova→primo quarto→
piena→ultimo quarto nel JSON.

Se impostata la variabile `PLANETARIUM_API_KEY`, tutte le chiamate (tranne
`/api/v1/health`) richiedono l'header `X-API-Key` — vedi sotto per come è
pensata la catena di fiducia.

## Deploy: nginx + WireGuard

Topologia: `planetarium-api` gira su un host interno (qui: dedalus,
`10.10.0.3` su `wg0`), raggiungibile **solo in HTTP in chiaro dentro il
tunnel WireGuard**, mai sull'interfaccia pubblica. TLS/certificati e
l'esposizione verso internet sono responsabilità di nginx su un'altra VPS,
che fa da reverse proxy verso dedalus attraverso il tunnel. Questo è
corretto perché WireGuard fornisce già cifratura e autenticazione a
livello di trasporto: aggiungere TLS anche sul tratto interno sarebbe
ridondante.

Il container va bindato **solo sull'IP dell'interfaccia WireGuard**, mai su
`0.0.0.0` (già configurato così in `docker-compose.yml`/`.env.example`):

```yaml
ports:
  - "10.10.0.3:8484:8484"
```

### `PLANETARIUM_API_KEY`: segreto interno nginx↔backend

```
client esterno ──(auth/TLS a discrezione di nginx)──> nginx (VPS pubblica)
                                                           │
                                              inietta X-API-Key FISSA
                                                           ▼
                                          dedalus:8484 (via WG, solo HTTP)
```

Non è un token che il client esterno deve conoscere: è nginx a iniettarlo,
sempre uguale, su ogni richiesta proxata verso dedalus — indipendentemente
da cosa manda il chiamante. Serve come seconda barriera nel caso la rete
WG sia condivisa con altri peer non fidati (con un tunnel punto-punto
dedicato l'handshake WG è già di per sé un controllo d'accesso sufficiente,
ma costa comunque poco tenerla attiva).

Generare la chiave con `openssl rand -hex 32`, metterla nel `.env` **sia**
di dedalus (`PLANETARIUM_API_KEY=...`, mai in git) **sia** nella config
nginx (stesso valore):

```nginx
# /etc/nginx/secrets/planetarium.conf — permessi 600, root:root, MAI nel vhost pubblico
set $planetarium_api_key "il-valore-generato-con-openssl";

server {
    location /planetarium/ {
        include /etc/nginx/secrets/planetarium.conf;
        proxy_pass http://10.10.0.3:8484/;
        proxy_set_header X-API-Key $planetarium_api_key;   # fissa, non dal client
        limit_req zone=planetarium_rl burst=20 nodelay;
    }
}
```

## MCP

`mcp/` è un adapter sottile (Node/TypeScript, `@modelcontextprotocol/sdk`):
traduce tool-call MCP in chiamate HTTP contro questo servizio. Non
reimplementa alcun calcolo.

```sh
cd mcp
npm install
npm run build
PLANETARIUM_API_URL=http://127.0.0.1:8484 npm start
```

Tool esposti: `get_celestial_positions`, `list_supported_bodies`.

## Estensioni future (fuori scope v1)

- Costellazione/zodiaco/stelle vicine (richiede `SkyGrid` + catalogo
  `stars/`, ~75MB — oggi non incluso in questo repo)
- Pasqua (`Earth::Easter`, già presente nel motore, non ancora esposta)
- Conversione data calendario ↔ JD lato server (oggi il client deve
  fornire `jd` già calcolato)
