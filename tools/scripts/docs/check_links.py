#!/usr/bin/env python3
"""Check repository references in the live documentation and code comments.

Collects relative Markdown links and backticked repository paths from every
live Markdown file and from the comments of game/src, then reports problems.
Live means everything except docs/history/, openspec/ (changes and specs are
records of their time), hidden directories and the vendored framework.
Backticked paths resolve against the repository root, then the file's own
directory (game/README.md says `tools/regen.sh` for game/tools/regen.sh).

  missing   the target does not exist in the working tree
  history   the target is under docs/history/ (temporary, must stay unlinked)
  work      the target is under work/ (local, not part of the repository)

usage: check_links.py [--without-history]

--without-history checks as if docs/history/ had been deleted: any reference
into it is then also reported as missing. Exit status is 1 when anything is
reported.
"""
import os
import re
import sys
from pathlib import Path

ROOT = Path(os.path.normpath(Path(__file__).absolute())).parents[3]


def norm(p):
    """Normalise without following symlinks (a vendored tree may be a link)."""
    return Path(os.path.normpath(p))
TOP = ("README.md", "docs/", "game/", "tools/", "openspec/", "maps/", "work/")
SKIP_DIRS = ("docs/history/", "openspec/", "game/snesrecomp/",
             "game/build", "game/src/gen/", "work/", "rom/", ".git/")
# Paths that are created locally by the build or the game, so they never
# exist in a clean checkout but are legitimate to mention.
LOCAL = ("game/build-local", "game/src/gen", "rom/", "game/recomp/funcs.h")

MD_LINK = re.compile(r"\]\(([^)\s#]+)(?:#[^)]*)?\)")
TICK = re.compile(r"`([^`\s]+)`")


def live_files():
    for p in sorted(ROOT.rglob("*.md")):
        rel = p.relative_to(ROOT).as_posix()
        if not rel.startswith(SKIP_DIRS) and not any(
                part.startswith(".") for part in Path(rel).parts):
            yield p, "md"
    for p in sorted((ROOT / "game/src").glob("*.[ch]")):
        yield p, "c"


def candidates(path, kind):
    text = path.read_text(encoding="utf-8", errors="replace")
    if kind == "c":
        text = "\n".join(re.findall(r"/\*.*?\*/|//[^\n]*", text, re.S))
        # Comments wrap long paths; also look at bare repo paths.
        for m in re.finditer(r"(?<![\w/.])((?:docs|openspec|tools|game)/[\w./-]+)", text):
            yield m.group(1), False
        return
    for m in MD_LINK.finditer(text):
        target = m.group(1)
        if "://" in target or target.startswith("mailto:"):
            continue
        yield target, True
    for m in TICK.finditer(text):
        yield m.group(1), False


def resolve(src, target, is_link):
    target = target.rstrip(".,;:)")
    if is_link:
        return norm(src.parent / target)
    if not target.startswith(TOP):
        return None
    local = norm(src.parent / target)
    if not (ROOT / target).exists() and local.exists():
        return local
    if any(ch in target for ch in "<>*{}$|"):
        return None
    if target.startswith(LOCAL):
        return None
    return norm(ROOT / target)


def main():
    without_history = "--without-history" in sys.argv[1:]
    problems = []
    for path, kind in live_files():
        rel_src = path.relative_to(ROOT).as_posix()
        for target, is_link in candidates(path, kind):
            dest = resolve(path, target, is_link)
            if dest is None:
                continue
            try:
                rel = dest.relative_to(ROOT).as_posix()
            except ValueError:
                problems.append((rel_src, target, "outside"))
                continue
            if rel.startswith("docs/history") and rel != "docs/history":
                problems.append((rel_src, target, "history"))
                if without_history:
                    problems.append((rel_src, target, "missing"))
                continue
            if rel.startswith("work/"):
                problems.append((rel_src, target, "work"))
                continue
            if not dest.exists():
                problems.append((rel_src, target, "missing"))
    for src, target, why in problems:
        print(f"{why:8} {src}: {target}")
    print(f"{len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
