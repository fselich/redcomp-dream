## 1. Implementation

- [x] 1.1 Save `src/main.c` and `src/game_rtl.c` under `work/ws-ultrawide/patches/*.before`.
- [x] 1.2 `PrepareFrame`: width from `SNESRECOMP_WS_ASPECT` / window aspect via `SnesDisplayAspect_ComputeAdaptiveFrame`, floor 16:9, cap 512 px (D1).
- [x] 1.3 `game_rtl.c`: `WsExtra()` from `g_ws_extra` (0..128); cull margin = E (D2).
- [x] 1.4 HUD elastic segments derived from E (D2).

## 2. Verification

- [x] 2.1 Native 4:3 hash unchanged.
- [x] 2.2 `SNESRECOMP_WS_ASPECT=16:9`: Dragster 2500 capture equals the pre-change 16:9 default (`4b138431…`); record what `auto` resolves to headless. *(16:9 and headless `auto` → 342 px, `4b138431…`.)*
- [x] 2.3 21:9 and 512 px: frame widths 448 / 512; probe-scored BG1 on Dragster, Zoom Zoo and Monster: 0 wrong. *(0 wrong / 0 masked on all six runs.)*
- [x] 2.4 Visual review at 21:9 and 512 px: BG2 continuity, HUD placement, riders in the far margins (`work/ws-ultrawide/frames/`). *(see report §2–3; sprites checked visually only.)*
- [x] 2.5 CPU cost at 512 px vs 16:9. *(within noise.)*

## 3. Report

- [x] 3.1 Write `report.md`; save `work/ws-ultrawide/patches/ws-ultrawide.diff`.
- [x] 3.2 Update `docs/UNIRACERS_TRACK_FORMAT.md` (§5.4 ultrawide note in map-decode findings superseded), the handoff and `work/README.md`.
