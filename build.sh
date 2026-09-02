#!/usr/bin/env bash
# Build dell'engine astronomico + del servizio HTTP.
# Verificato manualmente riga per riga (compilazione + link + smoke test
# via curl) prima di essere messo in Docker: vedi README.md.
#
# Uso:
#   ./build.sh [PROJECT_ROOT] [OUT_LIB] [OUT_BIN]
#
# Di default compila dentro l'albero corrente e installa le .so in /usr/lib
# (pensato per essere eseguito dentro lo stage builder del Dockerfile).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${1:-$ROOT_DIR}"
OUT_LIB="${2:-/usr/lib}"
OUT_BIN="${3:-$ROOT_DIR/planetarium-svc}"

ENGINE="$PROJECT_ROOT/engine"
SERVICE="$PROJECT_ROOT/service"
OBJ_DIR="$(mktemp -d)"

echo ">> libXML"
make -C "$ENGINE/cpp/libXML"    lib PROJECT_ROOT="$ENGINE" OUT_LIB="$OUT_LIB"

echo ">> libSystem"
make -C "$ENGINE/cpp/libSystem" lib PROJECT_ROOT="$ENGINE" OUT_LIB="$OUT_LIB"

ldconfig "$OUT_LIB" 2>/dev/null || true

INCLUDES="-I$ENGINE \
  -I$ENGINE/cpp/libPlanetarium/VSOP/include \
  -I$ENGINE/cpp/libPlanetarium/timer/include \
  -I$ENGINE/cpp/libPlanetarium/date/include \
  -I$ENGINE/cpp/libPlanetarium/color \
  -I$ENGINE/cpp/libPlanetarium/elp82b \
  -I$ENGINE/cpp/geometria/include \
  -I$ENGINE/cpp/CMat \
  -I$ENGINE/cpp/CMat/interprete/include \
  -I$ENGINE/cpp/libSystem \
  -I$ENGINE/cpp/Utils \
  -I$ENGINE/cpp/libXML/include \
  -I$SERVICE"

CXXFLAGS="-fPIC -std=c++17 -O2 -Wall -Wextra"

echo ">> compilazione motore astronomico (VSOP87 + ELP82B + geometria)"
SOURCES=(
  "$ENGINE/cpp/Utils/Utils.cpp"
  "$ENGINE/cpp/libPlanetarium/elp82b/calc_interpolated_elements.cpp"
  "$ENGINE/cpp/libPlanetarium/elp82b/elp82b.cpp"
  "$ENGINE/cpp/libPlanetarium/color/color.cpp"
  "$ENGINE/cpp/libPlanetarium/timer/source/timer.cpp"
  "$ENGINE/cpp/libPlanetarium/date/source/date.cpp"
  "$ENGINE/cpp/libPlanetarium/VSOP/source/vsop.cpp"
  "$ENGINE/cpp/libPlanetarium/VSOP/source/elp.cpp"
  "$ENGINE/cpp/libPlanetarium/VSOP/source/orbit.cpp"
  "$ENGINE/cpp/libPlanetarium/VSOP/source/celestial_body.cpp"
  "$ENGINE/cpp/libPlanetarium/VSOP/source/moon_jupiter.cpp"
  "$ENGINE/cpp/libPlanetarium/VSOP/source/solarSystem.cpp"
  "$ENGINE/cpp/geometria/source/angolo.cpp"
  "$ENGINE/cpp/geometria/source/line.cpp"
  "$ENGINE/cpp/geometria/source/point.cpp"
  "$ENGINE/cpp/geometria/source/plane.cpp"
  "$ENGINE/cpp/geometria/source/horizont.cpp"
  "$ENGINE/PositionService.cpp"
)

OBJS=()
for src in "${SOURCES[@]}"; do
  obj="$OBJ_DIR/$(basename "${src%.cpp}").o"
  g++ $CXXFLAGS $INCLUDES -c "$src" -o "$obj"
  OBJS+=("$obj")
done

echo ">> compilazione servizio HTTP"
g++ -std=c++17 -O2 -Wall -Wextra $INCLUDES -c "$SERVICE/main.cpp" -o "$OBJ_DIR/main.o"
OBJS+=("$OBJ_DIR/main.o")

echo ">> link"
g++ -std=c++17 "${OBJS[@]}" -L"$OUT_LIB" -lXML -lsystem -lz -pthread -o "$OUT_BIN"

rm -rf "$OBJ_DIR"
echo ">> OK: binario generato in $OUT_BIN"
