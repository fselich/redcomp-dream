# Proposal: Uniracers HDMA margin extension (Phase E3)

## Why

The 16:9 preview is correct on flat sections, but its margins only show the
native per-line scroll; on curves and hills the HDMA tables need values beyond
the original 256 columns. Phase C identified HDMA ch3/ch4 writing
`$210F`/`$210D` and the current pointers (`$7E:8262`/`$7E:8280`, `$2046`), but
not the builder. This change finds the builder, maps the table layout and
extends the per-line scroll for the margins (P-U1), then verifies the wide seam
against the native curve.

## What Changes

- Trace WRAM writes to the HDMA tables and pointer with
  `set_wram_watch`/`wram_watch_log_get` and record the writer PC.
- Map the table layout (count/entries, mode 3) and how per-line values are
  derived from the camera and track curve.
- Extend the per-line values for the extra margin columns (+43 per side), or
  document P-U1 as blocked with evidence.
- Verify 4:3 stays aligned and 16:9 margins match the native curve at the seam
  on a curve/hill section.
- Report the builder, layout, extension method, evidence and remaining gaps.

## Non-goals

- Cull/OAM/spawn/staging (P7–P13), final HUD, netplay, audio.
- Simulation, collision, RNG or input changes.
- Player-facing widescreen polish.

## Capabilities

### New Capabilities

- `recomp-ws-hdma`: identification and extension of per-line HDMA scroll tables
  for widescreen margins, with seam evidence.

### Modified Capabilities

None.

## Impact

- Work under `work/ws-hdma/`; the `ws-spike` and `ws-discriminator` workspaces are
  preserved.
- May add a project-local generated-C hook or a host-side table extension; the
  framework pin is unchanged.
- 4:3 remains the default and must stay aligned; ROM-derived output stays in
  `work/`.
