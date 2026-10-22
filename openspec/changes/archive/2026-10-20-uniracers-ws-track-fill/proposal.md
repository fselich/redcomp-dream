# Proposal: True BG1 track fill (Phase E9)

## Why

The clamp change removed the disconnected track fragments, but it also removed
the track from the wide margins. The `ws_shadow` history experiment is
promising on some slopes (frame 4000) but still breaks on loops (frame 5000)
because the leading edge has no true column data: BG1 is a 256-wide ring map.

To fill the margins with the real track, the BG1 column stream must be decoded:
where each 32-byte DMA chunk comes from, which column/world position it
represents, and whether future columns can be prefetched from ROM/WRAM. This
change does that work and keeps the safe clamp as the fallback.

## What Changes

- Trace BG1 tilemap writes, DMA source/destination, WRAM staging buffers and
  the stream pointer across loop/slope frames.
- Identify the true column source and world mapping, or prove it unavailable.
- Feed true columns into a host-side shadow/prefill, keyed to PPU scroll, with
  no guest camera or DMA changes.
- Keep `SNESRECOMP_WS_TRACK=clamp` as the safe fallback and `off` as a
  comparison mode.
- Verify loops/slopes both directions, no fragments, and 4:3 unchanged.

## Non-goals

- HUD, cull/OAM, spawn/staging, 2P/VS, gameplay.
- Inventing track geometry not present in ROM/WRAM.
- Changing the guest camera or scroll.

## Capabilities

### New Capabilities

- `recomp-ws-track-fill`: source-backed BG1 margin fill with a clamp fallback.

### Modified Capabilities

None.

## Impact

- Work under `work/ws-track-fill/`; the clamp/validation workspaces are
  preserved.
- Presentation-only host code; no simulation.
- 4:3 remains default; ROM-derived data stays in `work/`.
