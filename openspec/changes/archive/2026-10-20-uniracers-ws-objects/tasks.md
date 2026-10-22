## 1. Preparation

- [x] 1.1 Record the ws-discriminator baseline: 4:3/16:9 hashes, live-race gate, and aligned offset. (Hashes in `metrics/baseline-hashes.txt`; 4:3 at 3500 is the wide central crop at +43.)
- [x] 1.2 Create `work/ws-objects/` with `project/`, `patches/`, `logs/`, `frames/`, and `metrics/`.
- [x] 1.3 Copy the ws-discriminator project and build it with the debug server/OAM rings enabled. (Trace build from the ws-hdma copy.)
- [x] 1.4 Capture the Dragster race (second entry in PICK TRACK; the menu reads `RACING ON DRAGSTER`) with objects near the margins as the baseline. (Frame 1739: CPU ghost at the right edge; 3500: Dragster curve.)

## 2. Object path recon

- [x] 2.1 Identify object vs HUD OAM slots from the OAM rings and captures. (HUD BG3; OAM 98 player, 99 CPU ghost, 96/97 parked, 0 parked.)
- [x] 2.2 Trace the OAM emitter and record the writer PC and signed-X encoding. (Staging `$00:1501–$1510`, uploader `$82:D2DC`, object writes `$82:AD5D`/`$82:AE1C`, signed 9-bit X.)
- [x] 2.3 Locate the cull comparisons and record each routine's base window. (Horizontal `$0421/$0423` = [-49,256] at 3500, compares at `$82:AD19`/`$82:ADE7`; vertical [-41,112].)
- [x] 2.4 Save the OAM/cull trace and the object-path map. (`work/ws-objects/object-path.md` + logs.)

## 3. P7 cull widening

- [x] 3.1 Shift the horizontal cull bounds by the margin (left −49→−92, right 256→299) and keep the vertical window. (`game_rtl.c`; observed `$0421=0xFFA4`, `$0423=0x012B` in the wide run.)
- [x] 3.2 Keep each rider's own base; do not unify the horizontal or vertical windows. (Base is captured on entry/section change; vertical `[-41,112]` unchanged.)
- [x] 3.3 Build and verify cull does not alter spawn/progression. (Read-site audit: `$0421/$0423` are only read by five `CMP` sites in bank 02; no spawn/AI reads. Cull-on/off WRAM delta is only those two words plus pre-existing run noise.)

## 4. P8 OAM validation

- [x] 4.1 Confirm the widened cull admits the CPU ghost beyond native X=256. (Bounds are `[-92,299]`; the five `CMP`s now admit X=270. The presentation probe renders X=270 in the wide right margin; 4:3 is unchanged/invisible.)
- [x] 4.2 Validate `PpuDecodeOamX` draws a signed negative-X rider in the left margin; add no redundant reject compare. (Probe X=−40/raw 472: wide shows the ghost in the left margin, 4:3 shows only a 1 px sliver. No new compare added.)
- [x] 4.3 Build and verify objects straddle both margins. (Left probe X=−40 and right probe X=270; plus the natural cull-on case at 1741.)

## 5. Kill switches

- [x] 5.1 Add an independent runtime gate for cull widening. (`SNESRECOMP_WS_CULL`, default on.)
- [x] 5.2 Add an independent runtime gate for OAM widening. (Not applicable: no OAM behavior was changed; `PpuDecodeOamX` already handles signed X.)
- [x] 5.3 Verify each gate can be disabled independently with 4:3 fallback. (Cull=0 at 1741 matches the pre-change wide hash; WS off matches the 4:3 baseline hash.)

## 6. Verification

- [x] 6.1 Verify the flag-off 4:3 baseline still aligns and simulation is unchanged. (4:3 frame 1741 SHA-256 identical before/after; WS-off cull words untouched.)
- [x] 6.2 Capture the Dragster curve with objects at the left and right margins. (Frame 3500 with probes: left and right margin riders; native 4:3 clips both. The track runs left; the probes are OAM objects, not the track.)
- [x] 6.3 Record any remaining pop-in or clipping for P9–P13. (No spawn pop-in observed; the native cull hid the ghost at 1741. Spawn/activation/staging remain P9–P13.)
- [x] 6.4 If cull affects progression or either margin still clips, revert and record it. (No progression change: only `$0421/$0423` + run noise in the WRAM diff; both margins draw in the wide probe. No revert.)

## 7. Report

- [x] 7.1 Write `report.md` with the object path, cull/OAM patches, kill switches, margin evidence, 4:3 result and remaining gaps.
- [x] 7.2 Apply the acceptance criteria and state whether P7/P8 are complete, partial or reverted. (P7 complete; P8 validated for both margins; 4:3 unchanged.)
- [x] 7.3 Verify no generated C, frames, or ROM data exist in the repository; all derived output remains under `work/ws-objects/`.
- [x] 7.4 Summarize the outcome and the recommended next change. (Recommend `uniracers-ws-spawn` for P9–P13.)
