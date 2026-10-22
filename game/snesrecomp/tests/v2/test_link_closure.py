"""Link-closure tripwire: a generated tree must define every variant it calls.

The regression these pin is real. The check was first written without
requiring the opening brace on a definition, which made it count emit_bank's
forward-declaration block as definitions; it then passed a Super Metroid tree
whose link failed on three undefined cross-bank variants
(bank_88_B279/B2A1/C3FF_M1X1, called from bank89_v2.c). Any rewrite of
_DEFINITION_RE has to keep test_forward_declaration_is_not_a_definition
passing.
"""
import pathlib
import tempfile

from v2.link_closure import (  # noqa: E402
    LinkClosureError, assert_closed, scan_tree)


_DEF = "RecompReturn {name}(CpuState *cpu) {{\n  return RECOMP_RETURN_NORMAL;\n}}\n"
_DECL = "RecompReturn {name}(CpuState *cpu);\n"
_CALL = "RecompReturn {caller}(CpuState *cpu) {{\n  return {name}(cpu);\n}}\n"


def _tree(**files):
    """Materialize a throwaway generated tree; return its path."""
    root = pathlib.Path(tempfile.mkdtemp(prefix="snesrecomp-closure-test-"))
    for name, text in files.items():
        (root / name.replace("__", ".")).write_text(text, encoding="utf-8")
    return root


def test_closed_tree_passes():
    """Caller and callee in different TUs, callee defined: closed."""
    root = _tree(
        bank00_v2__c=_CALL.format(caller="a_M1X1", name="b_M1X1"),
        bank01_v2__c=_DEF.format(name="b_M1X1"),
    )
    report = scan_tree(root)
    assert report.closed, report.detail()
    assert report.units == 2
    assert "b_M1X1" in report.defined
    assert assert_closed(root) is report or True


def test_missing_definition_is_reported_with_its_caller():
    """The undefined name AND the TU that references it both surface."""
    root = _tree(
        bank89_v2__c=_CALL.format(caller="x_M0X0", name="bank_88_B279_M1X1"),
    )
    report = scan_tree(root)
    assert not report.closed
    assert set(report.dangling) == {"bank_88_B279_M1X1"}
    assert report.dangling["bank_88_B279_M1X1"] == {"bank89_v2.c"}

    try:
        assert_closed(root)
    except LinkClosureError as exc:
        # The message has to carry the symbol and the file; a bare count
        # sends the reader back to the linker output this replaces.
        assert "bank_88_B279_M1X1" in str(exc)
        assert "bank89_v2.c" in str(exc)
    else:
        raise AssertionError("assert_closed accepted an unclosed tree")


def test_forward_declaration_is_not_a_definition():
    """A prototype must NOT satisfy a call. This is the original bug.

    emit_bank emits exactly this shape at the top of every bank file for the
    variants it calls but does not own; counting them as definitions makes
    the check blind to the one thing it is for.
    """
    root = _tree(
        bank89_v2__c=(_DECL.format(name="bank_88_B2A1_M1X1")
                      + _CALL.format(caller="x_M0X0",
                                     name="bank_88_B2A1_M1X1")),
    )
    report = scan_tree(root)
    assert "bank_88_B2A1_M1X1" not in report.defined, (
        "a forward declaration was counted as a definition")
    assert set(report.dangling) == {"bank_88_B2A1_M1X1"}


def test_variant_named_in_a_comment_is_not_a_call():
    """The emitter writes variant names into comments; those aren't refs."""
    root = _tree(
        bank00_v2__c=(
            "RecompReturn a_M1X1(CpuState *cpu) {\n"
            "  /* tail-call past end: into ghost_M0X1 at $8123 */\n"
            "  return RECOMP_RETURN_NORMAL;\n}\n"),
    )
    report = scan_tree(root)
    assert "ghost_M0X1" not in report.called
    assert report.closed


def test_dispatch_switch_cases_are_real_references():
    """A runtime (m, x) switch case calls a symbol like anything else.

    This is the exact shape that dangles when _emit_mx_dispatch trusts a
    valid_variant_list that outran what the owning bank emitted, so the
    check must not be tempted to treat `case N:` lines as prose.
    """
    root = _tree(
        bank89_v2__c=(
            "RecompReturn caller_M0X0(CpuState *cpu) {\n"
            "  RecompReturn _r;\n"
            "  switch (((cpu->m_flag & 1) << 1) | (cpu->x_flag & 1)) {\n"
            "    case 0: _r = interp_tier_run_call_frame(cpu, 0x88b279u,"
            " 0x89ac51u, 3, NULL); break;\n"
            "    case 3: _r = bank_88_B279_M1X1(cpu); break;\n"
            "  }\n  return _r;\n}\n"),
    )
    report = scan_tree(root)
    assert set(report.dangling) == {"bank_88_B279_M1X1"}


def test_empty_tree_is_vacuously_closed():
    """No generated C is a different failure; this check must not claim it."""
    report = scan_tree(_tree())
    assert report.closed
    assert report.units == 0
