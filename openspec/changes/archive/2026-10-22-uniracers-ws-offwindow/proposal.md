# Proposal: Uniracers off-window track columns (Phase F2)

## Why

The native-map spike proved the level data and uploader are real, but could not
map the BG1 ring to the columns outside `0..31`. That leaves the accepted clamp
incorrect for four of five 1P tracks:

- Dragster, Bowl, Switcher and Monster have horizontal track spanning the full
  native width; the clamp hides real continuation.
- Zoom Zoo’s vertical loop is the one section where the clamp is geometrically
  correct.
- The test-only edge-repeat looks right on Dragster but breaks Zoom Zoo; it is
  not a source-backed solution.

A focused follow-up is needed: derive the ring/column formula, source the
adjacent real columns, and decide if a true 16:9 track is implementable.

> **Status 2026-10-21 (pm).** The ring formula is proven: BG1 is 16×16 in race,
> and the ring is 512 px. The trailing margin already holds authentic ring
> cells. The remaining scope is sourcing ~3 leading-edge cells per row. See
> `docs/UNIRACERS_BG1_RING_FINDINGS.md`.

## What Changes

- Derive the BG1 ring formula from the known anchor: stage chunk = BG1 row 19,
  x=7..23 at frame 3505.
- Capture same-run frames around the horizontal Dragster section and the Zoom
  Zoo vertical loop.
- Decode the adjacent high-table/segment source for the leading-edge cells
  (the trailing edge is served from the ring).
- Add a test-only renderer pixel-source probe as ground truth for ring claims.
- Use the local line-art maps in `maps/` as a topology oracle, not as tile data.
- Build a test-only wide-column prototype with a dedicated switch.
- Validate 4:3, five tracks, loops/slopes and no wrap/duplicates.
- Produce a go/no-go for a production implementation.

## Capabilities

### New Capabilities

- `recomp-ws-offwindow`: source-backed BG1 cells outside the native 17-cell
  window of the 32-cell (16×16, 512 px) ring.

### Modified Capabilities

None.

## Impact

- Work under `work/ws-offwindow/`.
- Test-only host code; no gameplay/camera/RNG changes.
- Test-only debug probe patched into the `snesrecomp` renderer (local patch on
  the pin, off by default).
- Clamp remains the default until the prototype is accepted.
- Do not commit ROM-derived captures or external maps.
