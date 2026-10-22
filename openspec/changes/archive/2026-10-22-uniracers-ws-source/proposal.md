## Why

Two prototypes fill the 16:9 BG1 margins today. `ring` (Option A) serves only
verified ring cells, so the leading edge stays background. `ghost` fills the
leading edge (99.84 % on Dragster), but it runs a speculative copy of the
machine: about +2.3 ms per frame on average, a p99 of 9.6 ms, and ~42 fps
headless. In practice it is too slow to be worth it.

`uniracers-ws-map-decode` found that the whole track is already in WRAM. Bank
`$7F` holds an RNC1-unpacked image that does not change from load to finish: a
piece-id grid plus a library of 4×4-cell pieces. That decode matches the
renderer at 100 % (see `docs/UNIRACERS_TRACK_FORMAT.md`). A margin cell can
therefore be read straight from `$7F`. The cost is two WRAM reads, there is no
speculation, and coverage includes cells the camera never shows.

## What Changes

- Implement `SNESRECOMP_WS_OFFWINDOW=source` (mode 2, already reserved in
  `WsOffwindowMode()`). For a BG1 margin cell:
  1. verified ring cell (Option A, unchanged);
  2. else the `$7F` table cell at `world + anchor`;
  3. else the mask.
- Per-race **anchor**: find the offset between the renderer's world key
  (unwrapped scroll) and table coordinates. It is derived once from the
  verified native cells and re-checked every frame.
- Per-frame **self-check**: every verified native cell must equal its table
  cell. On disagreement, re-anchor and mask until the anchor is valid again.
- Validation against the renderer probe on all five 1P tracks, native 4:3
  bit-identical, guest state identical to `ring`, cost.
- Decision on making `source` the 16:9 default instead of `clamp`.
- `ghost` stays as an optional reference mode. It is not developed further.

## Capabilities

### New Capabilities

- `recomp-ws-source`: BG1 margin cells served from the game's own decoded
  track table in WRAM `$7F`, with a validated per-race anchor.

### Modified Capabilities

None. `recomp-ws-leading-edge` (Option A) and `recomp-ws-ghost-lane` keep their
behaviour; `source` is a new value of the same switch.

## Impact

- `work/ws-native-map-spike/project/UniracersSNESRecomp/src/game_rtl.c`:
  anchor, table lookup and self-check inside the existing margin hook path. No
  framework change: it reuses `g_ppu_margin_tile_hook` and
  `g_snes_beam_line_hook` from the leading-edge patch.
- Work artifacts under `work/ws-source/`.
- Presentation-only: reads WRAM and never writes guest memory or VRAM.
- Out of scope, but unblocked by it: ultrawide. BG1 is no longer the limit, while
  BG2, cull, HUD and CPU visibility remain separate work.
