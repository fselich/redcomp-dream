## 1. Preparation and preconditions

- [x] 1.1 Read `docs/UNIRACERS_BG1_RING_FINDINGS.md`, `openspec/changes/uniracers-ws-leading-edge/report.md` and this change's `design.md` (framework facts table).
- [x] 1.2 Create `work/ws-ghost-lane/{logs,frames,metrics,patches,scripts}/`.
- [x] 1.3 Build `build-local/` with both local patches; confirm native Dragster 2500 SHA-256 `3417f91a…df44`, and that `ring` reproduces the leading-edge report numbers on Dragster.
- [x] 1.4 Check CHR stability: hash the BG1 character region (`PPU_bgTileAdr(ppu, 0)` area) once per race frame on Dragster and Zoom Zoo; record whether it changes. If it does, adapt D4 before continuing. *(It changes at race entry: Dragster 1381–1430, Zoom Zoo 1404–1510. Added a 30-frame stable warm-up gate before ghost serving.)* *(Preliminary, from `uniracers-ws-map-decode`: CHR `0x2000–0x5FFF` byte-identical in Dragster dumps at 2500/3500/5000; confirm per frame and on Zoom Zoo.)*
- [x] 1.5 Record baseline `ring` metrics: CPU seconds over 2,500 frames, `SNESRECOMP_FRAME_TIMING` CSV, `SNESRECOMP_STATE_TRACE` file. *(`logs/ring-timing.*`, `logs/ring-dragster.trace`.)*

## 2. Framework patch

- [x] 2.1 Add `snes_beam_frame_driver_get_exec(uint32_t *pc, bool *wai)` and `snes_beam_frame_driver_set_exec(uint32_t pc, bool wai)` for `s_resume_pc` / `s_wai_halted` in `beam_frame_driver.{h,c}`.
- [x] 2.2 Save the diff as `work/ws-ghost-lane/patches/beam-exec.diff`; confirm native hash unchanged.

## 3. Ghost lane

- [x] 3.1 Add mode `ghost` (value 3) to `WsOffwindowMode()`; it implies all Option A behaviour.
- [x] 3.2 Add env parameters `SNESRECOMP_WS_GHOST_AHEAD` (4), `_MAXSTEPS` (2), `_RESYNC` (600, raised from 60 after p99 measurement).
- [x] 3.3 Allocate two snapshot buffers with `RtlRollbackSnapshotBound()` once. *(Realloc if the bound grows.)*
- [x] 3.4 Implement the host-state save/restore set from design D3 (beam exec, frame counter, audio cursor, speculative flag, `s_cull_*`, render/hook globals). Keep a separate copy of the beam exec and `s_cull_*` for the ghost. *(Also the raster journal and the whole `Ppu` struct; see report §3.)*
- [x] 3.5 Implement `GameGhostAfterRunFrame()` in `game_rtl.c` following design D1 pseudocode; inputs from `g_snes->input1_currentState | input2_currentState << 12`.
- [x] 3.6 Register it as `.after_run_frame` in `src/main.c`.
- [x] 3.7 Eligibility: wide + live race + mode ghost; disabled under netplay and when `snes_runahead_frames() > 0`.
- [x] 3.8 Resync triggers: input change, Option A scene reset, eligibility toggle, every `_RESYNC` frames.

## 4. Harvest and serving

- [x] 4.1 Ghost-only line hook writing `s_gh[32][32]` (world key + map word), with its own unwrap seeded from the real lane at resync.
- [x] 4.2 Ghost draw via `snes_beam_frame_driver_draw_ppu_frame()` with margin hook NULL and the probe suppressed (frame-counter sentinel).
- [x] 4.3 Scene guard: skip harvest if ghost line-1 map base/BGMODE differ from the race scene. *(Also per-line map-base guard and a CHR-stability gate.)*
- [x] 4.4 Extend `WsOffwindowTile`: ring verified → ring; else ghost world match → ghost word (log `masked=0`); else mask.

## 5. Invariants (stop here if any fails)

- [x] 5.1 Native 4:3 hash unchanged with the switch unset.
- [x] 5.2 `SNESRECOMP_STATE_TRACE`, `ring` vs `ghost`, 2,500 Dragster frames: identical. If not, record the first divergent frame and find the unsnapshotted static.
- [x] 5.3 Frame 2500 WRAM/VRAM/CPU/DMA dumps and native-window pixels identical to `ring`.
- [x] 5.4 No audio glitch/duplication in a real-time (non-headless) run; note it in the report. *(Waived, not tested: by decision the ghost lane stays experimental and is not promoted. The lane rewinds the audio producer cursor like the framework run-ahead, so no ghost samples should reach the consumer; `source` is the production default.)*

## 6. Validation

- [x] 6.1 Score Dragster →, Dragster ←, Zoom Zoo, Bowl, Switcher, Monster with the probe + `--mask-log --served`: 0 wrong on every run.
- [x] 6.2 Report leading-side served/masked vs `ring` per track; target ≥ 95 % served on Dragster →. *(99.84 %.)*
- [x] 6.3 Pixel check: sample ghost-served cells and compare the margin 16×16 block with the native block when that world cell becomes visible. *(Map word exact; CHR static after warm-up; 15/256 RGB pixels differ only in animated palette shades, geometry matches. See report §7.)*
- [x] 6.4 Input-divergence run (frequent direction changes): 0 wrong; record coverage. *(`route-dragster-bounce.txt`: 0 wrong, leading served 86.5 %.)*
- [x] 6.5 Capture wide frames `ring` vs `ghost` for visual review (Dragster 2500, one per other track).

## 7. Cost

- [x] 7.1 CPU seconds over 2,500 frames vs `ring`; average ≤ 5 ms/frame extra. *(≈+2.3 ms/frame mean (end-to-end +1.9–2.9 ms).)*
- [x] 7.2 p99 and worst `hook_ms` from `SNESRECOMP_FRAME_TIMING`. *(mean 2.33 ms, p99 9.6 ms, max 60.9 ms one-off.)*
- [x] 7.3 If over budget, try in order: adaptive K from scroll speed; skip steps when no leading cell is missing; harvest via journal walk without `ppu_runLine` (prove identical PPU/DMA state first). *(CPU budget passes; the headless wall rate is ~42 fps and the cost is the snapshot/restore traffic, not the ghost raster. A lighter snapshot strategy is the next change; journal-walk alone will not recover 60 fps.)*

## 8. Report and decision

- [x] 8.1 Write `report.md`: method, invariants, per-track wrong/served/masked, coverage vs `ring`, cost, captures, patches.
- [x] 8.2 Go/no-go for `ghost`; recommend the default (`ring` or `ghost`). *(go; recommend `ghost` in a follow-up, `ring` fallback.)*
- [x] 8.3 Update `docs/UNIRACERS_BG1_RING_FINDINGS.md`, the handoff and `work/README.md`.

## Closure

Closed on 2026-10-22 as an experimental reference. Real-time audio (5.4) was
waived by decision, not tested. `ghost` remains available behind
`SNESRECOMP_WS_OFFWINDOW=ghost`, but `source` is the production default; ghost
is not promoted because of its cost.
