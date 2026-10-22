# Uniracers AOT dispatch gap (Phase D.1) — report

Date: 2026-10-20
Change: `uniracers-aot-dispatch`
Outcome: **fixed**. The camera range is now fully AOT. The cause was a **function
boundary misclassification**, not a bridge bug: `$81AE7D` is a branch join, and
the containing routine starts at `$81AE59`. No framework patch was needed.

## Verdict

- `CameraUpdate` at `$81:AE59` (M0X0) is the real function entry; `$81AE7D` is
  the target of a `BEQ` from `$81AE6E` inside that routine.
- The routine is called by `JSR $AE59` from `$81:A5B1`; once the correct entry is
  promoted, the interpreter bridge bounces it (`aot_call`) every frame.
- Camera range `$81AE7D–$81AE9F` interpreted instructions: **19,201 → 0**.
- Total interpreted instructions: **37,722,540 → 37,653,280** (−69,260 vs
  Phase D; −3,307,494 / **−8.07%** vs the original baseline).
- Replay is visually identical with a stable **3-frame** offset; no tripwires.
- Fix is a single project-local symbol change; the pinned framework is untouched.

## Diagnosis

The Phase D `dispatch_log` was empty, so the camera never reached
`cpu_dispatch_pc`. The trace build (`SNESRECOMP_ENABLE_TRACE=ON`) and the TCP
`interp_edges` command showed the real transfer:

Before the fix:

```
from 0x81AE6E -> to 0x81AE7D  kind=branch  op=0xF0 (BEQ)  sp=0x01ED
count=1 per frame, frames 1717..1742
```

`$81AE7D` is a join after a conditional block, not a function entry. The
bridge is correct not to bounce branch targets.

Static disassembly confirmed the containing routine:

```
81AE59: LDA $04F9
81AE5C: BEQ $AE6B
...
81AE6E: BEQ $AE7D       ; join into the camera update
...
81AE7D: LDA $7E202A     ; camera scroll update
...
```

and the caller:

```
81A5B1: JSR $AE59
```

After promoting `$81AE59`, the edge ring shows:

```
from 0x81A5B1 -> to 0x81AE59  kind=aot_call  op=0x20 (JSR)  sp=0x01ED
count=1 per frame
```

The bridge now enters the AOT body every frame.

## Fix

`recomp/symbols.toml`:

```diff
- name = "CameraScroll"
- addr = "AE7D"
+ name = "CameraUpdate"
+ addr = "AE59"
  bank = 0x81
  entry_m = 0
  entry_x = 0
  emit = true
```

Saved as `work/aot-dispatch/patches/camera-boundary.diff`. No framework files
were modified. Regeneration: 1 s. Build: exit 0; executable 1,873,760 bytes.

## Measurement

| Metric | Baseline | Phase D | Dispatch fix | Fix vs Phase D |
|---|---:|---:|---:|---:|
| Camera range `$81AE7D–$81AE9F` | 19,175 | 19,201 | **0** | −19,201 |
| Total interpreted instructions | 40,960,774 | 37,722,540 | 37,653,280 | −69,260 |
| Discoveries | 594 | 542 | 541 | −1 |
| Hot instruction PCs | 14,017 | 12,540 | 12,442 | −98 |

The camera function `$81AE59` no longer appears in the interpreted hot list or
the discovery set.

## Replay equivalence

The replay was compared against the preserved baseline window:

| Baseline frame | CRC | Fix frame | CRC |
|---:|---|---:|---|
| 2395 | `b07e14ba` | 2398 | `b07e14ba` |
| 2396 | `39aa5139` | 2399 | `39aa5139` |
| 2399 | `ed9e0548` | 2402 | `ed9e0548` |
| 2400 | `7f3f69a3` | 2403 | `7f3f69a3` |
| 2404 | `7af5d4d7` | 2407 | (outside window) |

Fix frame **N = baseline frame N−3**. The offset is stable across the window;
the scene, timer and HUD are identical. No tripwires, crashes or warnings.

The offset grew from 2 (Phase D) to 3 frames with the additional AOT body. The
timing shift is cumulative with the amount of AOT, so a fixed `k` cannot be
hard-coded across builds.

## Timing policy

**Aligned-frame equivalence, with `k` measured per build**, is the P16 policy
for widescreen work. Exact fixed-frame hashes are not achievable without a
separate cycle-accounting change. The aligned gate compares promoted frame N
with baseline frame N−k across a window, requiring identical visual/state
content and no tripwires.

Exact timing remains a separate framework blocker if netplay or bit-identical
4:3 is ever required.

## Widescreen readiness

The camera is now an AOT-reached anchor:

- `CameraUpdate` `$81:AE59` is generated C and can host the camera/scroll hook
  (P1/P4/P-U2).
- The camera scroll mirrors `$7E:2026/$202A/$202E/$2032` are still the values the
  PPU uses; P1 still requires taking the rendered phase from the PPU for margins.

Remaining blockers are unchanged:

- P5/P6 gameplay discriminator.
- P7 cull windows and P8 OAM reject compare.
- P9–P13 spawn/activation/staging.
- P-U1 HDMA table builder.
- P16 now uses aligned frames with per-build `k`.

## What this does not prove

- That 16:9, culling, spawn or HUD behavior works.
- That the aligned-frame offset is acceptable for netplay or input latency.
- That the camera hook itself is implemented; only its anchor is now native.
- That the remaining engine paths (cull/OAM/spawn/HDMA builder) are close.

## Recommendation

Next change: **`uniracers-widescreen-spike`**, following the doctrine order:

1. P16 aligned baseline (k=3 now).
2. P5/P6 gameplay discriminator.
3. P1–P4 + P-U1/P-U2 backgrounds/HDMA.
4. P2c HUD.
5. P7/P8 cull + OAM.
6. P9–P13 spawning/staging.
7. P14–P16 verification.

The camera anchor is ready; the HDMA builder and the cull/OAM/spawn paths are
the next discovery work.

## Artifacts

- Workspace: `work/aot-dispatch/` (gitignored).
- Patch: `patches/camera-boundary.diff`.
- Trace: `metrics/interp-edges-camera.json`, `metrics/interp-edges-fix.json`.
- Metrics: `metrics/fix-ingest.json`, `metrics/fix-delta.txt`,
  `metrics/baseline.json`.
- Frames: `frames/fix-window/`, `frames/fix-window.csv`,
  `frames/fix-3000.ppm`.
- Coverage: `coverage/uniracers/*.json`, `*.jsonl`.
- This report contains no generated C, no ROM bytes, and no game data.
