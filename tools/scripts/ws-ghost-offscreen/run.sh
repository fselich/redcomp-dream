#!/usr/bin/env bash
# usage: run.sh tag route first last
P="<workshop>"
B="$P/work/ws-native-map-spike/project/UniracersSNESRecomp/build-local"
H="$(cd "$(dirname "$0")" && pwd)"
tag=$1; route=$2; first=$3; last=$4; port=${PORT:-9971}
cd "$B"; rm -f saves/save.srm saves/save.srm.bak
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_NO_LAUNCHER=1 \
SNESRECOMP_RUN_FRAMES=$((last+300)) SNESRECOMP_WS=1 SNESRECOMP_DEBUG_PORT=$port \
./UniracersSNESRecomp --rom "$P/Uniracers (USA).sfc" --no-launcher --script "$route" > "$H/run-$tag.log" 2>&1 &
pid=$!; sleep 2
python3 -I "$H/rec.py" $port $first $last "$H/$tag.json" > "$H/rec-$tag.log" 2>&1
kill $pid 2>/dev/null; wait $pid 2>/dev/null
tail -3 "$H/rec-$tag.log"
