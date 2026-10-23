## Why

On an ultrawide display the game draws a sliver of a rider sprite at the far
right edge, looking like a reflected piece of the current character. Every
ultrawide aspect from 21:9 up resolves to the 512 px width cap (`E = 128`),
where two families of parked OAM entries stop wrapping off-screen:

- the culled-rider sentinel (`X = $170`, `Y = $70`, high bit set) on slots
  98/99, which decodes as `+368` instead of `-144`;
- the parked 2P pair on slots 96/97 (`X = $165/$160`, `Y = $20/$60`), whose
  negative X decodes as `+357/+352`, i.e. image X 485/480.

`PpuDecodeOamX` wraps only at `256 + E = 384`, so both land inside the right
margin and draw even though they are off-screen in 4:3.

## What Changes

- Reproduce the artifact at `E = 128` (512 px) and identify the OAM slots and
  raw entries behind it. Both the sentinel and the parked 2P pair are involved.
- Harden `WsFixRiderOamXHigh`:
  - a parked sentinel is hidden in the PPU copy regardless of the WRAM
    position, instead of being kept when the rider "really" sits near
    `(-144, 112)`;
  - the parked 2P pair (slots 96/97) is hidden whenever its X high bit is set,
    since it has no legitimate position in a 1P race.
- Keep the fix presentation-only: only `ppu->oam` / `ppu->highOam` are
  touched, never guest WRAM.
- Extend the existing `SNESRECOMP_WS_OAM_XHIGH` switch so disabling it
  restores the current behaviour, and add a diagnostic log line for parked
  slots and any high-bit entry that lands in the right margin.
- Rule out the separate open zoom issue (`$03ED = 4`, 16-bit cull/sign wrap
  on tour 2 track 1). It was not observed here.
- Validate: no OBJ pixels from parked slots in the outer margins at 512 px, a
  real right-margin rider still draws, and 16:9 / 21:9 / native output stays
  byte-identical.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `recomp-ws-objects`: culled/parked riders and the parked 1P 2P-pair slots
  SHALL never be drawn in either margin, including the right margin at the
  512 px cap, and the high-bit fix SHALL distinguish a parked entry from a
  real rider at the same X.

## Impact

- `game/src/game_rtl.c` (`WsFixRiderOamXHigh` and its diagnostic log).
- No framework (`game/snesrecomp/`) change and no guest-memory write.
- Artifacts under `work/ws-right-edge-ghost/` (ROM-derived, gitignored).
- Out of scope: 2P split screen, widths above 512 px, and the open zoomed-track
  rider-wrap issue unless the capture proves it is the same defect.
