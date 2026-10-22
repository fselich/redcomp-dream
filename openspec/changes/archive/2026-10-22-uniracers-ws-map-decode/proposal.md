## Why

The 16:9 leading edge is being solved at runtime by the ghost lane
(`uniracers-ws-ghost-lane`). That approach can only show cells the game itself
draws in a near future. Three goals need the track **as data**:

- an **ultrawide** evaluation (21:9 and wider needs cells far outside any
  plausible camera path);
- a cheap, snapshot-free runtime source for low-end targets;
- longer term, tooling for **new or edited tracks**.

Option C of `uniracers-ws-leading-edge` was no-go only because the `$7F`
segment layout was undecoded. New evidence (2026-10-21) makes it much more
tractable:

- `$7F` already holds final BG1 tile words, not intermediate codes. At Zoom Zoo
  3504 all 108 distinct BG1 map words are present in the high table at
  `$7F800F`, and about half of the 4-cell row windows appear there byte-for-byte.
- The layout is regular. Inside the high table, +2 bytes = one cell right
  (within groups of 4 cells) and +8 bytes = one map row down. The track is
  assembled from **4-cell-wide pieces** that are placed in world space.

This is a time-boxed **spike**. It decodes the map storage and decides whether
a direct track source (WRAM first, then ROM) is viable.

## What Changes

- Decode the WRAM `$7F` representation:
  - the piece library (high table);
  - the placement/index data (low table and/or a list built by the stage-1
    code);
  - a `world cell → BG1 tile word` function validated against probe ground
    truth.
- Trace how `$7F` is filled at race load and identify the ROM source format
  (compression, per-track pointers).
- Offline tool: render each full track's BG1 layer to an image from a WRAM
  snapshot, and ideally from the ROM alone.
- Feasibility notes for ultrawide (cells needed per aspect ratio) and for track
  editing (what else a new track needs: collision, objects, palettes).
- Go/no-go for a runtime source (`SNESRECOMP_WS_OFFWINDOW=source`), to be built
  in a later change.

## Capabilities

### New Capabilities

- `recomp-ws-map-decode`: documented and validated decoding of the Uniracers
  BG1 track map (WRAM, then ROM), an offline full-track renderer, and a
  go/no-go for a runtime map source.

### Modified Capabilities

None.

## Impact

- Analysis scripts and notes under `work/ws-map-decode/` (gitignored,
  ROM-derived).
- Runtime experiments in a **separate copy** of the project
  (`work/ws-map-decode/project/`), never in the shared
  `work/ws-native-map-spike/project/UniracersSNESRecomp` tree or its
  `build-local/`. That tree is being modified by the ghost-lane change in
  parallel.
- No change to any presentation mode or default.
- Licensing: any extracted map or image is ROM-derived; keep it local.
- Working findings: `work/ws-map-decode/notes/findings.md`; full file list in
  `design.md` → "Artifact index".
