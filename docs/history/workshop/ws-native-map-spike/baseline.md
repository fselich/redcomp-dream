# Native-map spike — baseline and preparation

Date: 2026-10-21
Change: `uniracers-native-map-spike`

## Baseline build

- Source copied from `work/ws-validation/project/UniracersSNESRecomp`.
- Game/framework scaffold commit: `a444654dad66859c35b6d4063dee4e926fe13700`
  (local project); framework pin `snesrecomp=9c6afcbfd4106e4613e522741d667047a48ded3b`.
- ROM: 2,097,152 bytes, SHA-256 recorded in `../rom.sha256`.
- Trace build: `SNESRECOMP_ENABLE_TRACE=ON`, Release.
- Keep normal frame pacing. `DisableFrameDelay=1` is **not** valid for
  deterministic captures; it changed frame scheduling in this environment.
- Remove `build/saves/save.srm` and `save.srm.bak` before each deterministic
  run. Persistent SRAM made two runs start from different game state.
- With pacing on and clean SRAM, two native Dragster runs at frame 2500 were
  byte-identical (`3417f91a...`).

## Track switches as found

The validated build and the archived experiment are not the same switch:

| Build | Switch | Behavior |
|---|---|---|
| `ws-validation` (copied here) | `SNESRECOMP_WS_TRACK_CLAMP` (presence) | BG1 clamp |
| `ws-track-fill` (experiment) | `SNESRECOMP_WS_TRACK=clamp\|off\|fill` | clamp / no clamp / shadow fill |

The spike therefore uses a **new** test switch for any wide-map prototype;
it does not reinterpret either existing switch.

## Reproduced frames

| Track | Route | Frames | Result |
|---|---|---|---|
| Dragster | `scripts/route-dragster.txt` | 3500 | native 256×224 and clamp 342×224 |
| Dragster | `scripts/route-dragster-move.txt` | 2500 | live race, rider moving, generator probe |
| Zoom Zoo | `scripts/route-zoomzoo.txt` | 3500 | native 256×224 and clamp 342×224 |
| Zoom Zoo | `scripts/route-zoomzoo-move.txt` | 3504 | live race, loop/slope, generator probe |

The moving routes hold `right` after NOW PLAYING; without input the rider
coasts/stops and later frames are race-complete screens, not live race.

## Captures

- `frames/dragster-native-3500.ppm`, `frames/dragster-clamp-3500.ppm`
- `frames/zoomzoo-native-3500.ppm`, `frames/zoomzoo-clamp-3500.ppm`
- `frames/dragster-move-2500.ppm`, `frames/zoomzoo-move-3500.ppm`
- `frames/*-layer01/02/04/10.*` for BG1/BG2/BG3/OBJ isolation
- Historical state: `dumps/dragster-3500-*`, `dumps/dragster-move-2500-*`,
  `dumps/zoomzoo-3504-*`
