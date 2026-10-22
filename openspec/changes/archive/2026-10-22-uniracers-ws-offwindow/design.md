# Design: Uniracers off-window track columns (Phase F2)

## Context

The native-map spike established:

- BG1 map base `0x0C00`, 32×32 entries, little-endian tile words (originally recorded as big-endian; corrected by `uniracers-ws-map-decode`).
- Uploader `$0433`/`$0455` → DMA 34-byte chunks → BG1.
- At frame 3505, the staged chunk is BG1 **row 19, x=7..23**.
- The DMA destination walks one map row per frame; stage and pending transfer
  are offset in time/row.
- Stage 1/2 is a vertical tile column, not the horizontal margin key.
- Clamp is correct for the Zoom Zoo vertical loop, but hides track on
  Dragster/Bowl/Switcher/Monster horizontal sections.
- Edge-repeat is test-only and breaks Zoom Zoo.

Phase F2 section 2 (2026-10-21 pm) added, with a renderer pixel-source probe:

- In race **BG1 uses 16×16 tiles** (`BGMODE = 0x19`). The ring is
  32×32 cells × 16 px = **512×512 px**.
- The 17-word chunk is 16 visible cells + 1 fine-scroll overhang.
- The trailing 16:9 margin already holds correct world cells; only ~3
  leading-edge cells per row are stale.

Full record: `docs/UNIRACERS_BG1_RING_FINDINGS.md`.

The local line-art maps in `maps/` are sufficient for topology/visibility but
are not tile-level data.

## Goals / Non-Goals

**Goals:**

- Derive a reproducible ring/column formula from the known stage anchor.
  *(Done.)*
- Serve the trailing margin from authentic ring cells and source the leading
  margin cells.
- Build a test-only wide-column prototype.
- Validate all five 1P tracks and 4:3.
- Decide go/no-go for production.

**Non-Goals:**

- New art, procedural track, or edge-repeat as a shipping fix.
- Gameplay/camera/RNG/2P.
- Replacing the whole uploader before the formula is proven.

## Decisions

### D0 — Pixel-source probe as ground truth

Ring claims are measured with the renderer itself, not inferred from register
dumps. The test-only probe (`SNESRECOMP_BGPROBE_FRAME=N[,M]`,
`SNESRECOMP_BGPROBE_LAYERS`, `SNESRECOMP_BGPROBE_FILE`) logs, per drawn span,
`(frame, layer, sy, sx, w, bgx, bgy, hscroll, vscroll, map_word, map_x, map_y,
real, served)` from both 4bpp paths (8×8 and 16×16). It is a local patch on
the `snesrecomp` pin (`work/ws-offwindow/patches/bgprobe.diff`). It is off by
default and does not change native output.

Reason: the PPU-state dump that reported `bgmode 1` was not the per-line state
the renderer uses. That one wrong input cost the previous phase its formula.

### D1 — Ring formula (validated)

```text
map_x = ((hscroll + screen_x) >> 4) & 31
map_y = ((vscroll + screen_y) >> 4) & 31
world_cell = ((unwrapped_scroll + screen) >> 4)   # scroll unwrapped mod 1024
```

Anchor, frame 3505: `hscroll = 121` → cell 7, phase 9. Screen 0..255 samples
cells 7..23, which is the stage chunk. No base offset.

Validation: 0 world-key conflicts on Dragster 2300–2500 (2,895 cells) and Zoom
Zoo 3300–3505 (830 cells). Negative controls (32-px key, raw register) do
conflict.

### D2 — Same-run anchors

Capture stage, DMA, VRAM and probe in **one process** for frames around:

- Dragster horizontal section (2300–2500 / 3500);
- Zoom Zoo vertical loop (3300–3505).

Never compare pre/post across separate runs; the snapshots are not stable.

### D3 — Margin source split by motion side

Measured with 16:9 and no clamp:

| | Trailing margin | Leading margin |
|---|---|---|
| Dragster (moving right) | 100 % correct | 47 % (cells +0/+1/+2: 70/36/36 %) |
| Zoom Zoo | 99.9 % | 97.7 % |

- **Trailing side:** serve the ring cells as-is. They are authentic history
  written by the game.
- **Leading side:** needs a source for ~3 cells per visible row. Candidates, in
  order:
  1. widen the row builder `$81:B3D0` (`$76` count) into a host shadow;
  2. synthetic guest call of the cell resolver with save/restore.
- Until the leading side is sourced, the prototype may clamp only the leading
  side (asymmetric clamp). The side is chosen from the sign of the frame's
  scroll delta, with hysteresis.

The line-art maps decide only whether the margins should contain track or
background; they are not a tile source.

### D4 — Test-only prototype

Add a dedicated switch, e.g. `SNESRECOMP_WS_OFFWINDOW=1`. Do not reuse
`SNESRECOMP_WS_TRACK_CLAMP` or the test-only repeat switch. The prototype must
be presentation-only and must not write the guest VRAM ring. Use a host shadow
(`ws_shadow` with `tileShift = 4`) or renderer-side substitution.

### D5 — Acceptance

Pass only if:

- native 4:3 is bit-identical;
- Dragster/Bowl/Switcher/Monster horizontal margins show real track;
- Zoom Zoo vertical margins remain background;
- no wrap, duplication or smearing;
- every served margin cell agrees with the world-key ground truth
  (`world_key_check.py`, 0 wrong on verifiable cells);
- all five tracks and loops/slopes pass;
- cost is acceptable.

Otherwise keep clamp as a documented fallback and do not claim true 16:9 for
the horizontal tracks. A partial result (trailing side only) may be reported as
such.

## Risks

- Leading-edge cell not resolvable from `$7F` beyond the native 17 → only
  trailing side + asymmetric clamp.
- Direction flips (bounces, reversing) → leading/trailing selection must follow
  per-frame motion with hysteresis.
- Vertical motion: rows written 17 wide can leave stale cells in the margins of
  newly scrolled rows. Measured as small on Zoom Zoo; check on other tracks.
- Modifying VRAM map can damage the next visible columns → host shadow only.
- Line-art cannot validate tiles → only topology.
- The probe lives in the framework submodule → keep it as a patch; reapply on
  pin bumps.

## Open Questions

- ~~Is the ring column formula affine in hscroll/vscroll?~~ Yes, with 16-px
  cells (D1).
- Which code writes the leading column during horizontal motion (row chunk only,
  or a separate column path)?
- Does the `$7F` segment lookup give valid cells for `start-3..start-1` and
  `start+17..start+19`?
- ~~Is a host shadow map safer than writing the guest VRAM ring?~~ Yes;
  presentation-only (D4).

## History: first ring formula (rejected)

The first derivation assumed 8×8 tiles
(`ring_cell_x = ((hscroll >> 3) + (x >> 3)) & 31`, start = 15 at frame 3505).
It concluded that the right margin was a wrapped copy of the left edge, and the
offline world-key test gave 28,080 conflicts in 149,568 overlaps. Causes:
8-px cells instead of 16, mixed tile/pixel units, ignored fine phase, and no
scroll unwrap. Superseded by D1.
