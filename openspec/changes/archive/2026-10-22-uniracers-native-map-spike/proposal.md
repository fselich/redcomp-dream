# Proposal: Uniracers native map spike (Phase F1)

## Why

The full-track maps prove that the complete level data exists. BG1’s 256×256
tilemap is only the runtime display window/ring; the level table in WRAM `$7F`
is much larger (~1,149 columns by stride `$8FA`). The BG1 clamp is a safe
interim, but a true wide track needs the full table and generator.

Before committing to a native map pipeline, this spike must prove that we can
map the full level table to world/screen coordinates and generate real margin
columns. If it cannot, the clamp remains the final answer.

## What Changes

- Decode the full level table layout (`$7F000F`, stride, rows, columns).
- Map world X/Y ↔ table column/row ↔ screen X/Y using the PPU scroll and the
  game’s camera.
- Validate the mapping against the native 4:3 tilemap and the VGMaps full-track
  maps as an external oracle.
- Evaluate stage 1 (`$81:8A4A`) and stage 2 (`$81:8B95`) for margin columns,
  using AOT or a synthetic guest call.
- Build a **test-only** host-side wide BG1 tilemap/prototype for 342×224.
- Measure fragments, duplicates, 4:3, simulation and cost.
- Produce a go/no-go report for a production native-map change.

## Non-goals

- New art or procedural track content.
- Replacing the whole map pipeline before the spike passes.
- Gameplay, camera, RNG, 2P/VS.
- Shipping the prototype as the default renderer.

## Capabilities

### New Capabilities

- `recomp-native-map-spike`: evidence and prototype for a source-backed wide
  BG1 map.

### Modified Capabilities

None.

## Impact

- Work under `work/ws-native-map-spike/`.
- Test-only host code and/or synthetic calls; no default gameplay changes.
- Clamp remains the default until a later accepted implementation.
- Full maps are external references; do not commit the images.
