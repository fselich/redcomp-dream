#!/usr/bin/env bash
# Compare the SNESRECOMP_* variables the port reads with the documentation.
#
#   undocumented  read in game/src but missing from docs/CONFIG.md
#   phantom       named in docs/CONFIG.md or README.md but not read in
#                 game/src and not listed under "Framework variables" in
#                 docs/CONFIG.md
#
# A documented family such as SNESRECOMP_WS_*_LOG is not expanded; every
# variable is listed by its full name. Exit status is 1 when anything is
# reported.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CONFIG="$ROOT/docs/CONFIG.md"

code=$(grep -rhoE '"SNESRECOMP_[A-Z0-9_]+"' "$ROOT/game/src" --include='*.c' --include='*.h' \
       | tr -d '"' | sort -u)
docs=$(cat "$CONFIG" "$ROOT/README.md" | grep -oE 'SNESRECOMP_[A-Z0-9_]+' \
       | grep -vE '_$' | sort -u)
# Variables the framework reads (documented in CONFIG.md's framework table).
framework=$(sed -n '/^## Framework variables/,/^## /p' "$CONFIG" \
            | grep -oE 'SNESRECOMP_[A-Z0-9_]+' | sort -u)

status=0
for v in $code; do
  if ! grep -qw "$v" "$CONFIG"; then
    echo "undocumented $v"
    status=1
  fi
done
for v in $docs; do
  if ! grep -qx "$v" <<< "$code" && ! grep -qx "$v" <<< "$framework"; then
    echo "phantom      $v"
    status=1
  fi
done
[ "$status" -eq 0 ] && echo "switches OK ($(wc -w <<< "$code") read in game/src)"
exit $status
