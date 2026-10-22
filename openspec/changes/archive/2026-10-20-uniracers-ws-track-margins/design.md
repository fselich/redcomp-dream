# Design: Non-horizontal track margins (Phase E8)

## Context

Validation reproduced the bug on Zoom Zoo:

- native 4:3 frame 5000: a continuous S-curve;
- wide 16:9 frame 5000: the S-curve plus a detached blue/green segment at the
  right edge;
- BG1-only confirms the fragment is in the track layer;
- PPU state: BG1 `hScroll=776`, `bgXsc[0]=0x0C` = 32×32 (256×256) tilemap.

The extra 43 px margins sample the wrapped 256-wide map. Earlier background
work measured Dragster/early frames and concluded margins were natural; it did
not exercise loops/slopes on Zoom Zoo. The `SNESRECOMP_WS_TRACK_CLAMP=1` probe in
validation keeps BG1 native and removes the fragments, but loses track margin
coverage.

## Goals / Non-Goals

**Goals:**

- Characterize the track map, scroll and per-column streaming on loops/slopes.
- Determine whether the game has the true off-native track columns (retained
  history, streaming ahead, or level data).
- Fill margins from real data if available; clamp safely if not.
- Add an independent `SNESRECOMP_WS_TRACK` switch.
- Verify 4:3 and both loop directions; no simulation changes.

**Non-Goals:**

- HUD, cull/OAM, spawn/staging, 2P/VS.
- Synthesizing track art not present in the game.
- Changing the track scroll or camera.

## Decisions

### D1 — Track-layer recon

Capture wide/native and BG1-only frames at loop/slope landmarks. Record:

- BG1 screen base/size and tilemap address;
- hScroll/vScroll and whether the map is a ring or a fixed 256-wide world;
- per-frame VRAM writes/column streaming;
- whether the game writes columns before/after the native edge;
- whether the left margin was previously visible (history).

### D2 — Proper margin fill

If the game retains true off-native columns:

- key the history to the PPU-rendered scroll, not a WRAM mirror (P4);
- prefill left/right margins from the retained columns without advancing the
  guest camera, DMA queue or rolling-map bookkeeping;
- never sample the wrapped map outside the authored world.

If the game has no retained data (the 256 map is all that exists), proper fill
is impossible without inventing geometry; go to D3.

### D3 — Safe clamp fallback

Clamp BG1 to the native 256 columns:

```c
PpuSetWidescreenLayerClamp(ppu, 1u << 0);
```

This removes the disconnected fragments and leaves the background wide. Record
that the track does not extend into the margins. Do not use mirror/repeat as a
substitute: they would invent visible track geometry.

### D4 — Switch

`SNESRECOMP_WS_TRACK`:

- `fill` — proper retained-data margins;
- `clamp` — safe native track;
- `off` — pre-change natural sampling (for comparison only).

The switch is independent of HUD/cull.

### D5 — Verification

- 4:3 and WS-off frames unchanged.
- Wide loop/slope frames show no disconnected segments.
- Both travel directions and both margins.
- No track/camera/RNG/simulation state changes.
- Record whether the final mode is fill or clamp.

### D6 — Workspace

```text
work/ws-track-margins/
├── project/       # copy of the ws-validation build
├── logs/
├── frames/        # native/wide/BG1 loop captures
├── metrics/
└── patches/
```

## Risks

- **No true off-native data** → clamp and document; do not invent track.
- **History keyed to the wrong scroll** → use PPU per-line scroll (P4).
- **Ring-buffer wrap** → prove the authored world span before sampling.
- **Performance** → measure the fill cost.
- **Regression** → 4:3 must stay exact.

## Open Questions

- Is BG1 a fixed 256-wide world, a ring buffer, or a wider map with a streaming
  window?
- Does the game write columns ahead/behind the native edge?
- Can the true margins be recovered from history, or only clamped?
- Are the detached pieces actual other track sections or wrap artifacts?
