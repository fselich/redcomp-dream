# Proposal: Uniracers widescreen objects (Phase E4)

## Why

The 16:9 background and live-race gate are done; P-U1 turned out not applicable
(the HDMA BG tables are empty and the curves are BG content). The wide frame
still uses the native cull/OAM windows, so opponents and objects can disappear
at the native edge or fail to draw in the margins. `WIDESCREEN_PATTERNS.md`
requires P7+P8 together before spawn/staging. This change finds the object,
cull and OAM path and widens it symmetrically, without touching spawn or
progression.

## What Changes

- Recon the object update/OAM path: the OAM emitter/reject compare and the cull
  windows. OAM setup is known at `$808665/$808669`; the reject/cull logic is not.
- Implement P7: widen each cull window symmetrically while preserving its base
  window.
- Implement P8: widen the OAM reject limit and replace the unsigned compare so
  objects straddling either margin still draw.
- Add independent kill switches (P14) for cull and OAM widening.
- Verify objects straddle both margins on the second track; 4:3 unchanged; no
  spawn or progression changes.
- Report the path, patches, evidence and remaining P9–P13.

## Non-goals

- Spawn/activation/staging (P9–P13), final HUD, netplay, audio.
- Simulation, collision, RNG or input changes.
- Player-facing polish.

## Capabilities

### New Capabilities

- `recomp-ws-objects`: symmetric cull/OAM widening for widescreen margins, with
  independent kill switches and margin evidence.

### Modified Capabilities

None.

## Impact

- Work under `work/ws-objects/`; the baseline and `ws-hdma` workspaces are
  preserved.
- May add project-local hooks in generated C or the object path; the framework
  pin is unchanged.
- 4:3 remains the default and must stay aligned; ROM-derived output stays in
  `work/`.
