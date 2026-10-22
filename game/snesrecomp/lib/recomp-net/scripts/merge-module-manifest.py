#!/usr/bin/env python3
"""Merge per-platform fragments from package-module.sh into netplay-module-manifest.json.

usage: merge-module-manifest.py <download-url-prefix> <out.json> <fragment.json>...
Refuses to write when platforms disagree on version, commit, ABI or wire version.
"""
import json, sys
prefix, out, *frags = sys.argv[1:]
m = None
for f in frags:
    d = json.load(open(f))
    if m is None:
        m = d
        continue
    for k in ("schema", "name", "version", "commit", "module_abi", "wire_version"):
        if d[k] != m[k]:
            sys.exit(f"{f}: {k} differs ({d[k]!r} vs {m[k]!r})")
    m["platforms"].update(d["platforms"])
for e in m["platforms"].values():
    e["url"] = prefix.rstrip("/") + "/" + e["archive"]
json.dump(m, open(out, "w"), indent=2)
print(f"{out}: {m['version']} {m['commit'][:8]} abi {m['module_abi']['version']} wire {m['wire_version']} {sorted(m['platforms'])}")
