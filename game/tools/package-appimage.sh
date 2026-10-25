#!/usr/bin/env bash
# Build a playable Linux AppImage: the game, the recomp-ui launcher and its
# assets in one file. The AppImage loads assets from usr/bin/assets inside the
# mount and writes config/keybinds/saves/mods beside the user-visible file
# (the framework's $APPIMAGE policy).
#
# Usage (from anywhere):
#   game/tools/package-appimage.sh [options]
#
#   --build                 build the portable Linux binary first
#   --fetch-toolchain       fetch the Linux cmake-clang-v1 pack if absent (implies --build)
#   --toolchain-dir DIR     use an existing cmake-clang-v1 pack
#   --build-dir DIR         portable build directory (default: game/build-appimage)
#   --out-dir DIR           output directory (default: game/dist)
#   --version V             AppImage version (default: game/VERSION)
#   --icon PATH             PNG/TGA icon (default: launcher_assets/appicon.png,
#                           then launcher_assets/brand_mark.tga)
#   --rom PATH              ROM used by the smoke test (default: rom/Uniracers (USA).sfc)
#   --no-build              package an existing build-appimage (default)
#   --no-test               skip the launcher smoke test
#   --keep-appdir           keep the staged AppDir
#   -h, --help
#
# Output: game/dist/redcomp-dream-<version>-x86_64.AppImage and .sha256
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GAME_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd -- "$GAME_DIR/.." && pwd)"

BUILD_DIR="$GAME_DIR/build-appimage"
OUT_DIR="$GAME_DIR/dist"
TOOLCHAIN_DIR="${RETCOMM_TOOLCHAIN_DIR:-$GAME_DIR/.cache/toolchain-linux}"
TOOLCHAIN_DL="$GAME_DIR/.cache/toolchain-dl"
APPIMAGETOOL="$GAME_DIR/.cache/appimagetool/appimagetool-x86_64.AppImage"
APPIMAGETOOL_SHA="95cbe7cce9717fce90c484e34052ee7c7f1d7635b33c12525b4776826a7d29b6"
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
VERSION="$(cat "$GAME_DIR/VERSION" 2>/dev/null || echo dev)"
ICON=""
ROM=""
DO_BUILD=0
FETCH=0
DO_TEST=1
KEEP_APPDIR=0

usage() {
    sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
    printf 'package-appimage: %s\n' "$*" >&2
    exit 1
}

need_value() {
    [[ $# -ge 2 && -n "$2" ]] || die "$1 needs a value"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build)          DO_BUILD=1; shift ;;
        --fetch-toolchain) FETCH=1; DO_BUILD=1; shift ;;
        --toolchain-dir)  need_value "$@"; TOOLCHAIN_DIR="$2"; shift 2 ;;
        --build-dir)      need_value "$@"; BUILD_DIR="$2"; shift 2 ;;
        --out-dir)        need_value "$@"; OUT_DIR="$2"; shift 2 ;;
        --version)        need_value "$@"; VERSION="$2"; shift 2 ;;
        --icon)           need_value "$@"; ICON="$2"; shift 2 ;;
        --rom)            need_value "$@"; ROM="$2"; shift 2 ;;
        --no-build)       DO_BUILD=0; shift ;;
        --no-test)        DO_TEST=0; shift ;;
        --keep-appdir)    KEEP_APPDIR=1; shift ;;
        -h|--help)        usage; exit 0 ;;
        *)                die "unknown option: $1 (use --help)" ;;
    esac
done

[[ "$BUILD_DIR" == /* ]] || BUILD_DIR="$GAME_DIR/$BUILD_DIR"
[[ "$OUT_DIR" == /* ]] || OUT_DIR="$GAME_DIR/$OUT_DIR"
[[ "$TOOLCHAIN_DIR" == /* ]] || TOOLCHAIN_DIR="$GAME_DIR/$TOOLCHAIN_DIR"
if [[ -z "$ROM" ]]; then
    ROM="$ROOT_DIR/rom/Uniracers (USA).sfc"
fi

need_toolchain() {
    if [[ -f "$TOOLCHAIN_DIR/env.sh" ]]; then
        return 0
    fi
    if [[ "$FETCH" -eq 1 ]]; then
        local out
        mkdir -p "$GAME_DIR/.cache"
        out="$(bash "$GAME_DIR/snesrecomp/tools/ci/fetch_toolchain.sh" --artifact linux-x64 --out-dir "$TOOLCHAIN_DIR" --dl-dir "$TOOLCHAIN_DL")"
        TOOLCHAIN_DIR="$(printf '%s\n' "$out" | sed -n 's/^TOOLCHAIN_DIR=//p' | tail -1)"
        [[ -n "$TOOLCHAIN_DIR" && -f "$TOOLCHAIN_DIR/env.sh" ]] || die "toolchain fetch did not produce env.sh"
        return 0
    fi
    die "no Linux toolchain pack at $TOOLCHAIN_DIR/env.sh (use --fetch-toolchain or --toolchain-dir)"
}

build_portable() {
    need_toolchain
    # Bash is required: env.sh uses BASH_SOURCE to find its own root.
    # shellcheck disable=SC1090
    source "$TOOLCHAIN_DIR/env.sh"
    command -v cmake >/dev/null 2>&1 || die "pack cmake not on PATH"
    command -v ninja >/dev/null 2>&1 || die "pack ninja not on PATH"
    printf 'Configuring portable Linux build in %s\n' "$BUILD_DIR"
    cmake -S "$GAME_DIR" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DSNESRECOMP_SETUP_HOST=OFF \
        -DCMAKE_DISABLE_FIND_PACKAGE_Freetype=ON
    printf 'Building portable Linux binary\n'
    cmake --build "$BUILD_DIR" --parallel
}

if [[ "$DO_BUILD" -eq 1 ]]; then
    build_portable
else
    need_toolchain
    # shellcheck disable=SC1090
    source "$TOOLCHAIN_DIR/env.sh"
fi
[[ -n "${CMAKE_SYSROOT:-}" ]] || die "toolchain pack did not set CMAKE_SYSROOT"

EXE="$BUILD_DIR/redcomp-dream"
[[ -x "$EXE" ]] || die "missing $EXE (run with --build, or build it first)"
[[ -d "$BUILD_DIR/assets/fonts" ]] || die "missing $BUILD_DIR/assets/fonts; rebuild so recomp-ui stages assets"

# ImageMagick is used for the icon and for the launcher art already in assets.
IMAGE_TOOL=""
if command -v magick >/dev/null 2>&1; then
    IMAGE_TOOL="magick"
elif command -v convert >/dev/null 2>&1; then
    IMAGE_TOOL="convert"
fi

APPDIR="$BUILD_DIR/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
         "$APPDIR/usr/share/doc/redcomp-dream/licenses"

# ── executable and launcher assets ─────────────────────────────────────────
cp "$EXE" "$APPDIR/usr/bin/redcomp-dream"
chmod +x "$APPDIR/usr/bin/redcomp-dream"
cp -a "$BUILD_DIR/assets" "$APPDIR/usr/bin/assets"

# Bundle the exact C++/unwind runtime from the sysroot. The binary itself is
# built against glibc 2.34/GLIBCXX 3.4.29; the bundled pair makes that
# independent of the player's libstdc++ version. System GL/X11 stay system.
for lib in \
    "$CMAKE_SYSROOT/usr/lib/x86_64-linux-gnu/libstdc++.so.6" \
    "$CMAKE_SYSROOT/lib/x86_64-linux-gnu/libgcc_s.so.1"; do
    if [[ -f "$lib" ]]; then
        cp -L "$lib" "$APPDIR/usr/lib/"
    fi
done

# ── icon and desktop entry ─────────────────────────────────────────────────
ICON_SRC="$ICON"
if [[ -z "$ICON_SRC" && -f "$GAME_DIR/launcher_assets/appicon.png" ]]; then
    ICON_SRC="$GAME_DIR/launcher_assets/appicon.png"
fi
if [[ -z "$ICON_SRC" && -f "$GAME_DIR/launcher_assets/brand_mark.tga" ]]; then
    ICON_SRC="$GAME_DIR/launcher_assets/brand_mark.tga"
fi
if [[ -z "$ICON_SRC" && -f "$GAME_DIR/recomp-ui/assets/common/img/brand_mark.tga" ]]; then
    ICON_SRC="$GAME_DIR/recomp-ui/assets/common/img/brand_mark.tga"
fi
[[ -n "$ICON_SRC" ]] || die "no icon source (use --icon PATH or add launcher_assets/appicon.png)"
[[ -f "$ICON_SRC" ]] || die "icon not found: $ICON_SRC"
[[ -n "$IMAGE_TOOL" ]] || die "ImageMagick is required for the AppImage icon"
"$IMAGE_TOOL" "$ICON_SRC" -resize 256x256 -background none -gravity center \
    -extent 256x256 "$APPDIR/redcomp-dream.png"
cp "$APPDIR/redcomp-dream.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/redcomp-dream.png"
ln -sf redcomp-dream.png "$APPDIR/.DirIcon"

cat > "$APPDIR/redcomp-dream.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=redcomp-dream
Comment=Uniracers recompilation
Exec=redcomp-dream
Icon=redcomp-dream
Terminal=false
Categories=Game;
EOF
cp "$APPDIR/redcomp-dream.desktop" "$APPDIR/usr/share/applications/redcomp-dream.desktop"

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/redcomp-dream" "$@"
EOF
chmod +x "$APPDIR/AppRun"

# ── docs, version and licenses ─────────────────────────────────────────────
DOC="$APPDIR/usr/share/doc/redcomp-dream"
{
    printf 'redcomp-dream %s - Linux x86_64 AppImage\n\n' "$VERSION"
    printf 'You must legally own a copy of Uniracers (USA). No ROM is included.\n\n'
    printf 'Run this AppImage. The launcher asks for your ROM on first run\n'
    printf '(Browse For ROM) and verifies its SHA-256/CRC32. The launcher\n'
    printf 'fonts and artwork are inside the AppImage; you do not need an\n'
    printf 'assets/ folder beside this file.\n\n'
    printf 'config.ini, keybinds.ini, saves/ and mods/ are created beside the\n'
    printf '.AppImage. Keep the AppImage in a writable folder (for example\n'
    printf '~/Applications). In a race, F10 or Select+L opens the in-game menu.\n\n'
    printf 'Requires Linux x86_64 with glibc >= 2.35, a working OpenGL driver\n'
    printf 'and FUSE/libfuse2. Without FUSE, run:\n'
    printf '  ./redcomp-dream-%s-x86_64.AppImage --appimage-extract-and-run\n\n' "$VERSION"
    printf 'If there is no zenity or kdialog, the Browse button cannot open a\n'
    printf 'native picker; pass the ROM directly or drag it onto the AppImage:\n'
    printf '  ./redcomp-dream-%s-x86_64.AppImage --rom /path/Uniracers.sfc\n'
} > "$DOC/README.txt"
printf '%s\n' "$VERSION" > "$DOC/VERSION"

{
    if git -C "$ROOT_DIR" rev-parse --short HEAD >/dev/null 2>&1; then
        printf 'game=%s\n' "$(git -C "$ROOT_DIR" rev-parse HEAD)"
    fi
    if git -C "$GAME_DIR/recomp-ui" rev-parse HEAD >/dev/null 2>&1; then
        printf 'recomp-ui=%s\n' "$(git -C "$GAME_DIR/recomp-ui" rev-parse HEAD)"
    fi
    if [[ -f "$TOOLCHAIN_DIR/retcomm-toolchain.json" ]]; then
        printf 'toolchain=%s\n' "$(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$TOOLCHAIN_DIR/retcomm-toolchain.json" | head -1)"
    fi
} > "$DOC/BUILD_INFO.txt"

cp "$GAME_DIR/LICENSE" "$DOC/licenses/PROJECT_LICENSE"
cp "$GAME_DIR/snesrecomp/LICENSE" "$DOC/licenses/SNESRECOMP_LICENSE"
cp "$GAME_DIR/snesrecomp/THIRD_PARTY_ATTRIBUTION.md" "$DOC/licenses/THIRD_PARTY_ATTRIBUTION.md"
cp "$GAME_DIR/recomp-ui/LICENSE" "$DOC/licenses/RECOMP_UI_LICENSE"
cp "$GAME_DIR/recomp-ui/assets/common/fonts/NOTICE.md" "$DOC/licenses/LAUNCHER_FONT_NOTICE.md"
cp "$GAME_DIR/recomp-ui/assets/common/img/NOTICE.md" "$DOC/licenses/LAUNCHER_IMAGE_NOTICE.md"

# ── verification of the AppDir ─────────────────────────────────────────────
if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate "$APPDIR/redcomp-dream.desktop"
fi

LDD_LOG="$BUILD_DIR/ldd.txt"
LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ldd "$APPDIR/usr/bin/redcomp-dream" > "$LDD_LOG"
if grep -q 'not found' "$LDD_LOG"; then
    die "unresolved shared library (see $LDD_LOG)"
fi
BAD="$(grep -v "$APPDIR" "$LDD_LOG" | grep -E "$GAME_DIR|/home/linuxbrew|$HOME/\\.cache" || true)"
[[ -z "$BAD" ]] || {
    printf '%s\n' "$BAD" >&2
    die "the binary links a build-host library (see $LDD_LOG)"
}

for forbidden in config.ini keybinds.ini saves src/gen recomp/funcs.h; do
    if find "$APPDIR" -name "$(basename "$forbidden")" -print -quit | grep -q .; then
        die "AppDir contains forbidden $forbidden"
    fi
done
if find "$APPDIR" -type f \( -name '*.sfc' -o -name '*.smc' -o -name '*.srm' \) | grep -q .; then
    die "AppDir contains a ROM or save file"
fi

# ── appimagetool ───────────────────────────────────────────────────────────
if [[ ! -x "$APPIMAGETOOL" ]]; then
    mkdir -p "$(dirname "$APPIMAGETOOL")"
    printf 'Downloading appimagetool\n'
    curl -fL --retry 3 --retry-all-errors -o "$APPIMAGETOOL" "$APPIMAGETOOL_URL"
    chmod +x "$APPIMAGETOOL"
fi
printf '%s  %s\n' "$APPIMAGETOOL_SHA" "$APPIMAGETOOL" | sha256sum -c - >/dev/null \
    || die "appimagetool SHA-256 mismatch (delete $APPIMAGETOOL and retry)"

mkdir -p "$OUT_DIR"
APPIMAGE="$OUT_DIR/redcomp-dream-${VERSION}-x86_64.AppImage"
rm -f "$APPIMAGE"
printf 'Packaging %s\n' "$APPIMAGE"
ARCH=x86_64 "$APPIMAGETOOL" --appimage-extract-and-run \
    --no-appstream "$APPDIR" "$APPIMAGE"
chmod +x "$APPIMAGE"
[[ -s "$APPIMAGE" ]] || die "appimagetool produced no image"
( cd "$OUT_DIR" && sha256sum "$(basename "$APPIMAGE")" > "$(basename "$APPIMAGE").sha256" )

# ── smoke test ─────────────────────────────────────────────────────────────
if [[ "$DO_TEST" -eq 1 ]]; then
    if [[ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
        printf 'Skipping smoke test: no DISPLAY/WAYLAND_DISPLAY\n' >&2
    elif [[ ! -f "$ROM" ]]; then
        printf 'Skipping smoke test: ROM not found at %s\n' "$ROM" >&2
    else
        SMOKE="$(mktemp -d "${TMPDIR:-/tmp}/redcomp-dream-appimage.XXXXXX")"
        trap 'rm -rf "$SMOKE"' EXIT
        cp "$APPIMAGE" "$SMOKE/"
        BASE="$(basename "$APPIMAGE")"
        SHOT="$SMOKE/launcher.png"
        echo "Smoke-testing $BASE from $SMOKE"
        ( cd "$SMOKE" && env -u SNESRECOMP_FORCE_SETUP \
            LNG_SCRIPT="wait:60;shot:$SHOT;quit" \
            timeout 90 "./$BASE" --appimage-extract-and-run --launcher --rom "$ROM" ) \
            >"$SMOKE/run.log" 2>&1 || {
                tail -40 "$SMOKE/run.log" >&2
                die "AppImage smoke test failed"
            }
        [[ -f "$SHOT" ]] || die "smoke test produced no launcher screenshot"
        [[ -f "$SMOKE/config.ini" ]] || die "config.ini was not created beside the AppImage"
        [[ -f "$SMOKE/keybinds.ini" ]] || die "keybinds.ini was not created beside the AppImage"
        [[ ! -d "$SMOKE/assets" ]] || die "AppImage created or required an assets folder beside it"
        cp "$SHOT" "$OUT_DIR/redcomp-dream-${VERSION}-launcher.png"
        cp "$SMOKE/run.log" "$OUT_DIR/redcomp-dream-${VERSION}-smoke.log"
        printf 'Smoke test passed; screenshot: %s\n' "$SHOT"
        rm -rf "$SMOKE"
        trap - EXIT
    fi
fi

if [[ "$KEEP_APPDIR" -eq 0 ]]; then
    rm -rf "$APPDIR"
else
    printf 'AppDir kept at %s\n' "$APPDIR"
fi

printf '\nAppImage: %s\n' "$APPIMAGE"
printf 'Size:     %s\n' "$(du -h "$APPIMAGE" | cut -f1)"
printf 'SHA-256:  %s.sha256\n' "$(basename "$APPIMAGE")"
printf 'Contents: redcomp-dream, usr/bin/assets, AppRun, .desktop, icon, docs/licenses\n'
