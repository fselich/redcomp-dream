#!/usr/bin/env bash
# Output gate for the Uniracers port.
#
# Runs a COPY of the executable in a scratch directory with an empty saves/
# and a fixed generated config.ini. It never reads or writes
# game/build-local/saves/ or the user's config.ini; a fingerprint guard checks
# that both are untouched at the end.
#
# Usage:
#   game/tools/validate.sh [--record] [--build DIR] [--case TAG]
#
#   --record     run every case twice and write game/tools/validate_refs.txt
#   --build DIR  directory containing UniracersSNESRecomp (default:
#                game/build-local) or the executable itself
#   --case TAG   run only one case (repeatable)
#
# Frame hashes are SHA-256 of the PPM written by SNESRECOMP_SCREENSHOT. Logs
# are hashed with SHA-256. A case whose frame differs between the two record
# runs is stored as NONDET; default runs then compare the BG1 probe score
# instead (the same score tools/scripts/ws-offwindow/world_key_check.py
# prints).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
GAME="$ROOT/game"
REF="${VALIDATE_REFS:-$GAME/tools/validate_refs.txt}"
ROM="$ROOT/rom/Uniracers (USA).sfc"
BUILD="$GAME/build-local"
RECORD=0
ONLY=()

while [ $# -gt 0 ]; do
  case "$1" in
    --record) RECORD=1; shift ;;
    --build) BUILD=$2; shift 2 ;;
    --case) ONLY+=("$2"); shift 2 ;;
    -h|--help) sed -n '2,/^set /p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "validate.sh: unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [ -d "$BUILD" ]; then
  EXE="$BUILD/UniracersSNESRecomp"
else
  EXE="$BUILD"
fi
if [ ! -x "$EXE" ]; then
  echo "validate.sh: executable not found: $EXE" >&2
  exit 2
fi
if [ ! -f "$ROM" ]; then
  echo "validate.sh: ROM not found: $ROM" >&2
  exit 2
fi

# tag|route|screenshot frame|extra env
CASES=(
  "native|game/tools/route-validate-dragster.txt|2800|SNESRECOMP_WS=0"
  "16x9|game/tools/route-validate-dragster.txt|2800|SNESRECOMP_WS=1 SNESRECOMP_WS_ASPECT=16:9"
  "21x9|game/tools/route-validate-dragster.txt|2800|SNESRECOMP_WS=1 SNESRECOMP_WS_ASPECT=21:9"
  "512|game/tools/route-validate-dragster.txt|2800|SNESRECOMP_WS=1 SNESRECOMP_WS_ASPECT=32:9"
  "clamp|game/tools/route-validate-dragster.txt|2800|SNESRECOMP_WS=1 SNESRECOMP_WS_ASPECT=16:9 SNESRECOMP_WS_OFFWINDOW=clamp"
  # Values of removed policies are mapped: wrap -> clamp, ring -> source.
  # Their references are the clamp and 16x9 frames.
  "map-wrap|game/tools/route-validate-dragster.txt|2800|SNESRECOMP_WS=1 SNESRECOMP_WS_ASPECT=16:9 SNESRECOMP_WS_OFFWINDOW=wrap"
  "map-ring|game/tools/route-validate-dragster.txt|2800|SNESRECOMP_WS=1 SNESRECOMP_WS_ASPECT=16:9 SNESRECOMP_WS_OFFWINDOW=ring"
  "monster|tools/scripts/ws-leading-edge/route-monster-move.txt|2300|SNESRECOMP_WS=1 SNESRECOMP_WS_ASPECT=16:9"
  "zoomzoo512|tools/scripts/ws-native-map-spike/route-zoomzoo-move.txt|3505|SNESRECOMP_WS=1 SNESRECOMP_WS_ASPECT=32:9"
  "logs|game/tools/route-validate-dragster.txt|2800|SNESRECOMP_WS=1 SNESRECOMP_WS_ASPECT=16:9 SNESRECOMP_WS_OFFWINDOW=source"
)

want_case() {
  [ ${#ONLY[@]} -eq 0 ] && return 0
  for t in "${ONLY[@]}"; do [ "$t" = "$1" ] && return 0; done
  return 1
}

SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/uniracers-validate.XXXXXX") || exit 1
if [ -z "${VALIDATE_KEEP:-}" ]; then
  trap 'rm -rf "$SCRATCH"' EXIT
else
  echo "validate.sh: keeping $SCRATCH" >&2
fi

# Metadata only: validate.sh must not read the player's save or config.
user_fingerprint() {
  for f in "$GAME/build-local/config.ini" "$GAME/build-local/saves/save.srm"; do
    if [ -e "$f" ]; then
      stat -c '%n %Y %s' "$f"
    else
      echo "absent $f"
    fi
  done
}
USER_BEFORE=$(user_fingerprint)

write_config() {
  cat > "$1/config.ini" <<EOF
[General]
Autosave = 0
DisableFrameDelay = ${VALIDATE_FRAME_DELAY:-1}
SkipLauncher = 0
[Graphics]
WindowSize = Auto
Fullscreen = 0
NewRenderer = 1
NoSpriteLimits = 1
DisplayAspect = 4:3
OutputMethod = SDL
VSync = 0
Widescreen = 0
FrameBlend = 0
Renderer = auto
[Sound]
EnableAudio = 0
[Video]
MiniMap = 1
EOF
}

hash_file() {
  [ -f "$1" ] || { echo MISSING; return; }
  sha256sum "$1" | cut -d' ' -f1
}

score_bg1() {
  local probe=$1 mask=$2 out=$3
  [ -f "$probe" ] || { echo MISSING; return; }
  python3 -I "$ROOT/tools/scripts/ws-offwindow/world_key_check.py" \
      "$probe" ${mask:+--mask-log "$mask"} --served > "$out" 2>/dev/null || true
  if [ ! -s "$out" ]; then
    echo MISSING
    return
  fi
  # Counts of cells/frames vary with the (pre-existing) menu timing. The
  # correctness signal is the wrong-cell count, which must stay zero.
  local conf ew ww
  conf=$(sed -n 's/.*conflicting_cells=\([0-9][0-9]*\).*/\1/p' "$out" | head -1)
  ew=$(sed -n 's/^margin E:.*wrong=\([0-9][0-9]*\).*/\1/p' "$out")
  ww=$(sed -n 's/^margin W:.*wrong=\([0-9][0-9]*\).*/\1/p' "$out")
  printf 'conflicting=%s E_wrong=%s W_wrong=%s\n' "${conf:-0}" "${ew:-0}" "${ww:-0}"     | sha256sum | cut -d' ' -f1
}

# run_one TAG ROUTE FRAME ENVSPEC DIR
run_one() {
  local tag=$1 route=$2 frame=$3 envspec=$4 dir=$5
  local -a envargs=()
  for kv in $envspec; do envargs+=("$kv"); done

  rm -rf "$dir"
  mkdir -p "$dir/saves"
  cp "$EXE" "$dir/UniracersSNESRecomp"
  write_config "$dir"

  local frame_ppm="$dir/frame.ppm"
  local -a extra=()
  if [ "$tag" = "logs" ]; then
    extra+=(
      "SNESRECOMP_WS_SOURCE_LOG=$dir/source.log"
      "SNESRECOMP_WS_OFFWINDOW_LOG=$dir/offwindow.log"
      "SNESRECOMP_BGPROBE_FRAME=$((frame - 200)),$frame"
      "SNESRECOMP_BGPROBE_FILE=$dir/bgprobe.csv"
    )
    [ -n "${VALIDATE_SKIP_PARK:-}" ] || extra+=("SNESRECOMP_WS_PARK_LOG=$dir/park.log")
    [ -n "${VALIDATE_SKIP_RADAR:-}" ] || extra+=("SNESRECOMP_WS_RADAR_LOG=$dir/radar.log")
  fi

  ( cd "$dir" && env -i \
      PATH="$PATH" HOME="${HOME:-/tmp}" \
      SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      SNESRECOMP_NO_LAUNCHER=1 \
      SNESRECOMP_RUN_FRAMES=$((frame + 150)) \
      SNESRECOMP_SCREENSHOT="$frame_ppm" \
      SNESRECOMP_SCREENSHOT_FRAME=$frame \
      "${envargs[@]}" "${extra[@]}" \
      ./UniracersSNESRecomp --rom "$ROM" --no-launcher \
        --script "$ROOT/$route" > "$dir/run.log" 2>&1 )
  echo $? > "$dir/exit"
}

# artifacts DIR -> "name hash" lines
artifacts() {
  local dir=$1
  echo "frame $(hash_file "$dir/frame.ppm")"
  if [ -f "$dir/source.log" ] || [ -f "$dir/offwindow.log" ] || \
     [ -f "$dir/park.log" ] || [ -f "$dir/radar.log" ] || \
     [ -f "$dir/bgprobe.csv" ]; then
    # search_ms is wall-clock noise; every other source-log field is a
    # frame/state value and stays byte-exact.
    if [ -f "$dir/source.log" ]; then
      sed -E 's/search_ms=[0-9.]+/search_ms=X/g' \
          "$dir/source.log" > "$dir/source.norm"
    fi
    echo "log:source $(hash_file "$dir/source.norm")"
    echo "log:offwindow $(hash_file "$dir/offwindow.log")"
    echo "log:park $(hash_file "$dir/park.log")"
    echo "log:radar $(hash_file "$dir/radar.log")"
    echo "log:bgprobe $(hash_file "$dir/bgprobe.csv")"
    echo "score $(score_bg1 "$dir/bgprobe.csv" "$dir/offwindow.log" "$dir/score.txt")"
  fi
}

case_field() { echo "$1" | cut -d'|' -f"$2"; }

fail=0
: > "$SCRATCH/new_refs.txt"
printf '# generated by game/tools/validate.sh --record\n' >> "$SCRATCH/new_refs.txt"
printf '# head: %s%s\n' "$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo no-git)" \
  "$(git -C "$ROOT" status --porcelain 2>/dev/null | grep -q . && echo ' (dirty)' || true)" \
  >> "$SCRATCH/new_refs.txt"
printf '# note: map-wrap and map-ring are checked against the clamp and 16x9\n' >> "$SCRATCH/new_refs.txt"
printf '#       frames; after --record they must equal them.\n' >> "$SCRATCH/new_refs.txt"

for spec in "${CASES[@]}"; do
  tag=$(case_field "$spec" 1)
  want_case "$tag" || continue
  route=$(case_field "$spec" 2)
  frame=$(case_field "$spec" 3)
  envspec=$(case_field "$spec" 4)

  if [ "$RECORD" -eq 1 ]; then
    echo "record $tag ..." >&2
    run_one "$tag" "$route" "$frame" "$envspec" "$SCRATCH/$tag-r1"
    run_one "$tag" "$route" "$frame" "$envspec" "$SCRATCH/$tag-r2"
    artifacts "$SCRATCH/$tag-r1" > "$SCRATCH/$tag-a1.txt"
    artifacts "$SCRATCH/$tag-r2" > "$SCRATCH/$tag-a2.txt"
    # A frame that differs between runs falls back to the BG1 score.
    if ! diff -q <(grep '^frame ' "$SCRATCH/$tag-a1.txt") \
                 <(grep '^frame ' "$SCRATCH/$tag-a2.txt") >/dev/null; then
      sed -i 's/^frame .*/frame NONDET/' "$SCRATCH/$tag-a1.txt"
    fi
    # Any log/score that is unstable is recorded as NONDET (not a failure).
    while IFS=' ' read -r name _; do
      [ "$name" = frame ] && continue
      h1=$(awk -v n="$name" '$1==n {print $2}' "$SCRATCH/$tag-a1.txt")
      h2=$(awk -v n="$name" '$1==n {print $2}' "$SCRATCH/$tag-a2.txt")
      if [ "$h1" != "$h2" ]; then
        sed -i "s|^$name .*|$name NONDET|" "$SCRATCH/$tag-a1.txt"
      fi
    done < "$SCRATCH/$tag-a1.txt"
    while read -r name hash; do
      printf '%s %s %s\n' "$tag" "$name" "$hash" >> "$SCRATCH/new_refs.txt"
    done < "$SCRATCH/$tag-a1.txt"
    continue
  fi

  # Default: compare against the stored references.
  if [ ! -f "$REF" ]; then
    echo "validate.sh: references missing; run --record first" >&2
    exit 2
  fi
  printf 'case %-10s ' "$tag"
  run_one "$tag" "$route" "$frame" "$envspec" "$SCRATCH/$tag"
  artifacts "$SCRATCH/$tag" > "$SCRATCH/$tag-now.txt"
  case_fail=0
  missing=0
  skipped=0
  while read -r name hash; do
    if [ "$hash" = "MISSING" ]; then
      case_fail=1
      echo >&2
      echo "  $name: artifact missing" >&2
      continue
    fi
    ref=$(awk -v c="$tag" -v n="$name" '$1==c && $2==n {print $3}' "$REF")
    if [ -z "$ref" ]; then
      missing=1
      continue
    fi
    if [ "$ref" = "NONDET" ]; then
      if [ "$name" = "frame" ]; then
        # Compare the score instead when the frame is non-deterministic.
        ref_score=$(awk -v c="$tag" '$1==c && $2=="score" {print $3}' "$REF")
        now_score=$(awk '$1=="score" {print $2}' "$SCRATCH/$tag-now.txt")
        if [ -n "$ref_score" ] && [ "$ref_score" != "NONDET" ] && \
           [ "$ref_score" != "$now_score" ]; then
          case_fail=1
        else
          skipped=1
        fi
      else
        skipped=1
      fi
      continue
    fi
    if [ "$ref" != "$hash" ]; then
      case_fail=1
      echo >&2
      echo "  $name: expected $ref" >&2
      echo "  $name: got      $hash" >&2
    fi
  done < "$SCRATCH/$tag-now.txt"
  # References must all be present in this run too (a log that vanished is a
  # regression, not an absent comparison).
  while read -r c n h; do
    [ "$c" = "$tag" ] || continue
    if ! awk -v n="$n" '$1==n {found=1} END{exit found?0:1}' "$SCRATCH/$tag-now.txt"; then
      case_fail=1
      echo >&2
      echo "  $n: reference exists but artifact is missing now" >&2
    fi
  done < "$REF"
  if [ "$missing" -eq 1 ]; then
    case_fail=1
    echo >&2
    echo "  reference entry missing for one or more artifacts" >&2
  fi
  if [ "$(cat "$SCRATCH/$tag/exit")" != "0" ]; then
    case_fail=1
    echo >&2
    echo "  game exited $(cat "$SCRATCH/$tag/exit"); see $SCRATCH/$tag/run.log" >&2
  fi
  if [ "$case_fail" -eq 0 ]; then
    if [ "$skipped" -eq 1 ]; then
      echo "SKIP (nondeterministic; score checked where available)"
    else
      echo PASS
    fi
  else
    echo FAIL
    fail=1
    echo "  artifacts kept in $SCRATCH/$tag" >&2
    if [ -f "$SCRATCH/$tag/score.txt" ]; then
      sed 's/^/  score: /' "$SCRATCH/$tag/score.txt" >&2
    fi
  fi
done

if [ "$RECORD" -eq 1 ]; then
  mv "$SCRATCH/new_refs.txt" "$REF"
  echo "wrote $REF" >&2
fi

USER_AFTER=$(user_fingerprint)
if [ "$USER_BEFORE" != "$USER_AFTER" ]; then
  echo "validate.sh: WARNING: game/build-local saves/config.ini changed" >&2
  echo "  (external change, e.g. you played during the run; validate only" >&2
  echo "   used the scratch copy and never wrote these files)" >&2
fi

if [ "$fail" -ne 0 ]; then
  exit 1
fi
echo "validation OK"
