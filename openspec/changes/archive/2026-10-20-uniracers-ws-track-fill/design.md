# Design: True BG1 track fill (Phase E9)

## Context

- BG1 is the track layer.
- Native 4:3 is continuous; wide margins show disconnected pieces.
- BG1 map: 32×32 (256×256), hScroll 776; `bgXsc[0]=0x0C`.
- Frame 5000 DMA: 29 transfers, mostly 32-byte chunks to `$2118`; the small
  map writes are around `0x0CF3/0x0EA0/0x0EB3`.
- `ws_shadow` history/edge fill (`SNESRECOMP_WS_TRACK=fill`) is partial:
  frame 4000 is mostly continuous, frame 5000 loops still fragment.
- Clamp is the safe archived fallback.

## Goals / Non-Goals

**Goals:**

- Decode the BG1 column stream and world mapping.
- Determine whether true future/off-native columns exist in ROM/WRAM.
- Prefill host margins from those columns without touching guest state.
- Keep clamp as fallback; verify both loop directions and 4:3.

**Non-Goals:**

- HUD, cull/OAM, spawn/staging, 2P/VS.
- Synthesizing geometry.
- Changing camera/scroll.

## Decisions

### D1 — Trace the BG1 stream

Use the debug server at loop/slope frames:

- `ppu_dmas` per frame: source bank:addr, destination VRAM word, size;
- isolate writes landing in the BG1 map base;
- dump VRAM around the map and compare with the source bytes;
- watch WRAM staging buffers around `$0433–$0455` and the DMA source PC;
- step frames to see the source pointer advance and ring-column mapping.

### D2 — Identify the true column model

Determine whether the 32-byte chunks are:

- a vertical column of tilemap entries for a world column;
- a CHR tile update, not a map column;
- a half-column or a ring head/tail.

Record the world column/world x for each chunk and whether the game writes
ahead/behind the native edge.

### D3 — Source-backed fill

If true columns exist:

- read them from `g_rom` or the relevant WRAM buffer;
- write them into `ws_shadow` via `WsShadowPrefillTile`/`ForceTile` for the
  margin world cells;
- key world/scroll as in `ws_shadow.h` (WRAM camera + PPU scroll);
- never advance guest camera, DMA queue or ring pointer.

If no true columns exist, keep clamp and record the limitation.

### D4 — Coexist with clamp

`SNESRECOMP_WS_TRACK`:

- `fill` — source-backed shadow fill;
- `clamp` — safe native track;
- `off` — pre-change natural sampling for comparison.

Fill must fall back to clamp if the source is unavailable or the frame is not
recognized.

### D5 — Verification

- Native/wide at loops and slopes in both directions.
- No disconnected fragment, no smeared edge column.
- 4:3 and WS-off exact.
- RNG/camera/simulation unchanged.

### D6 — Workspace

```text
work/ws-track-fill/
├── project/       # copy of the clamp build
├── logs/          # DMA/WRAM/VRAM traces
├── frames/        # fill vs clamp vs native
├── metrics/
└── patches/
```

## Risks

- **No true source** → clamp remains; do not invent.
- **Wrong column mapping** → visible duplicate/misaligned track; revert.
- **Ring wrap** → prove world column before prefill.
- **Performance** → measure per-frame fill.
- **4:3 regression** → gate to live race + WS.

## Open Questions

- Are the 32-byte `$2118` chunks columns or CHR?
- Where is the level strip pointer, and does it advance monotonically?
- Does the game write columns ahead of the native edge?
- Can ROM/WRAM provide the true leading-edge columns?
