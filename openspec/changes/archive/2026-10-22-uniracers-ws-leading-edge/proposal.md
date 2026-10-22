## Why

`uniracers-ws-offwindow` proved the BG1 ring formula (16×16 tiles, 512 px
ring) and measured the unclamped 16:9 margins. The trailing side already holds
authentic world cells (100 % on Dragster). Only ~3 leading-edge cells per row
are stale (47 % on Dragster). The global BG1 clamp therefore throws away real
track on the trailing side of every horizontal section. The only remaining gap
is a source for the leading cells (`docs/UNIRACERS_BG1_RING_FINDINGS.md`).

This change turns those findings into a test-only prototype along the two
options identified there.

## What Changes

- **Option A, verified ring cells:** serve each BG1 margin cell from the ring
  only when its world key and tile word were confirmed in the native view. Mask
  every unverified cell to background. This gives a per-cell clamp instead of a
  per-layer clamp. It needs no guest changes and no direction heuristic.
- **Option C, leading-edge source:** investigate how the game writes the
  leading column, and whether the row builder `$81:B3D0` / `$7F` segment lookup
  can produce `start-3..start-1` and `start+17..start+19`. If it can, feed those
  cells to the margins so masked leading cells become real track. A synthetic
  guest call is the fallback route inside this option.
- One dedicated test switch, `SNESRECOMP_WS_OFFWINDOW`, with modes `ring`
  (Option A) and `source` (A + C). The global BG1 clamp stays the default.
- Validation against the probe ground truth on all five 1P tracks, plus 4:3
  bit-identical, plus cost.
- Go/no-go per option. Option A can ship as an improvement over the clamp even
  if Option C is no-go.

## Capabilities

### New Capabilities

- `recomp-ws-leading-edge`: per-cell verified BG1 margin presentation (Option
  A) and source-backed leading-edge cells (Option C), behind a test switch.

### Modified Capabilities

None. `recomp-ws-offwindow` is still an active change. This change implements
its prototype and verification sections (5–6) and its leading-source tasks
(3.4–3.5).

## Impact

- `work/ws-native-map-spike/project/UniracersSNESRecomp/src/game_rtl.c`
  (host-side draw bracket, ring validity table).
- Possibly a small framework hook in `snesrecomp/runner/src/snes/ppu.c` if a
  per-cell mask cannot be expressed host-side. It would be a local patch next
  to the existing `bgprobe.diff`.
- Option C may need a generated-C or synthetic-call hook around `$81:B3D0`.
  This must be presentation-only.
- Work artifacts under `work/ws-leading-edge/` (gitignored, ROM-derived).
- No gameplay, camera, RNG, input or 2P changes. Clamp remains the default until
  a mode is accepted.
