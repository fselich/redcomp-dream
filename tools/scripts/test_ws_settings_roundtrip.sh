#!/bin/sh
# Build the WsSettingsSave round-trip harness and run it in a scratch directory.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/ws-settings-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# SDL3 headers live in different prefixes per system. Ask pkg-config when it
# is available; SDL3_CFLAGS can override it.
sdl3_cflags=${SDL3_CFLAGS:-}
if [ -z "$sdl3_cflags" ] && command -v pkg-config >/dev/null 2>&1; then
    sdl3_cflags=$(pkg-config --cflags sdl3 2>/dev/null || true)
fi

cc -std=c11 -Wall -Wextra -Wno-unused-parameter -DSNESRECOMP_SDL3=1 \
    -I"$root/game/src" \
    -I"$root/game/snesrecomp/runner/src" \
    -I"$root/game/snesrecomp/runner/src/desktop" \
    $sdl3_cflags \
    "$root/tools/scripts/ws_settings_roundtrip_test.c" \
    "$root/game/src/ws_settings.c" \
    -o "$tmp/roundtrip"

cd "$tmp"
./roundtrip
