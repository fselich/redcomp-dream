# Proposal: Non-horizontal track margins (Phase E8)

## Why

The per-track validation exposed a real rendering bug that earlier margin work
missed: on Zoom Zoo loops and slopes, the 16:9 margins show disconnected track
segments. Native 4:3 is continuous; BG1-only wide shows the fragments; the
layer uses a 32×32 (256×256) tilemap with hScroll 776, so the extra 43 px
margins sample the wrapped map.

This is P2/P3 territory: margin columns must be populated from the game's own
track data, not from a wrap. The safe clamp tested in validation removes the
fragments but also removes the track from the margins, so it is only a
fallback.

## What Changes

- Characterize the BG1 track map, scroll and streaming on loops/slopes.
- Determine whether the game retains or writes the true off-native columns.
- If yes, prefill the margins from that data/history, keyed to the PPU scroll.
- If no, provide a safe `clamp` fallback that keeps BG1 native and records the
  limitation.
- Add `SNESRECOMP_WS_TRACK` (`fill`, `clamp`, `off`) as an independent switch.
- Verify native 4:3, both loop directions, and no simulation changes.

## Non-goals

- HUD, cull/OAM, spawn/staging, 2P/VS, gameplay.
- Inventing track geometry not present in the game or its retained data.

## Capabilities

### New Capabilities

- `recomp-ws-track-margins`: non-horizontal track margin fill or safe clamp.

### Modified Capabilities

None.

## Impact

- Work under `work/ws-track-margins/`; validation and HUD workspaces preserved.
- Presentation-only changes to BG1 margins; no guest registers or simulation.
- 4:3 remains default; ROM-derived output stays in `work/`.
