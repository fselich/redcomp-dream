"""Link-closure check for a generated tree.

A generated program is *link-closed* when every ``<Name>_M<m>X<x>`` variant a
body calls is also defined by some translation unit in the same tree. When it
is not, the whole pipeline still succeeds -- analysis, emission, sharding, the
idempotence check, the atomic publish -- and the failure surfaces minutes later
as a wall of linker noise (``ld.lld: undefined symbol`` / MSVC LNK2019) naming
mangled symbols and object files, with nothing pointing back at generation.

This module is the tripwire for that class. It is deliberately textual: it
checks the artifact that is actually handed to the compiler rather than the
emitter's belief about what it wrote, so it still fires when the two disagree.
That disagreement is the bug it exists to catch -- see ``_emit_mx_dispatch`` in
``codegen.py``, whose runtime (m, x) switch emits a direct call for every
variant ``valid_variant_list`` reports as surviving. In the v2_regen pipeline
that set is reconciled against reality by the auto-promote / reference-taint
fixpoint. In the manifest-driven v2_emit pipeline there is no such fixpoint, so
the set is the analyzer's claim, and a claim that outruns what the owning bank
actually emitted dangles.

THE OPENING BRACE IN ``_DEFINITION_RE`` IS LOAD-BEARING. ``emit_bank`` gives
every bank file a forward-declaration block --

    RecompReturn Foo_M0X0(CpuState *cpu);

-- for each variant it CALLS, including variants owned by another bank, and
shards rebuild that block per translation unit. A pattern that does not require
the brace counts those prototypes as definitions, which makes every reference
look satisfied and reports a clean tree that does not link. That is not
hypothetical: it is how this check was first written, and it passed a Super
Metroid tree whose link failed on three undefined cross-bank variants.
"""

from __future__ import annotations

import pathlib
import re
from dataclasses import dataclass, field

# `RecompReturn Foo_M1X1(CpuState *cpu) {` -- a body, brace required.
_DEFINITION_RE = re.compile(
    r'^(?:RecompReturn|void)\s+([A-Za-z0-9_]+_M[01]X[01])\s*'
    r'\(CpuState\s*\*cpu\)\s*\{', re.MULTILINE)
# `Foo_M1X1(cpu)` -- a call. Requiring the argument keeps prose out: the
# emitter writes variant names into comments ("tail-call past end: into
# Foo_M0X1 at $8123") that are not references to anything.
_CALL_RE = re.compile(r'\b([A-Za-z0-9_]+_M[01]X[01])\s*\(\s*cpu\s*\)')


class LinkClosureError(RuntimeError):
    """A generated tree calls variants that nothing in it defines."""


@dataclass
class ClosureReport:
    gen_dir: pathlib.Path
    units: int = 0
    defined: set[str] = field(default_factory=set)
    called: dict[str, set[str]] = field(default_factory=dict)

    @property
    def dangling(self) -> dict[str, set[str]]:
        return {name: where for name, where in self.called.items()
                if name not in self.defined}

    @property
    def closed(self) -> bool:
        return not self.dangling

    def summary(self) -> str:
        return (f"{self.gen_dir}: {self.units} TU(s), {len(self.defined)} "
                f"variant definition(s), {len(self.called)} called, "
                f"{len(self.dangling)} dangling")

    def detail(self) -> str:
        lines = [self.summary()]
        for name, where in sorted(self.dangling.items()):
            lines.append(f"  undefined: {name}  <- "
                         + ", ".join(sorted(where)))
        return "\n".join(lines)


def scan_tree(gen_dir) -> ClosureReport:
    """Read every ``*.c`` in ``gen_dir`` as one program and relate the two."""
    gen_dir = pathlib.Path(gen_dir)
    report = ClosureReport(gen_dir=gen_dir)
    for path in sorted(gen_dir.glob("*.c")):
        source = path.read_text(encoding="utf-8", errors="replace")
        report.units += 1
        report.defined.update(_DEFINITION_RE.findall(source))
        for name in set(_CALL_RE.findall(source)):
            report.called.setdefault(name, set()).add(path.name)
    return report


def assert_closed(gen_dir, display_dir=None) -> ClosureReport:
    """Scan ``gen_dir``; raise :class:`LinkClosureError` if it will not link.

    Callers inside the emit pipeline should run this against the STAGING tree,
    before the atomic publish, so a tree that cannot link never replaces one
    that can. Pass ``display_dir`` there: the reader needs the path they
    generate into, not the temporary one the swap happens through.
    """
    report = scan_tree(gen_dir)
    if display_dir is not None:
        report.gen_dir = pathlib.Path(display_dir)
    if report.closed:
        return report
    raise LinkClosureError(
        "generated output is not link-closed -- "
        f"{len(report.dangling)} variant(s) are called but never defined.\n"
        f"{report.detail()}\n"
        "\nThe linker would report these as `undefined symbol` / LNK2019 "
        "after compiling every translation unit. Nothing was published; the "
        "previously generated tree is untouched.\n"
        "This is a recompiler defect, not a project misconfiguration: a body "
        "was emitted calling a variant the owning bank did not emit. Fix it "
        "in the emitter (see link_closure.py's module docstring), never by "
        "hand-editing generated C.")
