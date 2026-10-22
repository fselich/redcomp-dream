# Proposal: Uniracers widescreen HUD (Phase E6)

## Why

P1–P4, P7/P8 are in place and P9–P13 were found not applicable in 1P. The BG3
HUD is currently clamped to the native 256 columns, so in 342×224 it stays
centered with empty margins at the top. P2c asks for a per-group HUD layout:
rigid groups copy 1:1 at the widened edges, elastic material absorbs the extra
columns, and a continuous bar must not open a hole through the middle.

The Uniracers HUD is not obviously one bar: `RACE` and the timer are separate
top groups, and the lap time is a bottom-centre group. This change first
measures the BG3 groups, then anchors the rigid ones and leaves the centre
group alone. If the HUD turns out to be continuous with no transparent seam,
the change uses an elastic band or keeps the clamp instead of stretching text.

## What Changes

- Capture BG3-only and composite HUD frames at race start, the ghost pass and
  the **Dragster** (first track) and **Zoom Zoo** (second track) curves.
- Identify HUD scanline bands and rigid/elastic column groups.
- Anchor the measured rigid left/right groups with
  `PpuSetWidescreenLayerAnchorBand` (or an elastic split band only if the bar
  is continuous).
- Keep the centre/bottom group untouched and all glyphs 1:1.
- Add `SNESRECOMP_WS_HUD` as an independent kill switch.
- Verify no lower-layer holes, no clipped glyphs, and 4:3 unchanged.

## Non-goals

- P7/P8 objects, spawn/staging (N/A), gameplay, 2P/VS split-screen.
- Stretching or resampling glyphs.
- Changing the HUD during menus or non-live states.

## Capabilities

### New Capabilities

- `recomp-ws-hud`: measured, per-group widescreen HUD anchoring with a safe
  clamp/elastic fallback and an independent switch.

### Modified Capabilities

None.

## Impact

- Work under `work/ws-hud/`; the baseline, ws-objects and ws-spawn workspaces
  are preserved.
- Host-side PPU policy only; no guest register, OAM or simulation changes.
- 4:3 remains the default; ROM-derived output stays in `work/`.
