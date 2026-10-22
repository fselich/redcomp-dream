## 1. Preparation

- [x] 1.1 Record the Phase E5 baseline: 4:3/16:9 hashes, HUD clamp, and the Dragster/Zoom Zoo frame set. (`metrics/native-baseline-hashes.txt`, `metrics/oam-summary.txt`.)
- [x] 1.2 Create `work/ws-hud/` with `project/`, `patches/`, `logs/`, `frames/`, and `metrics/`.
- [x] 1.3 Copy the ws-spawn project and build it with the debug server enabled. (Trace build at `work/ws-hud/project/UniracersSNESRecomp/build`.)
- [x] 1.4 Capture native and wide HUD landmarks at race start, ghost pass, Dragster (first track) and Zoom Zoo (second track). (`frames/bg3-native/`, `frames/bg3-wide/`, `frames/zoomzoo-*`.)

## 2. HUD recon

- [x] 2.1 Capture BG3-only (`SNESRECOMP_LAYER_MASK=0x04`) frames at the landmarks.
- [x] 2.2 Measure top HUD scanlines and the `RACE`/`0/3` and timer spans. (Dragster RACE 17–46; Zoom Zoo 0/3 17–38; timer 193–238; rows 17–30; gap 39/47–192.)
- [x] 2.3 Measure the bottom lap-time group and prompt/arrow rows. (Prompt 65–174 rows 83–93; arrows 208–231 or 40–63 rows 112–126; lap time 105–158 rows 163–173.)
- [x] 2.4 Identify transparent gaps, flat/elastic runs and a safe left/right split. (Top gap 39/47–192; plain anchor band clips the 17 px left inset; 1:1 rigid segments are safe.)
- [x] 2.5 Decide elastic vs clamp; save the measured group map. (Elastic rigid segments for the top band; clamp band for prompt/arrows/lap time. See `work/ws-hud/hud-layout.md`.)
- [x] 2.6 Correct the track identity. (Dragster = first track; Zoom Zoo = second. The earlier route was Dragster.)
- [x] 2.7 Capture Zoom Zoo native/wide BG3-only and measure its `0/3`/timer/arrow/lap-time groups. (`frames/zoomzoo-bg3-native/`, `frames/zoomzoo-bg3-wide/`.)
- [x] 2.8 Record that the archived `ws-spawn` N/A was Dragster-only and Zoom Zoo needs a separate spawn recheck. (`work/ws-hud/hud-layout.md` note.)

## 3. P2c HUD policy

- [x] 3.1 Test anchor vs elastic rigid segments for the top groups; do not clip the left glyphs. (Anchor band clips; elastic segment `17..64 -> -39..8` keeps a 4 px inset.)
- [x] 3.2 Keep the centre/bottom group and prompt rows unanchored. (Clamp band rows 31–224; prompt/arrows/lap time stay centred.)
- [x] 3.3 Re-apply the policy every live-race frame; clear it outside live race. (`PpuSetWidescreenBg3Widen(17)` + elastic slot 0 each draw; cleared when off.)
- [x] 3.4 Verify glyphs are 1:1 and no lower layer shows through the HUD band. (Visual: RACE/0/3 and timer are 1:1; middle gap shows world, no hole.)

## 4. Continuous fallback

- [x] 4.1 If no transparent gap exists, measure flat runs for an elastic split band. (N/A: both tracks have a large transparent top gap.)
- [x] 4.2 If no safe elastic run exists, keep the clamp and record why. (N/A: safe rigid segments exist.)
- [x] 4.3 Verify the chosen fallback has no hole and no resampled glyph. (N/A; chosen elastic path verified.)

## 5. Switch

- [x] 5.1 Add `SNESRECOMP_WS_HUD`, independent of `SNESRECOMP_WS_CULL`.
- [x] 5.2 Verify HUD=0 restores the clamp while cull stays wide. (`frames/hud-test/hudoff-3500.png`: 0/3 back at x60, timer at x236.)
- [x] 5.3 Verify the policy is inert outside live race and with WS off. (Native 256×224 frame and menus unchanged.)

## 6. Verification

- [x] 6.1 Verify WS-off 4:3 frames match the preserved baseline. (Native Zoom Zoo frame; no HUD policy active.)
- [x] 6.2 Capture wide BG3-only and composite HUD frames with the policy on/off. (`frames/hud-test/` and `frames/zoomzoo-bg3-*`.)
- [x] 6.3 Check no hole, no lower-layer bleed, no clipped/overlapped glyph, no prompt shift. (RACE/0/3 4 px inset, timer right inset; prompt/arrows/lap time unchanged.)
- [x] 6.4 If the policy distorts or opens the band, revert and record the first bad frame. (Initial left segment clipped the R; fixed with 4 px inset. No revert.)

## 7. Report

- [x] 7.1 Write `report.md` with measured groups, policy, switch, 4:3 result and captures.
- [x] 7.2 Apply the acceptance criteria and state P2c complete, partial, clamp retained or reverted. (P2c complete for the top band on both tracks; centre/prompt rows intentionally clamped.)
- [x] 7.3 Verify no generated C, frames or ROM data exist in the repository; all derived output stays under `work/ws-hud/`.
- [x] 7.4 Summarize the outcome and the recommended next change. (Recommend the Zoom Zoo spawn recheck as a separate change, then final polish.)
