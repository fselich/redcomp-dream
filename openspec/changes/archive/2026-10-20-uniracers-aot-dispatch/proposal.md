# Proposal: Uniracers AOT dispatch gap (Phase D.1)

## Why

Phase D cut interpreted instructions by 7.91%, but `CameraScroll` is emitted and
registered in `dispatch_v2.c` and still runs entirely in the interpreter:
19,201 instructions in `$81AE7D–$81AE9F`. The camera is the anchor for future
widescreen hooks (P1/P4/P-U2); without a reached AOT body there is no
generated-C anchor. The only direct call site is `$9D:83E2` (`JMP $AE7D`), so
the real entry is indirect; the interpreter's dispatch bridge is the suspect.

## What Changes

- Trace the actual entry path to `$81AE7D`: call site, bank/M/X, and the
  interpreter's dispatch decision.
- Fix the minimal cause: indirect-call bridge, LoROM bank alias, or fall-through
  entry. Prefer a small framework-local patch; otherwise a project-level
  dispatch hint. Record the patch for upstreaming.
- Rebuild and replay the 3,000-frame route; verify the camera range's
  interpreted instructions drop (target near zero) and frames remain
  aligned-equivalent (2-frame offset) or exact.
- Record the P16 timing policy: exact cycle fix vs permanent aligned-frame gate.
- Report the diagnosis, fix, measurement, policy, and remaining widescreen
  blockers.

## Non-goals

- Widescreen, culling, HDMA builder, spawn/staging, audio.
- Upstreaming the framework patch (record it; upstream later).
- Full playability.

## Capabilities

### New Capabilities

- `recomp-aot-dispatch`: diagnosis and repair of interpreter-to-AOT dispatch
  gaps for indirectly reached functions, with replay proof and a timing-policy
  decision.

### Modified Capabilities

None.

## Impact

- Work under `work/aot-dispatch/`; the baseline and promoted builds in
  `work/boot-spike/` and `work/aot-promotion/` are preserved.
- May patch the framework submodule locally at the pinned commit; the patch is
  recorded and reversible.
- No derived data in the repository.
