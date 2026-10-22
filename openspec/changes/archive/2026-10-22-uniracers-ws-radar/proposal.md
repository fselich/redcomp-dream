## Why

In widescreen races the player can see more of the track, but there is still no
map of the local section. A small radar in the bottom-right margin would show
the track around the player and the relative position of the CPU rival without
touching the game. The track data and the rider positions are already decoded:
the `$7F` track table (`source` mode) gives the shape, and `$0411/$0413` give
the riders.

A radar that shows only the current BG1 ring (512×512 px) would duplicate what
is already on screen. This change shows a **128×64-cell window (2048×1024 px)**
from the track table, so it covers about twice the ring and adds real context.

## What Changes

- Add a local radar overlay, **64×32 pixels**, showing a **128×64-cell
  (2048×1024 px) window** around the player, in the bottom-right widescreen
  corner.
- Render the track shape from the decoded `$7F` table (the `source` anchor);
  if the anchor is not live yet (race start, or a mode without it), draw no
  track until it is valid.
- Draw the player and CPU rival positions as distinct dots. The positions use
  the live WRAM rider/camera values and the same anchor as `source`.
- Compose the radar in-frame as a host `PpuExtraObject` (RGB15, no guest
  memory). It scales with the game, appears in captures, and needs no host
  presenter change.
- Add a config.ini switch: `[Video] MiniMap = 1` (default) / `= 0` (off).
  `SNESRECOMP_WS_MINIMAP=0` is an environment override for tests. The radar
  also stays off outside a live 1P race and at 4:3.
- Validate: track shape and dots on the five 1P tracks; 16:9, 21:9 and 512 px;
  4:3/WS-off and `MiniMap=0` byte-identical to the current build; no guest
  state change; negligible cost.

## Capabilities

### New Capabilities

- `recomp-ws-radar`: local track radar overlay sourced from the decoded track
  table, with player/rival positions and its own configuration switch.

### Modified Capabilities

None. The radar reads the existing `source` map and rider state; it does not
change BG1 margins, objects, cull, HUD or guest behavior.

## Impact

- `src/game_rtl.c`: track-window sampler, RGB15 radar buffer, player/rival
  positions, `PpuExtraObject` setup, config read.
- `src/main.c` / `src/game_rtl.h`: register a small `after_config` hook for the
  `[Video] MiniMap` key.
- `config.ini`: a new `[Video] MiniMap` key; the framework already accepts
  host-owned keys in that section silently, so no framework parser change is
  required.
- Work artifacts under `work/ws-radar/` (gitignored, ROM-derived).
- 1P widescreen only; 2P, netplay and non-live screens are out of scope.
- No change to `source`, `ultrawide`, OAM, cull or HUD behavior.
