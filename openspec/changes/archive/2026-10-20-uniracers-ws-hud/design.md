# Design: Uniracers widescreen HUD (Phase E6)

## Context

The live-race HUD is BG3. The current code clamps BG3 to the native 256 columns:

```c
PpuSetWidescreenLayerClamp(g_ppu, 1u << 2);
```

At 342×224 that keeps `RACE` and the timer centered with empty margin columns
above the track. P2c says to treat the HUD per column group: rigid groups copy
1:1 at the widened edges, elastic material absorbs the extra columns, and a
continuous bar must not open a transparent hole.

Track identity correction: **Dragster is the first track and Zoom Zoo is the
second**. The earlier route selected Dragster; the HUD must be measured on both.
Zoom Zoo uses a lap counter (`0/3`, `1/3`) where Dragster uses `RACE`; both use
a top-right timer, a bottom-centre lap time and side arrows.

What is known from the frames:

- `RACE`/`0/3` sits at the top left, the timer at the top right.
- A lap time sits at the bottom centre.
- `PULL A STUNT` and similar prompts may share BG3; they must be measured, not
  assumed to be part of the anchored band.
- The HUD is presentation-only; no guest register or simulation state needs to
  change.

## Goals / Non-Goals

**Goals:**

- Measure the BG3 HUD rows and column groups (rigid vs flat/elastic).
- Anchor the measured rigid left/right groups at the widened edges with 1:1
  glyphs.
- Keep the centre/bottom group untouched.
- Provide a safe clamp/elastic fallback for a continuous bar.
- Add an independent `SNESRECOMP_WS_HUD` switch.
- Prove no lower-layer holes, no glyph distortion, and 4:3 unchanged.

**Non-Goals:**

- P7/P8 objects, spawn/staging, gameplay, 2P/VS.
- Stretching or resampling text glyphs.
- Changing menus or non-live HUD states.

## Decisions

### D1 — Measure the HUD in isolation

Capture BG3-only (`SNESRECOMP_LAYER_MASK=0x04`) and composite frames at:

- race start (1500–1600),
- the ghost pass (1739–1741),
- the Dragster (first track) and Zoom Zoo (second track) curves (3500).

For each scanline, record the BG3 column spans with non-transparent pixels and
the transparent gaps. Build a per-band map:

```text
top band:      left group [0, L) | gap | right group [R, 256)
bottom band:   centre group
prompt rows:   separate band; do not anchor
```

### D2 — Anchor rigid groups

Use `PpuSetWidescreenLayerAnchorBand` on the top HUD band:

```c
PpuSetWidescreenLayerAnchorBand(ppu, 2, y0, y1, left_end, right_start);
```

`left_end` is the first transparent column after `RACE`; `right_start` is the
first column of the timer. The anchor shifts the left group to the widened left
edge and the right group to the widened right edge, keeping the middle centred.
Rigid segments are copied 1:1, so glyphs stay pixel-exact.

### D3 — Continuous bar fallback

If the measured HUD has no transparent column between its groups, anchoring
would open an `extra`-px hole. In that case:

1. Use `PpuSetWidescreenLayerElasticSplitBandSlot` with measured flat runs; or
2. Keep the clamp and record why no safe split exists.

Do not use the whole-line stretch band: it resamples every glyph.

### D4 — Keep the centre and prompts untouched

The bottom lap time is a single centred group; do not anchor it. Prompt rows
(`PULL A STUNT`, countdown text) use their own band and stay native-centred
unless measurement proves a safe rigid split. Exclude transitional scanlines
from the anchor band.

### D5 — Switch and lifetime

Add `SNESRECOMP_WS_HUD`, default on, independent of `SNESRECOMP_WS_CULL`.
Re-apply the PPU policy every live-race frame; clear it outside live race and
when the switch is off. The policy is host-only and must not touch the guest
BG3 registers or tilemap.

### D6 — Verification

- BG3-only and composite wide frames show anchored `RACE`/timer, 1:1 glyphs.
- The middle columns between the groups show the world, not a hole or a
  stretched centre group.
- The bottom lap time remains centred and unclipped.
- 4:3 frames and WS-off frames match the preserved baseline.
- No lower-layer bleed through the HUD band.

### D7 — Workspace

```text
work/ws-hud/
├── project/       # copy of the ws-spawn build
├── patches/       # HUD policy and switch
├── logs/          # layer captures and policy dumps
├── frames/        # BG3-only and composite wide/native
└── metrics/       # group spans and hashes
```

## Risks / Trade-offs

- **No transparent seam** → keep clamp or use measured elastic runs; never
  stretch glyphs.
- **Prompt rows misclassified** → measure per row; separate bands.
- **Raster HUD / per-line scroll** → verify the anchor band applies on the HUD
  lines; fall back to clamp if it does not.
- **Hole through the middle** → inspect composite and lower-layer leak.
- **4:3 regression** → gate to live race + `SNESRECOMP_WS_HUD`, compare hashes.
- **Menu HUD** → policy applies only in live race.

## Open Questions

- What are the exact top-band scanlines and the `left_end`/`right_start`
  transparent columns?
- Is `PULL A STUNT` on BG3 or another layer, and which rows does it occupy?
- Is the HUD one continuous graphic or separate rigid groups?
- Does the bottom lap time share the top band's BG3 hScroll?
