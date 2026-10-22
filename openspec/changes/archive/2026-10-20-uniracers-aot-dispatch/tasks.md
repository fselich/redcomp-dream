## 1. Preparation

- [x] 1.1 Record the Phase D baseline: camera range 19,201 interpreted instructions, the 2-frame offset, and the promoted project paths.
- [x] 1.2 Create `work/aot-dispatch/` with `project/`, `patches/`, `logs/`, `frames/`, `coverage/`, and `metrics/`.
- [x] 1.3 Copy the promoted project into the dispatch workspace and verify the framework pin and ROM identity.
- [x] 1.4 Re-run the camera range check on the copy to confirm the 19,201 baseline before changing anything.

## 2. Diagnosis

- [x] 2.1 Add a temporary trace to the interpreter bridge logging target PC, live bank, M/X, call kind, and AOT lookup result.
- [x] 2.2 Run a short race window (≈1,800 frames) and capture the first `$81AE7D` entry and the dispatch decision.
- [x] 2.3 Classify the cause: bank alias, indirect call, fall-through, or intentional safety skip.
- [x] 2.4 Save the trace and the diagnosis under `work/aot-dispatch/logs/` and `metrics/`.

## 3. Fix

- [x] 3.1 Implement the smallest reversible fix for the classified cause, with an exact `(pc, M, X)` guard and interpreter fallback.
- [x] 3.2 Save the patch or project-local change under `work/aot-dispatch/patches/` with a revert note.
- [x] 3.3 If no safe fix exists, record the blocker and stop without forcing dispatch. (N/A — a project-local boundary fix was safe.)

## 4. Build and replay

- [x] 4.1 Rebuild the project with the fix; record success, duration and executable size.
- [x] 4.2 Run 3,000 frames with frames at 2400 and 3000 plus tier2 coverage.
- [x] 4.3 Measure the `$81AE7D–$81AE9F` interpreted-instruction count before and after.
- [x] 4.4 Compare frames (exact or stable aligned offset) and check for tripwires or regressions in the other promoted targets.
- [x] 4.5 If replay regresses, revert the fix and record the first divergent frame. (N/A — aligned equivalence holds at k=3.)

## 5. Timing policy

- [x] 5.1 Re-measure the frame offset after the fix.
- [x] 5.2 Record the P16 policy: permanent aligned-frame equivalence, or exact cycle accounting as a separate blocked change.

## 6. Report

- [x] 6.1 Write `report.md` with the diagnosis, fix, camera before/after counts, replay result, timing policy, and remaining widescreen blockers.
- [x] 6.2 Apply the acceptance criteria and state whether the fix is retained or reverted.
- [x] 6.3 Verify no generated C, frames, coverage journals, or ROM data exist in the repository; all derived output remains under `work/aot-dispatch/`.
- [x] 6.4 Summarize the outcome and the recommended next change.
