## 1. Preparation

- [x] 1.1 Confirm the Zoom Zoo route (`Down` on PICK TRACK) reaches `NOW PLAYING … ON ZOOM ZOO`. (`frames/zoomzoo/nowplaying.png`.)
- [x] 1.2 Create `work/ws-zoomzoo-spawn/` with `project/`, `logs/`, `frames/`, `metrics/`, and `patches/`.
- [x] 1.3 Copy the ws-hud project and build it with the debug server enabled. (Trace build at `work/ws-zoomzoo-spawn/project/UniracersSNESRecomp/build`.)
- [x] 1.4 Capture native and wide Zoom Zoo landmarks. (`frames/zoomzoo/composite-*`, `frames/native-3500.png`.)

## 2. Zoom Zoo recon

- [x] 2.1 Trace OAM slots and write PCs across the race; compare with the two-rider Dragster set. (Fixed slots 0/96–99; active=5 at 1800/2500/3500; no new record.)
- [x] 2.2 Capture OBJ-only and BG-only frames; identify any track object that is not BG. (OBJ-only at 3500 has only the player; track is BG.)
- [x] 2.3 Record per-frame DMA destinations/sizes; look for large CHR/tilemap stage swaps. (25/23/33 transfers, all 32-byte to `$2118`; no large CHR swap.)
- [x] 2.4 Dump WRAM landmarks and look for a record table, spawn anchor or activation distance. (Low-2K dumps at 1800/2500/3500; no record/allocator found.)
- [x] 2.5 Record any X-axis stage trigger and its direction conditions. (None; BG columns stream per frame.)

## 3. Applicability decision

- [x] 3.1 If Zoom Zoo has camera-spawned records, list the applicable P9–P13 items. (No camera-spawned records.)
- [x] 3.2 If all objects are BG and OAM is fixed, record Zoom Zoo N/A separately. (**Zoom Zoo N/A** with OAM/layer/DMA evidence.)
- [x] 3.3 Do not reuse the Dragster N/A without the Zoom Zoo evidence. (Separate second-track captures and report.)

## 4. Implementation if found

- [x] 4.1 P9 ordinary anchors: margin + 32 only for ordinary records. (N/A.)
- [x] 4.2 P10/P11: native pass first, widened ordinary-only pass, balanced synthetic call. (N/A.)
- [x] 4.3 P12: large-object activation distance margin + 32. (N/A.)
- [x] 4.4 P13: X-axis stage lead margin only; keep direction conditions complementary. (N/A.)
- [x] 4.5 Add independent spawn/stage switches if code changes. (N/A; no code.)

## 5. Spec correction

- [x] 5.1 Add the track-scoped N/A requirement to `recomp-ws-spawn`.
- [x] 5.2 State that the archived Dragster result is not engine-wide.
- [x] 5.3 Record the Zoom Zoo result separately.

## 6. Verification

- [x] 6.1 Verify 4:3 and WS-off Zoom Zoo frames are authentic. (No source change; native frame captured.)
- [x] 6.2 If code was added, verify RNG/progression and no pop/CHR swap. (N/A; no code.)
- [x] 6.3 Capture the Zoom Zoo object/staging evidence (or the BG-only N/A evidence). (`frames/zoomzoo/`, `frames/layers/obj-3500.png`.)
- [x] 6.4 Revert any change that alters progression/RNG or still pops. (N/A.)

## 7. Report

- [x] 7.1 Write `report.md` with the route, captures, OAM/layer/DMA evidence and result.
- [x] 7.2 State P9–P13 complete, partial, N/A for Zoom Zoo, or reverted. (**N/A for Zoom Zoo**, separately evidenced.)
- [x] 7.3 Verify all derived output stays under `work/ws-zoomzoo-spawn/`.
- [x] 7.4 Summarize and recommend the next change. (Final per-track visual validation/polish; no P9–P13 code for 1P.)
