# Proposal: Uniracers widescreen spawn and staging (Phase E5)

## Why

P7/P8 are complete: the cull window is `[-92,299]`, signed OAM X draws in the
left margin, and 4:3 is unchanged. The wide frame can still pop objects at the
native spawn edge because anchors, activation distances and stage triggers are
authored for 4:3. `WIDESCREEN_PATTERNS.md` puts P9–P13 next: widen ordinary
spawn anchors by margin + 32, keep progression on a native pass, widen
large-object activation, and bias only X-axis staging by the margin.

1P is full-screen against a CPU ghost; 2P and VS are split-screen and out of
scope. This change first finds the object record walk and staging triggers on
Dragster (the second track; its road runs left), then implements only the parts
the engine actually has. If the engine has no camera-spawned records, the
change records that with evidence instead of inventing spawn logic.

## What Changes

- Recon the object record table, spawn anchors, activation distances and stage
  triggers on Dragster.
- P9: widen ordinary object spawn anchors by margin + 32, preserving the base.
- P10/P11: dual pass — native anchor for progression/controllers, widened pass
  only for ordinary records; balanced synthetic call with CpuState saved.
- P12: widen large-object activation distance by margin + 32.
- P13: bias only X-axis stage triggers by the margin, keeping direction
  conditions complementary; leave Y and camera locks native.
- Independent kill switches `SNESRECOMP_WS_SPAWN` and `SNESRECOMP_WS_STAGE`.
- Verify 4:3, RNG and progression; capture wide spawn/staging evidence.

## Non-goals

- Reopening P7/P8 cull/OAM; final HUD (P2c), netplay, audio.
- Simulation, collision, AI or RNG changes.
- 2P/VS split-screen widescreen.

## Capabilities

### New Capabilities

- `recomp-ws-spawn`: margin-aware spawn anchors, dual-pass progression, large
  object activation and X-only stage lead, with independent switches.

### Modified Capabilities

None.

## Impact

- Work under `work/ws-spawn/`; the baseline and `ws-objects` workspaces are
  preserved.
- May add host-side hooks or generated-C overrides; the framework pin is
  unchanged.
- 4:3 remains the default; ROM-derived output stays in `work/`.
