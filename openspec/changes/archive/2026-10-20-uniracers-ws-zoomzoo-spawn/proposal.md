# Proposal: Zoom Zoo spawn/staging recheck (Phase E5b)

## Why

The archived `uniracers-ws-spawn` N/A was measured on **Dragster, the first
track**. **Zoom Zoo is the second track**, with different visuals and HUD. A
track-specific N/A from the first track is not evidence for the second. P9–P13
must be rechecked on Zoom Zoo before the port claims there is no spawn or
staging work.

The correction also belongs in the capability spec: a spawn/staging N/A must
name the track it measured and must not be generalized across tracks without a
second-track check.

## What Changes

- Re-run the object/OAM, layer and DMA recon on **Zoom Zoo**.
- If Zoom Zoo has camera-spawned records, anchors, activation distances or
  stage triggers, implement the applicable P9–P13 work.
- If it does not, record a corrected N/A that names Zoom Zoo and Dragster
  separately.
- Add a track-scoped evidence requirement to `recomp-ws-spawn`.
- Do not rewrite the archived `uniracers-ws-spawn` report; this change carries
  the correction forward.

## Non-goals

- Reopening the HUD policy (already measured on both tracks).
- 2P/VS split-screen, gameplay, or RNG changes.
- Rewriting archived history.

## Capabilities

### Modified Capabilities

- `recomp-ws-spawn`: add track-scoped evidence for N/A results.

## Impact

- Work under `work/ws-zoomzoo-spawn/`; the HUD and prior workspaces are
  preserved.
- No code change unless Zoom Zoo actually has a spawn/staging path.
- 4:3 remains the default; ROM-derived output stays in `work/`.
