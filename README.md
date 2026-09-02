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

Se impostata la variabile `PLANETARIUM_API_KEY`, tutte le chiamate (tranne
`/api/v1/health`) richiedono l'header `X-API-Key`.

## Deploy: nginx + WireGuard

Il container va bindato **solo sull'IP dell'interfaccia WireGuard**, mai su
`0.0.0.0`:

```yaml
ports:
  - "10.66.0.2:8484:8484"
```

Sul VPS con nginx, reverse proxy verso quell'IP via tunnel WG, con rate
limiting e inoltro dell'header `X-API-Key`.

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
- Alba/tramonto, equinozi/solstizi, fase lunare, Pasqua (`Earth::sunRise`,
  `Earth::equinox*`, `Moon::phases`, `Earth::Easter` — già presenti nel
  motore, non ancora esposti come endpoint)
- Conversione data calendario ↔ JD lato server (oggi il client deve
  fornire `jd` già calcolato)
