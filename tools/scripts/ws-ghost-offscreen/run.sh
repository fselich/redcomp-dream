#!/usr/bin/env bash
# Record rival/player OAM and camera series through the debug server.
# usage: run.sh tag route first last
# Runs a copy of the executable in a scratch directory with an empty saves/,
# so the player's save in game/build-local is never touched.
H="$(cd "$(dirname "$0")" && pwd)"
P="$(cd "$H/../../.." && pwd)"
B="${UNIRACERS_BUILD:-$P/game/build-local}"
tag=$1; route=$2; first=$3; last=$4; port=${PORT:-9971}
W=$(mktemp -d "${TMPDIR:-/tmp}/uniracers-rec.XXXXXX") || exit 1
mkdir -p "$W/saves"; cp "$B/UniracersSNESRecomp" "$W/"; cd "$W"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_NO_LAUNCHER=1 \
SNESRECOMP_RUN_FRAMES=$((last+300)) SNESRECOMP_WS=1 SNESRECOMP_DEBUG_PORT=$port \
./UniracersSNESRecomp --rom "$P/rom/Uniracers (USA).sfc" --no-launcher --script "$route" > "$H/run-$tag.log" 2>&1 &
pid=$!; sleep 2
python3 -I "$H/rec.py" $port $first $last "$H/$tag.json" > "$H/rec-$tag.log" 2>&1
kill $pid 2>/dev/null; wait $pid 2>/dev/null
tail -3 "$H/rec-$tag.log"
