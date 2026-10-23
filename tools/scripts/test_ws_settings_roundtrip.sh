#!/bin/sh
# Build the WsSettingsSave round-trip harness and run it in a scratch directory.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/ws-settings-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Wno-unused-parameter -DSNESRECOMP_SDL3=1 \
    -I"$root/game/src" \
    -I"$root/game/snesrecomp/runner/src" \
    -I"$root/game/snesrecomp/runner/src/desktop" \
    -I/home/linuxbrew/.linuxbrew/include \
    "$root/tools/scripts/ws_settings_roundtrip_test.c" \
    "$root/game/src/ws_settings.c" \
    -o "$tmp/roundtrip"

cd "$tmp"
./roundtrip
