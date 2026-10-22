## Why

`uniracers-ws-leading-edge` shipped Option A (`SNESRECOMP_WS_OFFWINDOW=ring`).
The trailing 16:9 margin now shows authentic track. The **leading** margin is
still background, because the game writes each leading BG1 cell just in time
(~1 cell/frame). Option C, sourcing those cells by decoding the `$7F` segment
tables, was no-go: the segment list is undecoded.

There is a route that needs no decoding. Run a **ghost copy** of the game a few
frames ahead of the real one, harvest the BG1 ring cells the game itself writes
there, key them by world cell, and serve them in the leading margin of the real
frame. An offline measurement on the probe captures shows a 4-frame lead covers
**100 %** of the Dragster leading margin at full speed.

## What Changes

- New mode `SNESRECOMP_WS_OFFWINDOW=ghost`: Option A plus a ghost lane that
  keeps a second machine state N frames ahead (default 4) and harvests
  world-keyed BG1 cells for the leading margin.
- Steady-state cost target: about 1 extra emulated frame per real frame
  (estimate +3–4 ms), not a full N-frame run-ahead every frame.
- Small framework patch: expose save/restore of the beam driver's execution
  state (`s_resume_pc`, `s_wai_halted`). These are not in the rollback snapshot.
- Validation: 0 wrong served cells on the five tracks, real frames
  byte-identical to `ring` mode, leading-edge coverage, cost.

## Capabilities

### New Capabilities

- `recomp-ws-ghost-lane`: speculative ghost lane that harvests world-keyed BG1
  cells for the leading 16:9 margin, presentation-only, behind
  `SNESRECOMP_WS_OFFWINDOW=ghost`.

### Modified Capabilities

None. `recomp-ws-leading-edge` (Option A) is reused unchanged; `ghost` is a new
mode on the same switch.

## Impact

- `work/ws-native-map-spike/project/UniracersSNESRecomp/src/game_rtl.c`: ghost
  lane, harvest table, extended margin hook.
- `.../src/main.c`: register `.after_run_frame`.
- `.../snesrecomp/runner/src/beam_frame_driver.{c,h}`: exec-state accessor
  (local framework patch, next to `bgprobe.diff` and `ws-leading-edge.diff`).
- Work artifacts under `work/ws-ghost-lane/` (gitignored, ROM-derived).
- Offline/1P only; disabled under netplay and when the framework run-ahead
  (`RunAhead` > 0) is active.
- Clamp stays the default. `ring` stays the recommended default until `ghost`
  passes.
