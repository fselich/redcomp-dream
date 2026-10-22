# Proposal: Uniracers race reconnaissance (Phase C)

## Why

Phase B proved that the scaffolded executable boots headless to the main menu and
is playable, but **100% of the executed CPU code is interpreted**: the only AOT
functions are the four IRQ variants, which never fire during boot. The coverage
capture covers boot → menu only.

The final goal is a native port with enhancements such as true 16:9. That
requires knowing how the race renders, where the camera, viewport and culling
are set, and which functions must become AOT before the framework's
generated-C widescreen hooks can be injected. None of that is measurable from a
menu-only capture.

A preliminary route check already reached a race and dumped its PPU state: the
track is **Mode 1**, not Mode 7, with BG1/BG2 parallax and HDMA-driven per-line
horizontal scroll. The 2D doctrine is closer to applicable than expected, but
the HDMA scroll tables, margin streaming, HUD and object culling still need to
be mapped before a 16:9 prototype. The next cheapest experiment is to make that
map.

## What Changes

- Add a deterministic input script from the menu into a 1P race, using the
  host's `--script` grammar (`press`, `wait`, `until`, `dump`).
- Capture a bounded race segment: periodic frames, tier2 coverage, and
  PPU/VRAM/WRAM state dumps.
- Ingest the race capture against the same build manifest and identify race hot
  paths separately from the menu paths.
- Correlate hot PCs with PPU register writes to identify renderer modes and
  layers, HDMA usage, and camera/viewport/culling candidates.
- Produce a report with the route, evidence, renderer map, and a concrete 16:9
  feasibility assessment, including AOT prerequisites.
- Keep all derived output under `work/race-recon/` (gitignored).

## Non-goals

- Implementing 16:9 or any visual enhancement.
- Promoting functions to AOT or modifying generated C.
- Completing a race, audio validation, performance tuning, netplay, or mods.
- Full disassembly or decompilation; only targeted inspection justified by
  captured PCs.
- Redistributing ROM-derived output.

## Capabilities

### New Capabilities

- `recomp-race-recon`: deterministic gameplay-route capture and renderer
  reconnaissance for a scaffolded SNESRecomp project, producing a renderer map
  and a widescreen feasibility assessment.

### Modified Capabilities

None.

## Impact

- Adds a runtime reconnaissance step; no product code.
- Base: the pinned boot-spike build (`9c6afcbf…`), same ROM digest.
- Workspace: `work/race-recon/` (gitignored); the repository receives only the
  report and non-derived metrics.
- Runtime is bounded by `SNESRECOMP_RUN_FRAMES` and a wall-clock timeout.
- The user's manual play can supply the initial menu route; the script makes it
  repeatable and diffable.
