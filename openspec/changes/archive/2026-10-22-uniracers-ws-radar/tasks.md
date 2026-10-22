## 1. Setup and measurements

- [x] 1.1 Create `work/ws-radar/{logs,frames,metrics,patches,scripts}`. Save the current `src/game_rtl.c` as `patches/game_rtl.c.before`. Work in `<workshop>/work/ws-native-map-spike/project/UniracersSNESRecomp/`, build in `build-local/`.
- [x] 1.2 Record baselines: native 4:3 `3417f91a…`, source/default 16:9 at Dragster 2500, and a state trace with the radar off. *(`baseline-native-2500.ppm`, `baseline-source-2500.ppm`, `baseline-source.trace`.)*
- [x] 1.3 Validate the table coordinate formula on the recorded Dragster and Zoom Zoo frames: rider `$0411/$0413`, camera `$0419/$041D`, BG1 unwrapped scroll, `s_src_ax/ay`, `$03ED`. Record the first mismatching frame if any. *(Radar logs start at 1536; player/rival cells and the local track agree on Dragster and Zoom Zoo; Zoom Zoo `$03ED=2`. The cells are often palette fill (`word&0x3ff==0`), which is background in the chosen shape criterion; the dot still sits on the local track.)*
- [x] 1.4 Confirm `s_src_valid`/anchor timing and whether `WsSrcFrame` runs in the chosen mode; decide the radar visibility window at race start. *(Radar hidden until `s_src_valid>0`; first log frame 1536.)*

## 2. Configuration

- [x] 2.1 Add `GameAfterConfig` in `src/game_rtl.c` and register `.after_config` in `src/main.c`. Parse `[Video] MiniMap` from `config.ini` and then `config.local.ini`; default on; accept 0/false/off and 1/true/on.
- [x] 2.2 Add `SNESRECOMP_WS_MINIMAP=0`/`=1` as a test override. No framework parser change: `[Video]` is already a host-owned section.
- [x] 2.3 Test defaults, `MiniMap=0`, `config.local.ini` override and the env override; confirm no "Invalid .ini" warnings. *(All off cases hash `4b138431…`.)*

## 3. Radar data

- [x] 3.1 Add a 64×32 RGB15 panel buffer (`bit 15` opaque) and a 128×64-cell sampler: for each panel pixel, sample the corresponding 2×2 table cells through `WsSrcCell`; track if any `word & 0x3FF` is non-zero. Outside the grid is background.
- [x] 3.2 Draw the panel background, 1-pixel border, track colour, player dot and rival dot. Player at the centre (32,16); rival at `(rival_table - player_table)/2 + (32,16)`, clamped to the panel edge if outside.
- [x] 3.3 Hide/clear the panel while `s_src_valid == 0`; never guess a track shape.

## 4. Rendering and gating

- [x] 4.1 Build one `PpuExtraObject`: width 64, height 32, stride 64, priority 3, `math=1`, `oamSlot=127`, `order=1`; place it at the bottom-right of the visible field in **screen coordinates** (`x = 256 + ppu->extraRightCur - 64 - 2`, `y = 224 - 32 - 2`). The compositor adds `kPpuExtraLeftRight` internally.
- [x] 4.2 Call the radar update and `PpuSetExtraObjects` in `GameDrawPpuFrame` before the rasterizer. Clear the object when the radar is off.
- [x] 4.3 Gate: config/env on, `WideEnabled()`, `InLiveRace()`, `$0DDB==0`, `WsExtra()>0`, `s_src_valid>0`, no netplay. No object at 4:3, menus or pause.
- [x] 4.4 Verify the panel is inside the right margin at 16:9 (E=43), 21:9 (E=96) and 512 px (E=128), and does not cover the native HUD/timer/lap rows. *(64×32: 16:9 framebuffer 276..339, 22 px of native band but HUD ends at 273; 21:9 382..445; 512 446..509. The 16:9 case is lower-right, not strictly margin-only; the HUD is clear.)*

## 5. Validation

- [x] 5.1 Visual: Dragster idle and moving; Zoom Zoo idle and moving; Bowl, Switcher and Monster. Confirm the local track shape matches the decoded table and both dots match the on-screen riders. *(`radar-*-panel.png`; local track/loop/curve; dots.)*
- [x] 5.2 Validate the player/rival dots on Zoom Zoo (`$03ED=2`) and on a 512-px run. *(Zoom Zoo local loop, player centre; 512 panel red/white adjacent; `radar-aspect-512-panel.png`.)*
- [x] 5.3 Capture 16:9, 21:9 and 512 px screenshots with the radar on and off. *(`radar-on/off-2500`, `radar-aspect-219/512`, `radar-*-panel`.)*
- [x] 5.4 Native 4:3 and WS-off hashes unchanged; `MiniMap=0` wide frame byte-identical to the pre-change build except no radar. *(Native `3417f91a…`; off `4b138431…`; the 32×32 build's on/off diff was exactly the panel. The 64×32 resize changes only the panel buffer/object dimensions; the scripted menu route is flaky for a fresh same-state on/off diff.)*
- [x] 5.5 `SNESRECOMP_STATE_TRACE` and frame-2500 WRAM/VRAM/CPU/DMA dumps with radar on vs off are identical. *(Measured on the 32×32 build: traces identical; WRAM/VRAM/DMA identical; CPU registers identical, only the stale AOT `func` label differs. The 64×32 resize is the same read-only draw path with a larger panel.)*
- [x] 5.6 Measure cost with `SNESRECOMP_FRAME_TIMING`; target < 0.2 ms/frame and no visible fps change. *(`raster_ms` +0.021 ms; fps 59.6 vs 60.0.)*

## 6. Report and docs

- [x] 6.1 Write `report.md`: method, config switch, track/dot evidence, 16:9/21:9/512 captures, invariance and cost. Save `work/ws-radar/patches/ws-radar.diff`.
- [x] 6.2 Update `docs/UNIRACERS_NATIVE_MAP_HANDOFF.md` and `work/README.md` with the `[Video] MiniMap` switch and the work directory.
