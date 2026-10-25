#!/usr/bin/env bash
# Package a playable Windows build as a zip: the .exe plus the assets it loads
# (fonts, box art, brand, pad) and the optional mods/ directory. This is NOT a
# setup pack: it contains no source tree, no generated C and no toolchain.
#
# Usage (from anywhere):
#   game/tools/package-windows.sh [options]
#
#   --build                 build first with tools/build.sh (Windows target)
#   --fetch-toolchain       pass --fetch-toolchain to build.sh (implies --build)
#   --build-dir DIR         Windows build directory (default: game/build-windows)
#   --out-dir DIR           output directory (default: game/dist)
#   --version V             package version (default: game/VERSION)
#   --no-build              package the existing build (default)
#   -h, --help
#
# The generated zip is redcomp-dream-<version>-windows-x64.zip. It intentionally
# does not carry config.ini, saves/, a ROM, src/gen or recomp/funcs.h.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GAME_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd -- "$GAME_DIR/.." && pwd)"

BUILD_DIR="$GAME_DIR/build-windows"
OUT_DIR="$GAME_DIR/dist"
VERSION="$(cat "$GAME_DIR/VERSION" 2>/dev/null || echo dev)"
DO_BUILD=0
FETCH=0

usage() {
    sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
    printf 'package-windows: %s\n' "$*" >&2
    exit 1
}

need_value() {
    [[ $# -ge 2 && -n "$2" ]] || die "$1 needs a value"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build)          DO_BUILD=1; shift ;;
        --fetch-toolchain) FETCH=1; DO_BUILD=1; shift ;;
        --build-dir)      need_value "$@"; BUILD_DIR="$2"; shift 2 ;;
        --out-dir)        need_value "$@"; OUT_DIR="$2"; shift 2 ;;
        --version)        need_value "$@"; VERSION="$2"; shift 2 ;;
        --no-build)       DO_BUILD=0; shift ;;
        -h|--help)        usage; exit 0 ;;
        *)                die "unknown option: $1 (use --help)" ;;
    esac
done

[[ "$BUILD_DIR" == /* ]] || BUILD_DIR="$GAME_DIR/$BUILD_DIR"
[[ "$OUT_DIR" == /* ]] || OUT_DIR="$GAME_DIR/$OUT_DIR"

if [[ "$DO_BUILD" -eq 1 ]]; then
    build_args=(windows --windows-dir "$BUILD_DIR")
    if [[ "$FETCH" -eq 1 ]]; then
        build_args+=(--fetch-toolchain)
    fi
    "$GAME_DIR/tools/build.sh" "${build_args[@]}"
fi

command -v zip >/dev/null 2>&1 || die "zip not found"
EXE="$BUILD_DIR/redcomp-dream.exe"
[[ -f "$EXE" ]] || die "missing $EXE (run with --build, or tools/build.sh windows)"
if command -v file >/dev/null 2>&1; then
    file "$EXE" | grep -q 'PE32+' || die "$EXE is not a PE32+ executable"
fi
[[ -d "$BUILD_DIR/assets/fonts" ]] \
    || die "missing $BUILD_DIR/assets/fonts; rebuild so recomp-ui stages its assets"

NAME="redcomp-dream-${VERSION}-windows-x64"
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/uniracers-win.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
DEST="$STAGE/$NAME"
mkdir -p "$DEST/licenses"

cp "$EXE" "$DEST/redcomp-dream.exe"
cp -a "$BUILD_DIR/assets" "$DEST/assets"
if [[ -d "$BUILD_DIR/mods" ]]; then
    cp -a "$BUILD_DIR/mods" "$DEST/mods"
fi
cp "$GAME_DIR/LICENSE" "$DEST/licenses/PROJECT_LICENSE"
cp "$GAME_DIR/snesrecomp/LICENSE" "$DEST/licenses/SNESRECOMP_LICENSE"
cp "$GAME_DIR/snesrecomp/THIRD_PARTY_ATTRIBUTION.md" "$DEST/licenses/THIRD_PARTY_ATTRIBUTION.md"
cp "$GAME_DIR/recomp-ui/LICENSE" "$DEST/licenses/RECOMP_UI_LICENSE"
cp "$GAME_DIR/recomp-ui/assets/common/fonts/NOTICE.md" "$DEST/licenses/LAUNCHER_FONT_NOTICE.md"
cp "$GAME_DIR/recomp-ui/assets/common/img/NOTICE.md" "$DEST/licenses/LAUNCHER_IMAGE_NOTICE.md"
printf '%s\n' "$VERSION" > "$DEST/VERSION"

{
    printf 'Uniracers recomp %s - Windows x64\n\n' "$VERSION"
    printf 'You must legally own a copy of Uniracers (USA). No ROM is included.\n\n'
    printf 'Run redcomp-dream.exe. The launcher asks for your ROM on first\n'
    printf 'run (Browse For ROM) and verifies its SHA-256/CRC32. Keep the assets/\n'
    printf 'folder beside the executable: it holds the launcher fonts and art.\n\n'
    printf 'In a race, F10 or Select+L opens the in-game options menu.\n'
    printf 'config.ini, keybinds.ini and saves/ are created beside the executable.\n\n'
    printf 'The binary is self-contained: no MinGW, SDL or toolchain DLLs are needed.\n'
    printf 'Windows 10/11 x64.\n'
} > "$DEST/README.txt"

if git -C "$ROOT_DIR" rev-parse --short HEAD >/dev/null 2>&1; then
    {
        printf 'game=%s\n' "$(git -C "$ROOT_DIR" rev-parse HEAD)"
        if git -C "$GAME_DIR/recomp-ui" rev-parse HEAD >/dev/null 2>&1; then
            printf 'recomp-ui=%s\n' "$(git -C "$GAME_DIR/recomp-ui" rev-parse HEAD)"
        fi
    } > "$DEST/BUILD_INFO.txt"
fi

mkdir -p "$OUT_DIR"
ZIP="$OUT_DIR/$NAME.zip"
rm -f "$ZIP"
( cd "$STAGE" && zip -qr "$ZIP" "$NAME" )
[[ -s "$ZIP" ]] || die "zip was not created: $ZIP"

if command -v sha256sum >/dev/null 2>&1; then
    ( cd "$OUT_DIR" && sha256sum "$(basename "$ZIP")" > "$(basename "$ZIP").sha256" )
fi

printf '\nPackaged: %s\n' "$ZIP"
printf 'Size:     %s\n' "$(du -h "$ZIP" | cut -f1)"
printf 'Contents: redcomp-dream.exe, assets/, mods/ (if present), README.txt, licenses/\n'
