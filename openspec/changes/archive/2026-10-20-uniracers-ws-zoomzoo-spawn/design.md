# Design: Zoom Zoo spawn/staging recheck (Phase E5b)

## Context

- **Dragster is the first track; Zoom Zoo is the second.**
- The archived `uniracers-ws-spawn` change recorded P9–P13 as N/A after
  measuring Dragster only. That is a track-scoped result, not an engine-wide
  one.
- Zoom Zoo has different track art and HUD (`0/3`/`1/3`, ghost delta), so it
  must be measured independently.
- P7/P8 and the HUD top-band policy are already in place; this change does not
  revisit them.

## Goals / Non-Goals

**Goals:**

- Measure Zoom Zoo's OAM population, BG/OBJ layers and background streaming.
- Locate any camera-spawned record table, spawn anchor, activation distance or
  X-axis stage trigger.
- Implement P9–P13 only if Zoom Zoo actually has them.
- Record a track-scoped N/A otherwise.
- Add the track-scoped evidence requirement to `recomp-ws-spawn`.

**Non-Goals:**

- HUD changes, P7/P8, 2P/VS, gameplay/RNG changes.
- Rewriting the archived change.

## Decisions

### D1 — Zoom Zoo recon

Use the working Zoom Zoo route (`Down` on PICK TRACK) and capture:

- OAM render/write rings over the race;
- BG3-only, OBJ-only and BG-only frames;
- per-frame DMA destinations/sizes;
- WRAM landmarks at race start, ghost pass and late race.

### D2 — Spawn/staging applicability

Apply the same tests used on Dragster:

- Are there OAM slots or records beyond the two riders?
- Are track objects BG or spawned records?
- Is there a large CHR/tilemap stage swap tied to camera X?

If all objects are BG and OAM is fixed, record N/A for Zoom Zoo explicitly.

### D3 — Implementation only if found

If Zoom Zoo has a record walk:

- follow P9–P13 exactly as in `recomp-ws-spawn`;
- native pass first, ordinary-only widened pass, balanced synthetic call,
  large-object activation, X-only stage lead;
- independent spawn/stage switches.

Do not invent a record walk for a track that has none.

### D4 — Spec correction

Add a requirement to `recomp-ws-spawn`:

- an N/A must name the track measured;
- a second track with different content must be tested before generalizing;
- the result is per-track, not engine-wide.

### D5 — Workspace

```text
work/ws-zoomzoo-spawn/
├── project/       # copy of the ws-hud build
├── logs/          # OAM/layer/DMA/WRAM
├── frames/        # Zoom Zoo native/wide/layer captures
├── metrics/
└── patches/       # only if a spawn path exists
```

## Risks

- **Route selection** → confirm `NOW PLAYING … ON ZOOM ZOO` before capturing.
- **Different HUD** → layer isolation may show `0/3` and arrows; do not treat
  HUD text as objects.
- **False N/A** → require the same OAM/layer/DMA evidence as Dragster.
- **Scope creep** → HUD is already done; do not retune it here.

## Open Questions

- Does Zoom Zoo use camera-spawned records for animals/obstacles, or is the
  whole zoo BG?
- Does it stream CHR in sections or column-by-column?
- Are the `<<<`/`>>>` arrows HUD only, or do they correspond to track objects?
