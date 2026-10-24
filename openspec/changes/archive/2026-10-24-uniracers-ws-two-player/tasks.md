Read `design.md` first, including "Frame sequence that matters here" and "Corrections to the first draft". Record every measurement in `report.md` as you go; captures with ROM-derived pixels stay outside the repository. Before changing code, run `game/tools/validate.sh` and keep its output: the 1P hashes must not change at any point.

## 1. Measurement (no feature code yet)

- [x] 1.1 Write a 2P route under `game/tools/` (start from the route in the design's investigation log) that reaches a live race on a known track and then moves both pads; record the frame at which `$0DDB` becomes 1. With the trace build and `SNESRECOMP_DEBUG_PORT`, record `$0DDB`, `$00B4`, `$0300`, `$0EE9`, `$0421`, `$0423`, `$03ED`, `$0411-$041F` on menus, PICK screens, NOW PLAYING, countdown, race, pause, finish and the return to the menu, and in the attract/demo
- [x] 1.2 Add `kRamCam2X = 0x041B` and `kRamCam2Y = 0x041F` to `game/src/variables.h` and correct the `kRamRider2X/Y` comments (CPU rival in 1P, player 2 in 2P)
- [x] 1.3 Add the `SNESRECOMP_WS_2P_LINE_LOG` diagnostic to `ws_margin.c` (per drawn line: frame, line, BG1 map base, BG mode, BG1 h/v scroll, main-screen layers; viewport column filled in once 3.1 exists) and enable the margin line hook for it in 2P even before the predicate changes (a temporary `forcepoke 00b4 01` run is acceptable for this measurement only). Already measured on Looper (`.ppuw.tsv`: `4953 0 2107 0C hdma`, `4953 114 2107 1C hdma`): HDMA sets BG1SC to base `0x0C00` on line 0 and `0x1C00` on line 114, so the bottom half is BG1 with its own map base, the bottom viewport starts on line 114 and the divider is 112..113. Re-verify this on at least two more tracks (one of them Zoom Zoo or a looping track) and on VS, record how each half's BG1 scroll relates to its camera, and name the line constants in `ws_frame.h`. If any track differs (another layer, another line), stop and record it (design D3, contingencies)
- [x] 1.4 In `WsOamFix`, temporarily log `ppu->oam[192..199]` and `ppu->highOam[24]` for consecutive 2P frames together with the WRAM rider/camera words. Confirm the slot order (96 P2 bottom, 97 P1 bottom, 98 P1 top, 99 P2 top), the `+$70` bottom Y offset, the vertical window `[-41,112)`, the tile/attribute patterns of each rider entry, and the high bit of the sentinels `($70,$70)` and `($30,$30)` as uploaded. Explain the earlier `$A5`/`$5A`/`0x55` values (expected: alternation between frames and staging one frame ahead of the upload). Main open point: while measuring, look also for a real rider with a positive X that the game uploaded with the high bit set (compare `rel` from WRAM with the decoded X); record whether it happens, because it decides whether the `xhigh-clear` exception of design D5 step 2 is implemented
- [x] 1.5 Write a VS route and repeat 1.1, 1.3 and 1.4 for VS; record whether `$0DDB`, the viewport lines, the slots and the HUD rows match 2P

## 2. Predicate and frame

- [x] 2.1 Rewrite `WsInLiveRace()` as in design D1; if 1.5 found another VS flag, add it to the 2P branch
- [x] 2.2 Verify `WsFrameCtxBuild()` sets `two_player` and that `WsApplyCull`, `PrepareFrame`, the margin, HUD, OAM and radar modules get the new predicate without other changes
- [x] 2.3 Confirm `PrepareFrame()` produces 342/448/512 × 224 in a live 2P race at 16:9, 21:9 and 32:9
- [x] 2.4 Confirm the native 2P frame and all 2P menus and selection screens stay 256×224, and record NOW PLAYING's behaviour next to 1P's
- [x] 2.5 Run `validate.sh`: 1P hashes unchanged

## 3. BG1 margins per viewport

- [x] 3.1 Add `WS_VP_MAX` and `WsViewportOfLine(two_player, line)` to `ws_frame.[ch]` with the constants from 1.3
- [x] 3.2 Refactor `ws_ring.[ch]` into the per-viewport `WsRingVp` state of design D3: per-viewport first line, scene check, unwrap, map-base latch and seen table; viewport-indexed accessors; viewport-0 wrappers for `WsRingScrollValid/H/V`
- [x] 3.3 Refactor `ws_source.[ch]`: per-viewport anchor set, generation and race start; shared grid header and word index; `WsSourceFrame` per active viewport; `WsSourceAgree(vp, ...)`; viewport-0 wrappers `WsSourceValid`, `WsSourceAnchorX/Y`; `vp=` in log lines
- [x] 3.4 Update `ws_margin.c`: line hook and tile hook select the viewport by line and skip divider lines; `s_logged` per viewport; `WsMarginFrameEnd` per viewport; store `two_player` in `WsMarginFrameBegin`
- [x] 3.5 Run `validate.sh` before any 2P test: 1P hashes unchanged. If one changes, the refactor is not behaviour-preserving; fix it before continuing
- [x] 3.6 Add a per-half mode to `tools/scripts/ws-offwindow/world_key_check.py` (split probe rows at the bottom viewport's first line, score each half with its own camera)
- [x] 3.7 Run a 2P wide race with both riders moving separately (cameras far apart, more than 512 px); score each viewport: wrong served cells, masked cells, anchor latency, `search_ms`
- [x] 3.8 Repeat 3.7 on Zoom Zoo, Switcher and Monster; record anchor ambiguity per viewport
- [x] 3.9 Confirm `clamp` keeps BG1 native-width in both halves

## 4. HUD

- [x] 4.1 In `ws_hud.c`, add the 2P band set of design D4 (clamp band off, `Bg3Widen(10)`, slots 0/1/2 with segments `T`, `D`, `T`); align the band line ranges with the divider rows from 1.3; clear slots 1..2 on the 1P path and 0..2 on the disabled path
- [x] 4.2 Capture BG3-only (`SNESRECOMP_LAYER_MASK=4`) and composite frames at 16:9, 21:9 and 512: `RACE` 4 px inside the left edge, timer and name at the right edge, divider full width without a seam, prompts centred, glyphs byte-identical to native, nothing else in the margins; `Hud=0` clamps BG3 in both halves
- [x] 4.3 Re-measure the BG3 rows and columns on a second tour, with the widest rider names, and on VS; adjust the segments only if content falls outside columns 17..255 or outside the band lines
- [x] 4.4 Run `validate.sh`: 1P hashes unchanged

## 5. Rider OAM

- [x] 5.1 In `ws_oam_fix.c`, replace the `two_player` early return in `WsFixRiderOamXHigh` with a call to a new `WsFixRiderOamXHigh2P`, keeping the existing gates and `SNESRECOMP_WS_OAM_XHIGH`; the 1P body stays unchanged
- [x] 5.2 Implement the slot table and the per-slot order of design D5 (rider-pattern check, sentinel hide with the high bit from 1.4, leave a high-bit entry (with the `xhigh-clear` exception only if 1.4 found the case), leave `lo >= E`, set the missing high bit with the slot's camera and `WS_OAM_XHIGH_SLACK`, otherwise leave)
- [x] 5.3 Add the 2P OAM log (`frame,slot,half,lo,y,hi,tile,attr,rel,rel_y,E,action`) under `SNESRECOMP_WS_2P_OAM_LOG` or as a `vp` extension of `SNESRECOMP_WS_PARK_LOG`
- [x] 5.4 Verify at 16:9, 21:9 and 512: each player enters the right and left margin of both halves; a rider near the other camera is drawn in the other half too; no sentinel or rider fragment appears at any `E`; `SNESRECOMP_WS_OAM_XHIGH=0` returns the uncorrected wide output
- [x] 5.5 Run `validate.sh`: 1P hashes unchanged
- [x] 5.6 Add `WsOamLineFix` after playtest showed the bottom rider missing: set the line's half slots 96/97 (bottom) or 98/99 (top) to the big size bit and the true X high bit from the beam line hook; install it for any live 2P/VS (wide and native 4:3) while keeping 1P unchanged; re-record the five wide 2P/VS references and `2p-native`, and verify every 1P reference stayed byte-identical

## 6. Cull and gating

- [x] 6.1 Verify `WsApplyCull()` widens by `E << $03ED` in 2P, that no second cull pair appears for the bottom half within a frame, and that the native pair is restored when the race ends
- [x] 6.2 Verify `Cull=0` keeps riders out of the margins while the frame stays wide
- [x] 6.3 Verify `MiniMap=1` draws no radar in 2P/VS
- [x] 6.4 Verify no new guest writes: compare WRAM/VRAM/CGRAM dumps with wide on/off at the same frame; only `$0421`/`$0423` may differ; SRAM unchanged

## 7. Validation and documentation

- [x] 7.1 Finalise the 2P and VS routes under `game/tools/` so they reach a live race deterministically on the chosen tracks
- [x] 7.2 Add the cases of design D8 (`2p-native`, `2p-16x9`, `2p-21x9`, `2p-512`, `2p-clamp`, `vs-16x9`) to `game/tools/validate.sh`, record their references with `--record`, and check that the existing 1P references did not change
- [x] 7.3 Run 2P on the tour-1 tracks plus Zoom Zoo and one looping track; record native 4:3, RNG and progression invariance
- [x] 7.4 Update `docs/WIDESCREEN.md`, `docs/TRACK_MARGIN.md` (per-viewport state), `docs/VALIDATION.md` (2P cases, per-half probe), `docs/CONFIG.md` (new diagnostic variables), `docs/ROADMAP.md` (W4) and `README.md`
- [x] 7.5 Write `report.md`: viewport lines and BG1 layout per half, sentinel high bits, slot patterns, width matrix, BG1 score per viewport, HUD and OAM captures (described, not committed), guest-state diff, 1P hash check and an accept/partial/revert decision
- [x] 7.6 Write `docs/TWO_PLAYER.md` as the durable record of the feature: how a 2P/VS race is detected, the split-screen layout (cameras, viewports and per-viewport BG1, HUD bands, the four rider OAM slots, cull), how each part was measured and implemented, the decisions and rejected alternatives (including the first-draft errors listed in the design), and the remaining limits; link it from `docs/README.md`, `docs/WIDESCREEN.md` and `docs/TRACK_MARGIN.md`, remove "Two-player split screen is not adapted" from `docs/README.md`, keep ROM-derived data out, and pass `tools/scripts/docs/check_links.py`
