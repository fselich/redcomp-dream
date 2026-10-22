#!/usr/bin/env python3
"""Report whether a generated tree will link, without building it.

    python snesrecomp/tools/check_link_closure.py src/gen

Exit 0 when every ``<Name>_M<m>X<x>`` variant the tree calls is also defined
in it; exit 1 with the undefined names and the translation units that
reference them otherwise.

``snesrecomp generate`` runs this automatically before it publishes, so a
fresh regen does not need it. It is here for the trees generation does not
own: one a player produced through the launcher's "Generate & rebuild"
wizard, one already on disk from an older engine revision, or one a bug
report arrives with. Seconds, against a link that costs minutes to fail.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(
    0, str(pathlib.Path(__file__).resolve().parents[1] / "recompiler"))

from v2.link_closure import scan_tree  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "gen_dir", nargs="?", default="src/gen",
        help="directory of generated *.c (default: src/gen)")
    parser.add_argument(
        "-q", "--quiet", action="store_true",
        help="print only the undefined variants, not the summary line")
    args = parser.parse_args()

    gen_dir = pathlib.Path(args.gen_dir)
    if not gen_dir.is_dir():
        print(f"check_link_closure: not a directory: {gen_dir}",
              file=sys.stderr)
        return 2
    report = scan_tree(gen_dir)
    if not report.units:
        print(f"check_link_closure: no *.c under {gen_dir} -- nothing to "
              f"check (regenerate first?)", file=sys.stderr)
        return 2
    if report.closed:
        if not args.quiet:
            print(report.summary())
        return 0
    print(report.detail() if not args.quiet else "\n".join(
        f"{name}\t{', '.join(sorted(where))}"
        for name, where in sorted(report.dangling.items())))
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
