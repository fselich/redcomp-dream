#!/usr/bin/env bash
# Convert box art / brand / pad images into the TGAs the recomp-ui launcher
# loads, then rebuild the game so they are staged beside the executable.
#
# Usage (from anywhere):
#   game/tools/launcher-art.sh --boxart IMAGE [--brand IMAGE] [--pad IMAGE]
#       [--build-dir DIR] [--no-build] [--fill] [--key-black] [--trim]
#       [--boxart-size WxH] [--brand-size WxH] [--pad-size WxH]
#       [--bg COLOR] [--clear-boxart] [--clear-brand] [--clear-pad] [--clear]
#
# The generated TGAs live in game/launcher_assets/ and are optional: without
# them recomp-ui uses its default cartridge, brand mark and SNES pad. Commit
# the ones you want to ship; stage_setup_host.sh only copies tracked files.
#
# Examples:
#   game/tools/launcher-art.sh \
#       --boxart "Logo retro pixel art Redcomp Dream.png" \
#       --key-black --trim
#   game/tools/launcher-art.sh \
#       --brand "Logotipo retro_ Uniciclo y Sueño.png" \
#       --key-black --trim
#   game/tools/launcher-art.sh --clear
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GAME_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
ASSETS_DIR="$GAME_DIR/launcher_assets"
BUILD_DIR="$GAME_DIR/build-local"

BOXART=""
BRAND=""
PAD=""
DO_BUILD=1
FILL=0
KEY_BLACK=0
TRIM=0
BG="none"
BOXART_SIZE="768x1024"
BRAND_SIZE="256x256"
PAD_SIZE="512x512"
CLEAR_BOXART=0
CLEAR_BRAND=0
CLEAR_PAD=0

usage() {
    sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
    printf 'launcher-art: %s\n' "$*" >&2
    exit 1
}

need_value() {
    [[ $# -ge 2 && -n "$2" ]] || die "$1 needs a value"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --boxart)       need_value "$@"; BOXART="$2"; shift 2 ;;
        --brand)        need_value "$@"; BRAND="$2"; shift 2 ;;
        --pad)          need_value "$@"; PAD="$2"; shift 2 ;;
        --build-dir)    need_value "$@"; BUILD_DIR="$2"; shift 2 ;;
        --boxart-size)  need_value "$@"; BOXART_SIZE="$2"; shift 2 ;;
        --brand-size)   need_value "$@"; BRAND_SIZE="$2"; shift 2 ;;
        --pad-size)     need_value "$@"; PAD_SIZE="$2"; shift 2 ;;
        --bg)           need_value "$@"; BG="$2"; shift 2 ;;
        --no-build)     DO_BUILD=0; shift ;;
        --fill)         FILL=1; shift ;;
        --key-black)    KEY_BLACK=1; shift ;;
        --trim)         TRIM=1; shift ;;
        --clear-boxart) CLEAR_BOXART=1; shift ;;
        --clear-brand)  CLEAR_BRAND=1; shift ;;
        --clear-pad)    CLEAR_PAD=1; shift ;;
        --clear)        CLEAR_BOXART=1; CLEAR_BRAND=1; CLEAR_PAD=1; shift ;;
        -h|--help)      usage; exit 0 ;;
        *)              die "unknown option: $1 (use --help)" ;;
    esac
done

if [[ -z "$BOXART$BRAND$PAD" && "$CLEAR_BOXART" -eq 0 &&
      "$CLEAR_BRAND" -eq 0 && "$CLEAR_PAD" -eq 0 ]]; then
    die "give at least one of --boxart, --brand, --pad or --clear"
fi

for src in "$BOXART" "$BRAND" "$PAD"; do
    [[ -z "$src" || -f "$src" ]] || die "image not found: $src"
done

IMAGE_TOOL=""
if command -v magick >/dev/null 2>&1; then
    IMAGE_TOOL="magick"
elif command -v convert >/dev/null 2>&1; then
    IMAGE_TOOL="convert"
else
    die "ImageMagick not found (install 'magick' or 'convert')"
fi

mkdir -p "$ASSETS_DIR"

# Resolve a build directory given on the command line relative to game/.
if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$GAME_DIR/$BUILD_DIR"
fi

# convert_image <src> <dest> <WxH> <fit|fill>
convert_image() {
    local src="$1" dest="$2" size="$3" mode="$4"
    local w="${size%x*}" h="${size#*x}"
    [[ "$w" =~ ^[0-9]+$ && "$h" =~ ^[0-9]+$ ]] \
        || die "invalid size '$size' (expected WxH)"
    local -a args=("$src")
    if [[ "$KEY_BLACK" -eq 1 ]]; then
        args+=(-fuzz 8% -transparent black)
    fi
    if [[ "$TRIM" -eq 1 ]]; then
        args+=(-trim +repage)
    fi
    if [[ "$mode" == "fill" ]]; then
        args+=(-resize "${w}x${h}^" -gravity center -extent "${w}x${h}")
    else
        args+=(-resize "${w}x${h}" -background "$BG" -gravity center \
               -extent "${w}x${h}")
    fi
    # ImageMagick's TGA rows come out upside down to stb_image (the loader
    # recomp-ui uses), so flip once here. The launcher's own TGAs are authored
    # the other way round; generated art needs this.
    args+=(-flip -define tga:bits-per-pixel=32 "$dest")
    "$IMAGE_TOOL" "${args[@]}"
    [[ -s "$dest" ]] || die "conversion produced no file: $dest"
    printf '  %-8s %s -> %s (%s, %s)\n' \
        "$(basename "$dest" .tga)" "$src" "$dest" "$size" "$mode"
}

clear_one() {
    local name="$1"
    rm -f "$ASSETS_DIR/$name.tga"
    rm -f "$BUILD_DIR/assets/img/$name.tga"
    printf '  cleared %s\n' "$name.tga"
}

# Copy the generated TGA into the build's asset dir now. A POST_BUILD copy
# only runs when the executable relinks, and changing an image alone does not
# relink it, so the direct copy is what makes a second run show the new art.
stage_one() {
    local name="$1"
    [[ -f "$ASSETS_DIR/$name.tga" ]] || return 0
    mkdir -p "$BUILD_DIR/assets/img"
    cp -f "$ASSETS_DIR/$name.tga" "$BUILD_DIR/assets/img/$name.tga"
}

if [[ "$CLEAR_BOXART" -eq 1 ]]; then clear_one boxart; fi
if [[ "$CLEAR_BRAND" -eq 1 ]]; then clear_one brand_mark; fi
if [[ "$CLEAR_PAD" -eq 1 ]]; then clear_one pad; fi

if [[ -n "$BOXART" ]]; then
    convert_image "$BOXART" "$ASSETS_DIR/boxart.tga" "$BOXART_SIZE" \
        "$([[ "$FILL" -eq 1 ]] && echo fill || echo fit)"
fi
if [[ -n "$BRAND" ]]; then
    convert_image "$BRAND" "$ASSETS_DIR/brand_mark.tga" "$BRAND_SIZE" \
        "$([[ "$FILL" -eq 1 ]] && echo fill || echo fit)"
fi

# The Windows executable icon is generated from the same brand mark. Keep it
# in resources/ so the build does not need ImageMagick on the player's machine.
if [[ -n "$BRAND" ]]; then
    mkdir -p "$GAME_DIR/resources"
    "$IMAGE_TOOL" "$ASSETS_DIR/brand_mark.tga" -background none -gravity center \
        -resize 256x256 -define icon:auto-resize=256,128,64,48,32,16 \
        "$GAME_DIR/resources/redcomp-dream.ico"
    printf '  icon     %s\n' "$GAME_DIR/resources/redcomp-dream.ico"
fi
if [[ -n "$PAD" ]]; then
    convert_image "$PAD" "$ASSETS_DIR/pad.tga" "$PAD_SIZE" \
        "$([[ "$FILL" -eq 1 ]] && echo fill || echo fit)"
fi

stage_one boxart
stage_one brand_mark
stage_one pad

if [[ "$DO_BUILD" -eq 1 ]]; then
    echo "Configuring and building $BUILD_DIR ..."
    cmake -S "$GAME_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" --parallel
    for f in boxart brand_mark pad; do
        if [[ -f "$ASSETS_DIR/$f.tga" ]]; then
            [[ -f "$BUILD_DIR/assets/img/$f.tga" ]] \
                || die "$BUILD_DIR/assets/img/$f.tga was not staged"
            printf '  staged   %s\n' "$BUILD_DIR/assets/img/$f.tga"
        fi
    done
else
    echo "Skipped build. Run: cmake --build \"$BUILD_DIR\" --parallel"
fi

echo
echo "Open the launcher with: ./run.sh --launcher"
echo "Generated art is in: $ASSETS_DIR"
echo "Commit the TGAs you want in a release; the packager only copies tracked files."
