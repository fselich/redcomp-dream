# Proposal: Uniracers widescreen spike (Phase E1)

## Why

The camera is now AOT and the renderer is mapped: Mode 1, BG1/BG2 parallax,
HDMA per-line scroll, BG3 HUD. The next goal is true 16:9, but the framework's
`WIDESCREEN_PATTERNS.md` requires P16 (4:3 baseline) first, then P5/P6 (live-race
discriminator), backgrounds, HUD, cull/OAM and spawn. This change is a bounded
spike: prove a genuinely wider field at a fixed race state, identify the HDMA
table builder for P-U1, and document the object/cull gaps. It does not claim a
finished 16:9 mode.

## What Changes

- Preserve the dispatch-fix baseline and its aligned `k=3` gate.
- Find and verify a live-race discriminator (P5/P6) across menu, race and pause.
- Enable the host's native widescreen at 342×224 (16:9 from 4:3) behind a
  runtime flag, keeping 4:3 the default.
- Apply renderer-level BG1/BG2 margin policies and a BG3 HUD clamp, then capture
  4:3 vs 16:9 at the same race state.
- Trace the HDMA table builder and document or implement the P-U1 per-line
  extension.
- Report what is genuinely wider, what breaks (objects, margins, HUD, curves),
  and the next changes (P7/P8, P9–P13).

## Non-goals

- Full cull/OAM/spawn implementation (P7–P13).
- Final HUD, netplay, audio, release, or a playable 16:9 build.
- Simulation changes (camera, collision, RNG) or input changes.

## Capabilities

### New Capabilities

- `recomp-widescreen-spike`: a bounded, flag-gated 16:9 renderer spike with
  doctrine-gated evidence and a gap report.

### Modified Capabilities

None.

## Impact

- Work under `work/ws-spike/`; the baseline and dispatch workspaces are
  preserved.
- Changes the project host descriptor/config and, if needed, project-local
  generated-C hooks; the framework pin is unchanged.
- 4:3 remains the default and must stay aligned-equivalent.
- ROM-derived output stays in `work/`; the report contains counts and addresses
  only.
