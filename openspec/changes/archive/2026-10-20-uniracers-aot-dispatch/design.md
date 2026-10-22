# Design: Uniracers AOT dispatch gap (Phase D.1)

## Context

Phase D promoted 16 roots / 39 AOT variants and cut interpreted instructions by
7.91%. `FrameDisplay` and 10 of 11 top race targets are reached. `CameraScroll`
is different: `CameraScroll_M0X0` is declared, emitted in `bank81_v2.c`, and
registered in `dispatch_v2.c` at `0x81AE7D`, yet the whole routine remains
interpreted (19,201 instructions, 1,477 per race frame across 13 PCs).

Static search finds only one direct reference: `$9D:83E2` (`JMP $AE7D`), outside
the analyzed banks. The real entry is therefore indirect (or aliased), and the
interpreter's bridge is not selecting the AOT body. The camera is the future
widescreen anchor, so this gap blocks P1/P4/P-U2.

## Goals / Non-Goals

**Goals:**

- Identify the exact entry path and why the AOT dispatch misses it.
- Apply a minimal, reversible fix.
- Prove the camera range becomes AOT-reached (interpreted count drops).
- Preserve replay equivalence (exact or the documented 2-frame aligned gate).
- Record the P16 timing policy.

**Non-Goals:**

- Widescreen, culling, HDMA builder, spawn/staging, audio.
- Upstreaming the framework patch in this change.
- Full playability.

## Decisions

### D1 — Diagnose before patching

Add a temporary trace in the work copy's interpreter bridge that records, for a
target PC: live bank/PC, M/X, call kind (JSR/JSL/JMP/fall-through), whether the
AOT table has a matching entry, and the canonicalized key used for the lookup.
Run a short race window (1,800 frames) to capture `$81AE7D` and stop.

*Alternative:* guess from static disassembly. Rejected: the only direct call site
is outside the covered banks and does not explain the live PC.

### D2 — Fix the smallest cause found

Expected causes, in order of likelihood:

1. **Bank alias** — the live target is `$01:AE7D` while the AOT table is keyed
   `$81:AE7D` (LoROM mirror). Fix: canonicalize LoROM mirror banks in the
   dispatch lookup.
2. **Indirect call** — the bridge only consults AOT for direct calls. Fix:
   consult the AOT table for indirect JSR/JSL/JMP targets with a proven
   variant.
3. **Fall-through / non-call entry** — execution reaches `$81AE7D` without a
   call. Fix: add a dispatch check at the target entry or a project-level hint.
4. **Intentional safety skip** — the bridge refuses because the variant is not
   proven. Fix: add a narrowly scoped project hint, or record the blocker.

The fix must include a guard: only exact `(pc, M, X)` variants dispatch, and the
interpreter remains the fallback.

*Alternative:* force the camera caller to AOT so the generated C emits a direct
call. Rejected unless diagnosis shows the caller is the only path; it does not
generalize to other indirect entries.

### D3 — Minimal, reversible framework patch

If the fix is in the shared interpreter, keep it as a small patch against the
pinned submodule, saved as `work/aot-dispatch/patches/*.diff`. Do not silently
diverge from the pin: the report records the patch, the reason, and the upstream
candidate. If the fix is project-local, keep it in `recomp/` or `tools/`.

### D4 — Replay gate

Use the Phase C route. Run 3,000 frames and capture frames at 2400 and 3000 plus
tier2 coverage. Accept if:

- the camera range's interpreted instructions drop materially (target: near
  zero; record the actual);
- frames match exactly or at the documented stable offset (2 frames in Phase D);
- no new tripwire or crash appears;
- no unrelated target regresses.

If the camera remains interpreted, record the diagnosed cause and stop; do not
claim a fix.

### D5 — P16 timing policy

Phase D measured a stable 2-frame offset. This change records one policy:

- **Aligned gate (recommended):** P16 compares promoted N with baseline N−2
  across a window, with identical visual/state content. Exact fixed-frame
  hashes are not required for widescreen work.
- **Exact timing:** open a separate framework change to align AOT cycle
  accounting; until then, P16 is blocked.

The policy is a decision, not an implementation of cycle accounting.

### D6 — Workspace

```
work/aot-dispatch/
├── project/       # copy of the promoted project (or a patched work copy)
├── patches/       # framework diff, if any
├── logs/          # trace, build, replay
├── frames/        # replay frames
├── coverage/      # tier2 capture
└── metrics/       # camera-range counts and comparisons
```

### D7 — No widescreen code

No margin sampling, HUD, cull, OAM, spawn, or HDMA-table changes.

## Risks / Trade-offs

- **The bridge fix affects other indirect calls** → replay the full route and
  compare discoveries; keep the fix behind an exact-variant guard.
- **The live entry is a bank alias** → canonicalization must not fold SA-1 or
  HiROM aliases; Uniracers is LoROM only.
- **AOT dispatch is intentionally conservative** → forcing it may violate
  correctness; use a narrowly scoped hint and keep the interpreter fallback.
- **The fix changes timing further** → re-measure the offset; the aligned gate
  accommodates a stable k.
- **The patch diverges from the pinned submodule** → record the diff and the
  upstream candidate; the work copy, not the pin, carries it.
- **Camera may be entered by multiple paths** → measure the full range, not one
  call site.

## Open Questions

- Is the live entry a bank alias, an indirect call, or a fall-through?
- Does the interpreter bridge intentionally skip indirect entries?
- Will the fix change the 2-frame offset?
- Is aligned-frame comparison acceptable as the permanent P16 policy, or is
  exact cycle accounting required for netplay?
