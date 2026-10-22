## Why

`uniracers-ws-source` validated `SNESRECOMP_WS_OFFWINDOW=source`: both 16:9 BG1
margins show the authentic track, with 0 wrong cells and no measurable cost.
But it is opt-in. In normal play (`Widescreen = 1` in `config.ini`, no
environment variables) the BG1 margin policy today is **the raw ring wrap**:

- `WsOffwindowMode()` returns 0 when `SNESRECOMP_WS_OFFWINDOW` is unset;
- the global BG1 clamp only applies when `SNESRECOMP_WS_TRACK_CLAMP` is set.

Older docs describe the clamp as "the default", but no switch makes it so in
the current tree. Players therefore see disconnected, wrapped track pieces in
the margins.

## What Changes

- With `SNESRECOMP_WS_OFFWINDOW` unset, the 16:9 BG1 policy becomes `source`.
- `SNESRECOMP_WS_OFFWINDOW` accepts `source` (default), `ring`, `ghost`,
  `clamp` (global BG1 clamp) and `wrap` / `off` / `0` (the raw ring, as today).
  `SNESRECOMP_WS_TRACK_CLAMP` keeps working when `SNESRECOMP_WS_OFFWINDOW` is
  unset, for compatibility.
- The `[ws-ow]` stderr chatter becomes opt-in, through
  `SNESRECOMP_WS_OFFWINDOW_LOG` / `SNESRECOMP_WS_SOURCE_LOG` being set.
- Docs say plainly what the default is.

## Capabilities

### New Capabilities

- `recomp-ws-source-default`: the default 16:9 BG1 margin policy and the switch
  values that select the alternatives.

### Modified Capabilities

None. The `recomp-ws-source` behaviour is unchanged; only its selection
default moves.

## Impact

- `src/game_rtl.c`: `WsOffwindowMode()`, the clamp/repeat branch in
  `GameDrawPpuFrame()`, and the log gating.
- No change with widescreen off (native 4:3 bit-identical).
- Artifacts under `work/ws-source-default/`.
