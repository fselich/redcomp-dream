#!/usr/bin/env bash
# Builds the netplay module for this platform and writes what a launcher
# installs from (docs/module.md): the archive, its .sha256, and a manifest.
#
#   scripts/package-module.sh <rbengine checkout> <out dir> [platform-key]
#
# The manifest lists ONLY this platform; CI merges the per-platform fragments
# (scripts/merge-module-manifest.py). The library is read back out of the
# archive and asked what it is (module_test --info), so the manifest states
# what the shipped file reports, not what the build intended.
set -euo pipefail
RBE=${1:?rbengine checkout}; OUT=${2:?out dir}
here=$(cd "$(dirname "$0")/.." && pwd)
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) KEY=linux-x86_64; LIB=librecomp_net_module.so ;;
  Linux-aarch64) KEY=linux-arm64; LIB=librecomp_net_module.so ;;
  Darwin-arm64) KEY=macos-arm64; LIB=librecomp_net_module.dylib ;;
  Darwin-x86_64) KEY=macos-x86_64; LIB=librecomp_net_module.dylib ;;
  *) echo "unsupported platform" >&2; exit 1 ;;
esac
KEY=${3:-$KEY}
commit=$(git -C "$here" rev-parse HEAD)
ver=${VERSION:-$(sed -n 's/^project(recomp_net VERSION \([0-9.]*\).*/\1/p' "$here/CMakeLists.txt")}
mkdir -p "$OUT"; B=$(mktemp -d)
cmake -S "$here" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DRNET_BUILD_MODULE=ON \
  -DRNET_RBENGINE_DIR="$RBE" -DRNET_BUILD_EXAMPLES=OFF -DRNET_BUILD_TESTS=ON \
  -DRNET_BUILD_ID="$commit" -DRNET_BUILD_VERSION="$ver" >/dev/null
ninja -C "$B" recomp_net_module module_test >/dev/null
ctest --test-dir "$B" -R module_test --output-on-failure >/dev/null
archive="recomp-net-module-$ver-$KEY.tar.gz"
X=$(mktemp -d); cp "$B/$LIB" "$X/"; cp "$here/LICENSE" "$X/"
tar -C "$X" -czf "$OUT/$archive" "$LIB" LICENSE
if command -v sha256sum >/dev/null; then sum() { sha256sum "$@"; }; else sum() { shasum -a 256 "$@"; }; fi
sha=$(sum "$OUT/$archive" | cut -d' ' -f1); echo "$sha  $archive" > "$OUT/$archive.sha256"
# What the shipped file says about itself.
V=$(mktemp -d); tar -C "$V" -xzf "$OUT/$archive"
info=$("$B/module_test" "$V/$LIB" --info)
got=$(sed -n 's/^version=\([0-9.]*\) .*/\1/p' <<<"$info")
[ "$got" = "$ver" ] || { echo "library reports version $got, not $ver" >&2; exit 1; }
abi=$(sed -n 's/.*abi=\([0-9]*\).*/\1/p' <<<"$info"); wire=$(sed -n 's/.*wire=\([0-9]*\).*/\1/p' <<<"$info")
built=$(sed -n 's/.*build=\([0-9a-f]*\).*/\1/p' <<<"$info")
[ "$built" = "$commit" ] || { echo "library reports build $built, not $commit" >&2; exit 1; }
glibc=$(objdump -T "$V/$LIB" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1 | sed 's/GLIBC_//' || true)
python3 - "$OUT/$KEY.manifest.json" "$ver" "$commit" "$abi" "$wire" "$KEY" "$archive" "$sha" \
  "$(stat -c %s "$OUT/$archive" 2>/dev/null || stat -f %z "$OUT/$archive")" "$LIB" "${glibc:-}" <<'PY'
import json,sys
out,ver,commit,abi,wire,key,archive,sha,size,lib,glibc=sys.argv[1:]
entry={"archive":archive,"url":"","sha256":sha,"size":int(size),"library":lib,"requires":{}}
if glibc: entry["requires"]["glibc"]=glibc
json.dump({"schema":1,"name":"recomp-net-module","version":ver,"commit":commit,
 "module_abi":{"version":int(abi)},"wire_version":int(wire),"platforms":{key:entry}},open(out,"w"),indent=2)
PY
echo "wrote $OUT/$archive and $OUT/$KEY.manifest.json (abi $abi wire $wire)"
