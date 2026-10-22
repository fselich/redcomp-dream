#!/usr/bin/env bash
# Validate SNESRECOMP_WS_OFFWINDOW=<mode> on the six leading-edge captures
# (uniracers-ws-source task 5.1). Writes logs/{src,ow-src,run-src}-<tag>-* and
# appends the scores to logs/validation-<mode>.txt.
# usage: validate_all.sh [mode=source]
set -u
MODE=${1:-source}
P="<workshop>"; S="$P/work/ws-source"
B="$P/work/ws-native-map-spike/project/UniracersSNESRecomp/build-local"
S1="$P/work/ws-native-map-spike/scripts"; S2="$P/work/ws-leading-edge/scripts"
OUT="$S/logs/validation-$MODE.txt"; : > "$OUT"
cd "$B"
for spec in "dragster|$S1/route-dragster-move.txt|2300|2500" \
            "dragster-left|$S2/route-dragster-left.txt|3300|3600" \
            "zoomzoo|$S1/route-zoomzoo-move.txt|3300|3505" \
            "bowl|$S2/route-bowl-move.txt|2000|2300" \
            "switcher|$S2/route-switcher-move.txt|2000|2300" \
            "monster|$S2/route-monster-move.txt|2000|2300"; do
  IFS='|' read -r t r a b <<< "$spec"; tag="$MODE-$t"
  rm -f saves/save.srm saves/save.srm.bak
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_NO_LAUNCHER=1 \
  SNESRECOMP_RUN_FRAMES=$b SNESRECOMP_WS=1 SNESRECOMP_WS_OFFWINDOW=$MODE \
  SNESRECOMP_WS_SOURCE_LOG="$S/logs/src-$tag.log" \
  SNESRECOMP_WS_OFFWINDOW_LOG="$S/logs/ow-$tag-mask.csv" \
  SNESRECOMP_BGPROBE_FRAME=$a,$b SNESRECOMP_BGPROBE_FILE="$S/logs/ow-$tag-$a-$b.csv" \
  SNESRECOMP_SCREENSHOT="$S/frames/$tag-$b.ppm" SNESRECOMP_SCREENSHOT_FRAME=$b \
  ./UniracersSNESRecomp --rom "$P/Uniracers (USA).sfc" --no-launcher --script "$r" \
    > "$S/logs/run-$tag.log" 2>&1
  {
    echo "== $t ($MODE, frames $a-$b) $(grep -m1 'anchor ok' "$S/logs/src-$tag.log" 2>/dev/null | cut -d' ' -f1-5,10-) drops=$(grep -c dropped "$S/logs/src-$tag.log" 2>/dev/null) cpu=$(grep -o 'cpu_seconds=[0-9.]*' "$S/logs/run-$tag.log")"
    (cd "$P/work" && python3 -I ws-offwindow/scripts/world_key_check.py "$S/logs/ow-$tag-$a-$b.csv" \
       --mask-log "$S/logs/ow-$tag-mask.csv" --served | tail -2)
  } | tee -a "$OUT"
  rm -f "$S/logs/ow-$tag-$a-$b.csv"   # probe CSVs are large; scores are kept
done
