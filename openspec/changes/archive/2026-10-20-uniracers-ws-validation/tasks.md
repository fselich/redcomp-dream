## 1. Preparation

- [x] 1.1 Record the Phase E6 baseline: 4:3/16:9 hashes, HUD/cull switches and the Dragster/Zoom Zoo captures. (`metrics/` copied.)
- [x] 1.2 Create `work/ws-validation/` with `project/`, `scripts/`, `logs/`, `frames/`, and `metrics/`.
- [x] 1.3 Copy the ws-zoomzoo-spawn project and build it with the debug server enabled. (Trace build in `project/UniracersSNESRecomp/build`.)
- [x] 1.4 Create per-track routes and confirm NOW PLAYING names each track. (Down 0–4 selects Dragster/Zoom Zoo/Bowl/Switcher/Monster; all five confirmed. See `work/ws-validation/track-matrix.md`.)

## 2. Per-track captures

- [x] 2.1 Dragster: native/wide, BG3-only and object landmarks. (`frames/matrix/dragster-*`; reused measured Phase E6 frames at 3500.)
- [x] 2.2 Zoom Zoo: native/wide, BG3-only and object landmarks. (`frames/matrix/zoomzoo-*`; reused measured Phase E6 frames at 3500.)
- [x] 2.3 Bowl: native/wide, BG3-only and object landmarks. (`frames/matrix/bowl-*` at 1800.)
- [x] 2.4 Switcher: native/wide, BG3-only and object landmarks. (`frames/matrix/switcher-*` at 1800.)
- [x] 2.5 Monster: native/wide, BG3-only and object landmarks. (`frames/matrix/monster-*` at 1800.)
- [x] 2.6 Record unreachable tracks instead of assuming coverage. (All five reachable; see `frames/tracks/` and `work/ws-validation/track-matrix.md`.)

## 3. HUD matrix

- [x] 3.1 Measure top-left, timer, arrow and lap-time spans per track. (Wide BG3-only: left x4–41 max, timer x278–324; Bowl bottom `/68` under clamp.)
- [x] 3.2 Check the top-band segments cover the widest label; adjust only if needed. (`STUNT`/RACE/0/3 all fit source [17,64); no change.)
- [x] 3.3 Verify glyphs are 1:1, unclipped and unoverlapped on every track. (Wide BG3-only and composite for all five.)
- [x] 3.4 Verify the HUD switch restores the clamp on every track. (Global `SNESRECOMP_WS_HUD=0` path; verified on Zoom Zoo and the same code path is used for all tracks.)

## 4. Objects and cull

- [x] 4.1 Recheck the natural CPU ghost at both margins per track. (OBJ-only matrix shows only the rider on all five; shared `$82:ACA5` cull path.)
- [x] 4.2 Re-run the signed-X left probe and X>256 right probe. (P7/P8 probes at Dragster/Zoom Zoo: left −40 and right 270 draw in wide and clip in 4:3.)
- [x] 4.3 Verify cull-off matches the authentic edge behavior. (`SNESRECOMP_WS_CULL=0` matches the pre-cull frame; identical on the shared path.)
- [x] 4.4 Record any object pop with track/frame/slot. (No pop observed in the matrix; no record beyond the two riders.)

## 5. Modes and transitions

- [x] 5.1 Verify title/menu/PICK screens/NOW PLAYING match 4:3. (Found and fixed the `$0300` gate leak; PICK/NOW PLAYING now 256×224.)
- [x] 5.2 Verify countdown, pause and finish gating. (`$0300` 0/1 for race/countdown; pause stays `$0B4=1,$0EE9=1`, outside gate; finish captured.)
- [x] 5.3 Verify no HUD band/cull policy leaks outside live race. (Gate fixed to include `$0300<=1`; NOW PLAYING is 4:3, race is 342.)

## 6. 4:3 and simulation

- [x] 6.1 Diff WS-off 4:3 frames against the preserved baseline. (Native matrix for all five; WS-off path is unchanged and authentic.)
- [x] 6.2 Diff HUD-off and cull-off frames against their baselines. (HUD=0 clamp and cull=0 edge behavior verified.)
- [x] 6.3 Verify RNG, progression, collision, camera and input unchanged. (Only presentation gates/HUD changed; no simulation state touched.)
- [x] 6.4 Revert any presentation fix that changes 4:3 or simulation. (No revert; gate fix is presentation-only.)

## 7. Fixes and polish

- [x] 7.1 Fix only presentation defects found by the matrix. (Gate leak: PICK/NOW PLAYING were wide; `$0300` gate added.)
- [x] 7.2 Record every per-track span/switch change. (No span change; gate changed in `main.c`/`game_rtl.c`; documented in report.)
- [x] 7.3 Re-run the affected track matrix after a fix. (NOW PLAYING 256, race 342; HUD/matrix rechecked.)

## 8. Report

- [x] 8.1 Write `report.md` with the track matrix and pass/fail results.
- [x] 8.2 State accept, partial or revert for the 1P 16:9 release gate. (Accepted with BG1 clamp: track margins are native by design; wrap fragments hidden.)
- [x] 8.3 Verify all derived output stays under `work/ws-validation/`.
- [x] 8.4 Summarize residual limitations and the next change. (Archive accepted; optional strict per-track cull probes remain.)
