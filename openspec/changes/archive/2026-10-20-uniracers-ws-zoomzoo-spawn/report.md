# Zoom Zoo spawn/staging recheck (Phase E5b) — report

Date: 2026-10-20
Change: `uniracers-ws-zoomzoo-spawn`
Outcome: **P9–P13 N/A for Zoom Zoo.** The second track has the same fixed
two-rider OAM population, no camera-spawned records, no large CHR stage swap,
and only 32-byte tilemap-column DMAs. The Dragster N/A is now backed by a
separate second-track measurement.

## Track identity

- Dragster = first track.
- Zoom Zoo = second track.
- Route: PICK TRACK → `Down` → Zoom Zoo; NOW PLAYING reads
  `OVER 3 LAPS ON ZOOM ZOO`.

## Evidence

### OAM

- 1800/2500/3500 eight-snapshot rings: `active=5` always.
- Slots: 0 parked, 96/97 parked 2P pair, 98 player (`x=104`), 99 CPU ghost.
- No new slot or allocation appears during the race.

### Layers

- OBJ-only at 3500: only the player sprite, no track objects.
- Composite: zoo track, road and HUD.
- Track furniture is BG, not spawned records.

### DMA

- 1800: 25 transfers, all 32-byte to `$2118`.
- 2500: 23 transfers, all 32-byte to `$2118`.
- 3500: 33 transfers, all 32-byte to `$2118`.
- No large CHR block transfer or discrete section swap.

### Result

| Doctrine | Zoom Zoo |
|---|---|
| P9 ordinary spawn anchors | **N/A** — no records |
| P10 dual pass | **N/A** — no record walk |
| P11 balanced synthetic call | **N/A** — no native pass |
| P12 large-object activation | **N/A** — no large spawned objects |
| P13 X-axis stage lead | **N/A** — no discrete CHR/tilemap stage trigger |

## Spec correction

The change adds the **Track-scoped N/A evidence** requirement to
`recomp-ws-spawn`: an N/A must name the measured track and must not be
generalized to a second track without its own evidence. The archived
`uniracers-ws-spawn` report is not rewritten; this report carries the
correction forward.

## 4:3 and simulation

No source change was made. The native 256×224 Zoom Zoo frame remains the
baseline; no RNG, progression, collision, camera or input was touched.

## Artifacts

- `work/ws-zoomzoo-spawn/zoomzoo-spawn-path.md`
- `frames/zoomzoo/`, `frames/layers/`, `frames/native-3500.png`
- `logs/landmark-1800/2500/3500.json`
- `metrics/zoomzoo-hashes.txt`

## Next

Zoom Zoo is N/A for spawn/staging. Remaining work is final per-track visual
validation/polish; no P9–P13 code is needed for 1P.
