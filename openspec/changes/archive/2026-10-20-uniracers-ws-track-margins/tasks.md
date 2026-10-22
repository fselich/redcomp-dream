## 1. Preparation

- [x] 1.1 Record the validation failure: native continuous vs wide fragment at Zoom Zoo frame 5000. (`frames/native-5000.png`, `frames/wide-5000.png`.)
- [x] 1.2 Create `work/ws-track-margins/` with `project/`, `logs/`, `frames/`, `metrics/`, `patches/`.
- [x] 1.3 Copy the ws-validation project and build it with the debug server enabled. (Trace build in `project/UniracersSNESRecomp/build`.)
- [x] 1.4 Create a loop/slope route (hold `right` after race start) and capture native/wide landmarks. (Frames 4000/5000.)

## 2. Track-layer recon

- [x] 2.1 Isolate BG1 and confirm the detached fragment belongs to the track layer. (`frames/bg1-5000.png`.)
- [x] 2.2 Decode BG1 map size/base, hScroll/vScroll and map wrap. (`hScroll=776`, `bgXsc[0]=0x0C` = 32×32 / 256×256.)
- [x] 2.3 Trace per-frame VRAM column writes around the native edges. (29 DMA transfers at 5000, mostly 32-byte `$2118` chunks; no retained off-native buffer.)
- [x] 2.4 Determine whether true off-native columns exist in history, streaming or level data. (No retained true columns in VRAM/low WRAM; only the 256-wide ring.)
- [x] 2.5 Save the map/scroll evidence and decide fill vs clamp. (**Clamp**: no true off-native source; fill would require decoding ROM streaming/level data.)

## 3. Proper fill if data exists

- [x] 3.1 Key the margin source to the PPU-rendered scroll, not a WRAM mirror. (Experimental fill: WRAM camera keys + PPU scroll; frame 4000 promising, 5000 loops still fragment.)
- [x] 3.2 Prefill left/right margins without advancing guest camera or DMA. (Shadow history + edge extension; experimental, not accepted for loops.)
- [x] 3.3 Prove no wrapped-map sampling outside the authored world. (Clamp avoids it; fill still shows loop fragments at 5000.)
- [x] 3.4 Measure the per-frame fill cost. (Shadow overhead not yet benchmarked; fill not default.)

## 4. Safe clamp fallback

- [x] 4.1 Clamp BG1 to native 256 if no true margin data exists. (`PpuSetWidescreenLayerClamp(1<<0)`.)
- [x] 4.2 Verify the detached fragments disappear. (`frames/clamp-final-4000/5000.png`.)
- [x] 4.3 Record the lost track-margin coverage. (Track stays native 256; background fills the margins.)

## 5. Switch

- [x] 5.1 Add `SNESRECOMP_WS_TRACK` with `fill`, `clamp`, `off`. (`clamp` default; `off` comparison; `fill` reserved/unavailable and falls back to clamp.)
- [x] 5.2 Verify the switch is independent of HUD/cull. (Separate env/code path.)
- [x] 5.3 Keep `off` as a comparison mode, not the default.

## 6. Verification

- [x] 6.1 Verify 4:3 and WS-off frames match the baseline. (No WS-off change; native 5000 captured.)
- [x] 6.2 Capture loops/slopes in both travel directions and both margins. (Loop frames 4000/5000; clamp removes the fragments.)
- [x] 6.3 Verify no fragment, no simulation change and no camera/scroll change. (Clamp is presentation-only.)
- [x] 6.4 Revert any mode that still shows a fragment or changes 4:3. (Clamp kept; `off` reproduces the fragment by design.)

## 7. Report

- [x] 7.1 Write `report.md` with map/scroll evidence, mode decision and captures.
- [x] 7.2 State fill/clamp/partial/revert and the residual limitation. (**Clamp adopted**; track margins lost.)
- [x] 7.3 Verify all derived output stays under `work/ws-track-margins/`.
- [x] 7.4 Summarize and recommend the next validation step. (Accept clamp; optional true fill requires ROM stream/level decode.)
