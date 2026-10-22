## Why

On an ultrawide display the game occasionally draws a sliver of a rider
sprite at the far right edge, looking like a reflected piece of the player's
own character. Every ultrawide aspect from 21:9 up resolves to the 512 px
width cap (`E = 128`), which puts the game's culled-rider parking sentinel
(`X = $170`, `Y = $70`, high bit set) back inside the rendered window instead
of wrapping it off-screen.

## What Changes

- Reproduce and capture the artifact at `E = 128` (512 px) and identify the OAM
  slot and raw entry behind it. The prime suspect is the parked sentinel
  `X = $170` (`368`), which `PpuDecodeOamX` leaves at `+368` when
  `E = 128` (`256 + 128 = 384`), drawing a 16 px sliver at the right edge.
- Harden the parked-rider handling in `WsFixRiderOamXHigh`: a parked sentinel
  is never a drawable position at the current width cap, so hide it in the
  PPU copy regardless of the WRAM position, instead of keeping it when the
  rider "really" sits near `(-144, 112)`.
- Keep the fix presentation-only: only `ppu->oam` / `ppu->highOam` are
  touched, never guest WRAM.
- Extend the existing `SNESRECOMP_WS_OAM_XHIGH` switch so disabling it
  restores the current behaviour, and add a diagnostic log line for parked
  slots (raw X, high bit, WRAM-relative X/Y, `E`).
- Rule out the separate open zoom issue (`$03ED = 4`, 16-bit cull/sign wrap
  on tour 2 track 1). If the capture shows that instead, record it as a
  separate change rather than folding it in here.
- Validate: no OBJ pixels in the outer margins of an empty stretch at 512 px,
  a real right-margin rider still draws, and 16:9 / native output stays
  byte-identical.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `recomp-ws-objects`: culled/parked riders SHALL never be drawn in either
  margin, including the right margin at the 512 px cap, and the high-bit fix
  SHALL distinguish a parked sentinel from a real rider at the same X.

## Impact

- `game/src/game_rtl.c` (`WsFixRiderOamXHigh` and its diagnostic log).
- No framework (`game/snesrecomp/`) change and no guest-memory write.
- Artifacts under `work/ws-right-edge-ghost/` (ROM-derived, gitignored).
- Out of scope: 2P split screen, widths above 512 px, and the open zoomed-track
  rider-wrap issue unless the capture proves it is the same defect.
