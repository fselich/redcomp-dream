# Design: Uniracers widescreen spawn and staging (Phase E5)

## Context

P7/P8 are done. The wide frame is 342×224, gated to live race; the cull window
is shifted to `[-92,299]` and signed OAM X already draws in the left margin.
4:3 is bit-identical with the enhancement off.

Known facts:

- 1P is full-screen against a CPU ghost. Player OAM slot 98, ghost slot 99;
  HUD is BG3. 2P/VS are horizontal split-screen and out of scope.
- The route selects **Dragster**, the second entry in PICK TRACK; its road runs
  **left**.
- The cull words `$0421/$0423` are presentation-only; no spawn/AI/RNG reader.
- No object record table, spawn anchor, activation distance or stage trigger
  has been mapped yet. The track may render some objects as BG rather than
  spawning records.

## Goals / Non-Goals

**Goals:**

- Find the object record walk, spawn anchors, activation distances and stage
  triggers, or prove they do not exist for this engine.
- Widen ordinary object anchors by margin + 32 (43 + 32 = 75 px).
- Keep progression/controllers on the unmodified 4:3 anchor via a dual pass.
- Make the native pass a balanced synthetic call.
- Widen large-object activation distance and bias only X-axis stage triggers.
- Prove 4:3, RNG and progression are unchanged; capture spawn/staging evidence.

**Non-Goals:**

- P7/P8 cull/OAM, final HUD, netplay, audio.
- Simulation, collision, AI or RNG changes.
- 2P/VS split-screen.

## Decisions

### D1 — Find the spawn/staging path

Use the debug server plus WRAM/block traces on Dragster:

- locate the object record table, allocation/free path and per-record flags;
- find the spawn anchor compare (the value compared with camera X);
- find activation-distance checks for large objects;
- find X-axis stage/tilemap/CHR triggers and their direction conditions;
- record writer PCs, addresses and frame ranges.

If the track's objects are entirely BG and no camera-spawned record exists, the
change records P9–P13 as **not applicable** with that evidence and stops.

### D2 — P9 ordinary spawn anchor

Widen only ordinary object records by margin + 32:

```c
spawn_wide = spawn_base + 75;   /* 43 px margin + 32 px slack */
```

Keep the base anchor for progression records, controllers and any record that
is not an ordinary object. Do not unify enemy, projectile and stage triggers.

### D3 — P10/P11 dual pass

Preserve the native record walk:

1. Run the normal 4:3 pass first so progression, controllers, slot allocation
   and RNG keep their original order.
2. Run a second pass with the widened ordinary-object anchor only.
3. Mark records created by the native pass so the widened pass is a no-op for
   them; do not duplicate enemies.

If the engine's walk cannot be re-entered, or the second pass changes RNG
order, record the limitation and do not claim P10. The synthetic call must save
and restore `CpuState`, restore the anchor bytes afterward, and leave the guest
stack balanced (P11).

### D4 — P12 large-object activation

Large objects wake on distance, not only on the anchor. Widen that activation
distance by the same 75 px so outer tiles do not pop in after the centre enters
the margin.

### D5 — P13 stage lead

Bias only X-axis staging watchers *into the travel direction* by the 43 px
margin, and keep the two direction conditions complementary at every instant to
avoid a fire loop. Do not bias Y staging (there is no vertical margin) and do
not move camera locks or scripted triggers.

### D6 — Independent switches

Provide `SNESRECOMP_WS_SPAWN` (P9/P10/P12) and `SNESRECOMP_WS_STAGE` (P13).
Either must be disableable without disabling the other, so a bad staging swap
can fall back while spawn stays wide.

### D7 — Verification

- 4:3 and WS-off captures match the preserved baseline.
- RNG, slot allocation and progression records match the native run.
- Ordinary objects appear in the margins without pop; large objects show their
  outer tiles; staging does not swap CHR before the old section leaves.
- If a pass changes progression/RNG, revert it and record the first divergent
  frame.

### D8 — Workspace

```text
work/ws-spawn/
├── project/       # copy of the ws-objects build
├── patches/       # spawn/stage hooks and switches
├── logs/          # record traces, RNG/progression dumps
├── frames/        # wide/native spawn and staging captures
└── metrics/
```

## Risks / Trade-offs

- **No camera-spawned objects** → record N/A; do not invent records.
- **Progression/RNG order** → native pass first; prove state equivalence.
- **Synthetic call stack** → save/restore CpuState and anchor bytes.
- **Slot exhaustion** → widened pass may need free slots; record failures.
- **Early CHR swap** → P13 lead only the margin; test both travel directions.
- **Split-screen modes** → out of scope; do not widen them.

## Open Questions

- Does 1P Dragster use a camera-spawned object record table, or are all track
  objects BG?
- Where is the spawn anchor compared, and which records are ordinary vs
  progression/controllers?
- Does the record walk re-enter safely for the widened pass?
- Which stage triggers are X-axis, and how are their direction conditions
  expressed?
