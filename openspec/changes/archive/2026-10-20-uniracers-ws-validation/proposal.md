# Proposal: Uniracers widescreen validation (Phase E7)

## Why

The renderer, cull/OAM and HUD work are in place. P9–P13 are N/A on both
Dragster (first track) and Zoom Zoo (second track), each measured separately.
The remaining risk is per-track variation: HUD labels, arrow side, track art
and transitions differ between tracks, and the release gate requires 4:3 to
stay exact.

This change is the final 1P validation and polish pass: capture every track,
verify the HUD band covers each top-left group without clipping, recheck the
CPU ghost at the margins, and prove menus/pause/transitions and 4:3 are
unchanged. It fixes only presentation issues found by the matrix.

## What Changes

- Build a track matrix for the five 1P tracks (Dragster, Zoom Zoo, Bowl,
  Switcher, Monster; `CRAWLER` is the league heading, not a track).
- Capture native/wide composite, BG3-only and object landmarks per track.
- Verify the HUD top-band segments cover the widest top-left group and the
  timer; adjust the measured span only if a track needs it.
- Recheck the CPU ghost/OAM margins with the P7/P8 probes.
- Verify menus, race start, pause and track transitions.
- Diff 4:3 and WS-off frames against the preserved baseline.
- Record any per-track limitation; no gameplay/simulation changes.

## Non-goals

- 2P/VS split-screen, spawn/staging, new mechanics.
- Rewriting archived changes or the N/A results.
- New background policies; the BG margins are already natural.

## Capabilities

### New Capabilities

- `recomp-ws-validation`: per-track 16:9 acceptance evidence and presentation-
  only polish.

### Modified Capabilities

None.

## Impact

- Work under `work/ws-validation/`; all prior workspaces are preserved.
- Only presentation fixes if the matrix finds a defect.
- 4:3 remains the default; ROM-derived output stays in `work/`.
