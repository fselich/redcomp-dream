## Why

With `source` as the default (`uniracers-ws-source-default`), the BG1 track is
served from the full track table in WRAM `$7F`. BG1 no longer limits how wide
the race view can be. The frame is still hard-coded to 342 px (16:9), so
ultrawide monitors get pillarboxes.

## What Changes

- The race frame width follows the target aspect instead of a fixed 342 px. The
  target is the larger of 16:9 and the window aspect, or
  `SNESRECOMP_WS_ASPECT` (`16:9`, `21:9`, `24:9`, a decimal, or `auto`). It is
  computed with the framework's `SnesDisplayAspect_ComputeAdaptiveFrame` and
  the configured `DisplayAspect` pixel shape.
- Width is capped at **512 px** (margin E ≤ 128 per side, ≈ 24:9 with 4:3 CRT
  pixels). The SNES OAM X is 9 bits, and the renderer decodes it as
  `[E − 256, 256 + E)`. Beyond E = 128, left-margin sprites would alias into the
  right margin.
- Every widescreen constant that assumed 43 px is derived from E:
  - object cull margin (`$0421/$0423`);
  - HUD elastic segments (left group and timer).
- Validation at 16:9, 21:9 and 24:9:
  - BG1 0 wrong (probe);
  - BG2 background, HUD and riders checked visually;
  - native 4:3 and the 16:9 default unchanged;
  - cost.

## Capabilities

### New Capabilities

- `recomp-ws-ultrawide`: aspect-driven race frame width up to 512 px, with
  every margin-dependent widescreen policy derived from the margin width.

### Modified Capabilities

None. 16:9 behaviour stays byte-identical when the target resolves to 16:9.

## Impact

- `src/main.c` (`PrepareFrame`) and `src/game_rtl.c` (cull margin, HUD
  segments).
- No framework change.
- Artifacts under `work/ws-ultrawide/`.
- Out of scope: widths above 512 px (they would need OAM left-hints from the
  game's object positions); 2P split screen.
