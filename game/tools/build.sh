#!/usr/bin/env bash
# Build Uniracers for Linux, for Windows (.exe), or both.
#
# Usage (from anywhere):
#   game/tools/build.sh [linux|windows|both] [options]
#
#   linux       native build, default (game/build-local)
#   windows     cross-build to redcomp-dream.exe (game/build-windows)
#   both        both of the above
#
# Options:
#   --debug                 CMAKE_BUILD_TYPE=Debug (default Release)
#   --clean                 clean build artifacts (keeps config.ini/saves/)
#   --jobs N                parallel build jobs (default: nproc)
#   --linux-dir DIR         Linux build directory (default: game/build-local)
#   --windows-dir DIR       Windows build directory (default: game/build-windows)
#   --build-dir DIR         alias for --linux-dir
#   --toolchain-dir DIR     existing llvm-mingw/mingw prefix for Windows
#   --fetch-toolchain       download llvm-mingw if no Windows cross compiler is found
#   --llvm-mingw-version V  llvm-mingw release to fetch (default: 20261020)
#
# Windows cross-build needs a MinGW-w64 toolchain. If you do not have one,
# --fetch-toolchain downloads a self-contained llvm-mingw into game/.cache/.
# The result is a static .exe; copy it with assets/ (and mods/) to Windows.
#
# The generated C must already exist. If it does not, run:
#   bash game/tools/regen.sh --rom "../rom/Uniracers (USA).sfc"
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GAME_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

TARGET="linux"
BUILD_TYPE="Release"
JOBS=""
CLEAN=0
FETCH_TOOLCHAIN=0
TOOLCHAIN_DIR="${MINGW_PREFIX:-}"
LLVM_MINGW_VERSION="${LLVM_MINGW_VERSION:-20261020}"
LINUX_DIR="$GAME_DIR/build-local"
WINDOWS_DIR="$GAME_DIR/build-windows"
CACHE_DIR="$GAME_DIR/.cache/llvm-mingw"

usage() {
    sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
    printf 'build: %s\n' "$*" >&2
    exit 1
}

need_value() {
    [[ $# -ge 2 && -n "$2" ]] || die "$1 needs a value"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        linux|windows|both) TARGET="$1"; shift ;;
        --debug)            BUILD_TYPE="Debug"; shift ;;
        --clean)            CLEAN=1; shift ;;
        --jobs)             need_value "$@"; JOBS="$2"; shift 2 ;;
        --linux-dir|--build-dir) need_value "$@"; LINUX_DIR="$2"; shift 2 ;;
        --windows-dir)      need_value "$@"; WINDOWS_DIR="$2"; shift 2 ;;
        --toolchain-dir)    need_value "$@"; TOOLCHAIN_DIR="$2"; shift 2 ;;
        --fetch-toolchain)  FETCH_TOOLCHAIN=1; shift ;;
        --llvm-mingw-version) need_value "$@"; LLVM_MINGW_VERSION="$2"; shift 2 ;;
        -h|--help)          usage; exit 0 ;;
        *)                  die "unknown option: $1 (use --help)" ;;
    esac
done

if [[ -z "$JOBS" ]]; then
    JOBS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
fi
[[ "$JOBS" =~ ^[0-9]+$ && "$JOBS" -gt 0 ]] || die "invalid --jobs '$JOBS'"

# Absolute paths, so a build dir given as a relative name still works.
[[ "$LINUX_DIR" == /* ]] || LINUX_DIR="$GAME_DIR/$LINUX_DIR"
[[ "$WINDOWS_DIR" == /* ]] || WINDOWS_DIR="$GAME_DIR/$WINDOWS_DIR"

need_linux=0
need_windows=0
case "$TARGET" in
    linux)   need_linux=1 ;;
    windows) need_windows=1 ;;
    both)    need_linux=1; need_windows=1 ;;
esac

[[ -x "$GAME_DIR/recomp-ui/recomp_ui.cmake" || -f "$GAME_DIR/recomp-ui/recomp_ui.cmake" ]] \
    || die "recomp-ui missing. Run: git submodule update --init --recursive"
if ! ls "$GAME_DIR"/src/gen/*.c >/dev/null 2>&1; then
    die "no generated C in src/gen. Run: bash tools/regen.sh --rom \"../rom/Uniracers (USA).sfc\""
fi
command -v cmake >/dev/null 2>&1 || die "cmake not found"

build_one() {
    local dir="$1" label="$2"
    shift 2
    local -a extra=("$@")
    if [[ "$CLEAN" -eq 1 && -f "$dir/CMakeCache.txt" ]]; then
        printf 'Cleaning build artifacts in %s (config.ini and saves/ are kept)\n' "$dir"
        cmake --build "$dir" --target clean >/dev/null 2>&1 || true
    fi
    mkdir -p "$dir"
    printf 'Configuring %s in %s (%s)\n' "$label" "$dir" "$BUILD_TYPE"
    cmake -S "$GAME_DIR" -B "$dir" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DSNESRECOMP_SETUP_HOST=OFF \
        "${extra[@]}"
    printf 'Building %s with %s jobs\n' "$label" "$JOBS"
    cmake --build "$dir" --parallel "$JOBS"
}

build_linux() {
    build_one "$LINUX_DIR" "Linux"
    local exe="$LINUX_DIR/redcomp-dream"
    [[ -x "$exe" ]] || die "Linux build did not produce $exe"
    printf '\nLinux: %s\n' "$exe"
    printf 'Run:   ./run.sh\n'
}

# ── Windows cross toolchain ─────────────────────────────────────────────────

find_cached_toolchain() {
    local root
    root="$(find "$CACHE_DIR" -maxdepth 6 \
        \( -name x86_64-w64-mingw32-clang -o -name x86_64-w64-mingw32-gcc \) \
        2>/dev/null | head -1 || true)"
    [[ -n "$root" ]] && dirname "$(dirname "$root")"
}

toolchain_from_prefix() {
    local prefix="$1"
    if [[ -x "$prefix/bin/x86_64-w64-mingw32-clang" ]]; then
        echo "clang"
    elif [[ -x "$prefix/bin/x86_64-w64-mingw32-gcc" ]]; then
        echo "gcc"
    fi
}

resolve_windows_toolchain() {
    local found=""
    if [[ -n "$TOOLCHAIN_DIR" ]]; then
        [[ -d "$TOOLCHAIN_DIR" ]] || die "--toolchain-dir is not a directory: $TOOLCHAIN_DIR"
        found="$TOOLCHAIN_DIR"
    elif command -v x86_64-w64-mingw32-clang >/dev/null 2>&1; then
        found="$(dirname "$(dirname "$(command -v x86_64-w64-mingw32-clang)")")"
    elif command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
        found="$(dirname "$(dirname "$(command -v x86_64-w64-mingw32-gcc)")")"
    else
        found="$(find_cached_toolchain || true)"
    fi

    if [[ -z "$found" && "$FETCH_TOOLCHAIN" -eq 1 ]]; then
        fetch_llvm_mingw
        found="$(find_cached_toolchain || true)"
    fi
    [[ -n "$found" ]] || die \
        "no Windows cross compiler found. Use --fetch-toolchain, --toolchain-dir, or install mingw-w64."

    TOOLCHAIN_DIR="$found"
    TOOLCHAIN_KIND="$(toolchain_from_prefix "$TOOLCHAIN_DIR")"
    [[ -n "$TOOLCHAIN_KIND" ]] || die \
        "no x86_64-w64-mingw32-clang/gcc under $TOOLCHAIN_DIR/bin"
}

fetch_llvm_mingw() {
    local asset="llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz"
    local url="https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/${asset}"
    local dest="$CACHE_DIR/$LLVM_MINGW_VERSION"
    local archive="$CACHE_DIR/$asset"
    mkdir -p "$dest" "$CACHE_DIR"
    if find "$dest" -maxdepth 6 -name x86_64-w64-mingw32-clang \
        | grep -q .; then
        return 0
    fi
    printf 'Downloading llvm-mingw %s (~84 MB)\n' "$LLVM_MINGW_VERSION"
    curl -fL --retry 3 --retry-all-errors -o "$archive" "$url"
    tar -xf "$archive" -C "$dest"
    rm -f "$archive"
    find "$dest" -maxdepth 6 -name x86_64-w64-mingw32-clang \
        | grep -q . || die "llvm-mingw archive did not contain the cross compiler"
}

write_windows_toolchain() {
    local file="$CACHE_DIR/toolchain-mingw.cmake"
    local sysroot prefix
    prefix="$TOOLCHAIN_DIR"
    if [[ -d "$prefix/x86_64-w64-mingw32" ]]; then
        sysroot="$prefix/x86_64-w64-mingw32"
    else
        sysroot="/usr/x86_64-w64-mingw32"
    fi
    local cc cxx rc
    if [[ "$TOOLCHAIN_KIND" == "clang" ]]; then
        cc="$prefix/bin/x86_64-w64-mingw32-clang"
        cxx="$prefix/bin/x86_64-w64-mingw32-clang++"
        if [[ -x "$prefix/bin/x86_64-w64-mingw32-windres" ]]; then
            rc="$prefix/bin/x86_64-w64-mingw32-windres"
        elif [[ -x "$prefix/bin/llvm-windres" ]]; then
            rc="$prefix/bin/llvm-windres"
        else
            die "no windres in $prefix/bin"
        fi
    else
        cc="$prefix/bin/x86_64-w64-mingw32-gcc"
        cxx="$prefix/bin/x86_64-w64-mingw32-g++"
        rc="$prefix/bin/x86_64-w64-mingw32-windres"
    fi
    mkdir -p "$CACHE_DIR"
    cat > "$file" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "$cc")
set(CMAKE_CXX_COMPILER "$cxx")
set(CMAKE_RC_COMPILER "$rc")
set(CMAKE_FIND_ROOT_PATH "$sysroot")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
EOF
    echo "$file"
}

build_windows() {
    resolve_windows_toolchain
    local toolchain_file
    toolchain_file="$(write_windows_toolchain)"
    build_one "$WINDOWS_DIR" "Windows" \
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain_file" \
        -DSNESRECOMP_STATIC_RUNTIME=ON \
        -DSNESRECOMP_SDL3_FETCH=ON
    local exe="$WINDOWS_DIR/redcomp-dream.exe"
    [[ -f "$exe" ]] || die "Windows build did not produce $exe"
    printf '\nWindows: %s\n' "$exe"
    printf 'Copy the .exe with its assets/ (and mods/) directory to Windows.\n'
}

case "$TARGET" in
    linux)   build_linux ;;
    windows) build_windows ;;
    both)    build_linux; build_windows ;;
esac
