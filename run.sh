#!/usr/bin/env bash
# Launch Uniracers from the repository root.
#
# The host anchors config.ini, keybinds.ini and saves/ beside the executable,
# so this script changes to game/build-local/ before exec'ing the game. Extra
# args are passed through, e.g.:
#   ./run.sh --script "$PWD/tools/scripts/ws-native-map-spike/route-dragster-move.txt"
#   SNESRECOMP_WS_MINIMAP=0 ./run.sh
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROM="$ROOT/rom/Uniracers (USA).sfc"
BUILD="$ROOT/game/build-local"
BIN="$BUILD/UniracersSNESRecomp"

if [[ ! -f "$ROM" ]]; then
    echo "ROM not found: $ROM" >&2
    echo "Place your legally owned Uniracers (USA).sfc in rom/." >&2
    exit 1
fi
if [[ ! -x "$BIN" ]]; then
    echo "Game binary not found or not executable: $BIN" >&2
    echo "Build it with:" >&2
    echo "  cd \"$ROOT/game\"" >&2
    echo "  bash tools/regen.sh --rom \"$ROM\"" >&2
    echo "  cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release" >&2
    echo "  cmake --build build-local --parallel" >&2
    exit 1
fi

cd -- "$BUILD"
exec "$BIN" --rom "$ROM" --no-launcher "$@"
