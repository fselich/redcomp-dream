# Design: Uniracers native map spike (Phase F1)

## Context

- Full-track maps confirm long courses with loops; the data exists.
- BG1’s runtime map is 32×32 (256×256); it is a window/ring, not the level.
- The level table in WRAM `$7F` is much larger: one observed stride is `$8FA`
  (~1,149 columns at 16-bit entries).
- The generator pipeline is:

```text
$7F000F / $7F800F
  -> $81:8A4A (segment IDs $0260..)
  -> $81:8B95 (tiles $0230/$0231 via $7E:C000/$7E:A000)
  -> BG1 column in VRAM
```

- A synthetic call to `$81:8B95` works from C with save/restore, but the
  offset/world mapping used in the earlier scan was wrong; blank results were
  not evidence of missing data.
- The validated clamp build uses the boolean `SNESRECOMP_WS_TRACK_CLAMP`; the
  archived fill experiment used `SNESRECOMP_WS_TRACK=clamp|off|fill`. The
  spike's prototype must use a new, dedicated switch and must not redefine
  either baseline.

## Goals / Non-Goals

**Goals:**

- Decode the full level table and its row/column layout.
- Prove the world↔tile↔screen mapping against the native 4:3 tilemap.
- Use the full maps as an external oracle for whole-track shape.
- Generate real margin columns with stage 1/2.
- Render a test-only wide BG1 map (342×224).
- Decide go/no-go for a production native-map pipeline.

**Non-Goals:**

- New art/procedural content.
- Production replacement before the spike passes.
- Gameplay/camera/RNG/2P changes.
- Committing external map images.

## Decisions

### D1 — Decode the full table

Characterize:

- table base(s) `$7F000F` / `$7F800F`;
- row stride (`$0236`, observed `$8FA`), row count, column count;
- entry format and attributes (`$00A0` blank, track entries);
- how the source pointer (`$023E`) selects a column;
- how stage 1/2 consume it.

Dump full WRAM `$7E`/`$7F` at a loop frame and build an offline map of table
cells, marking blank vs track.

### D2 — World/screen mapping

Use:

- camera/world position (`$00A5/$00A7`, plus scroll mirrors);
- PPU scroll (`hScroll/vScroll`) as the presentation phase;
- native 4:3 screen columns as ground truth.

For each native tile column, find the table column that reproduces its VRAM
tile entries. Then extrapolate ±43 px to margin columns. Record the formula and
its units; do not guess offsets.

### D3 — External oracle

Use the VGMaps full-track images only for validation:

- compare overall track shape/loops;
- do not embed or redistribute the images;
- do not use them as a runtime data source.

### D4 — Generator evaluation

Use either:

- AOT promotion of stage 1/2 with explicit M/X variants; or
- the synthetic call harness (`cpu_dispatch_call_pc($81:8B95)` with full
  CpuState + WRAM save/restore).

Synthetic calls are acceptable for the spike; production would prefer AOT or a
pure C function.

### D5 — Prototype wide BG1

Build a test-only host-side BG1 tilemap/prototype:

- 342 columns using true native + margin cells;
- clamp/fill switch independent of HUD/cull;
- no guest register/camera/DMA changes;
- if the mapping fails, stop and keep clamp.

### D6 — Acceptance / go-no-go

Spike passes only if:

- native 4:3 tilemap cells match the decoded table;
- margin columns match the full-track map shape and have no wrap fragments;
- loops/slopes both directions;
- 4:3 and simulation unchanged;
- cost is acceptable.

Otherwise record the blocker and keep clamp.

## Risks

- **Wrong coordinate scale** → duplicate/missing track; stop.
- **Hidden generator state** → synthetic call may depend on more than saved.
- **Ring/streaming timing** → margin columns may not be available at the same
  frame; use the full table, not VRAM.
- **VRAM/tilemap override complexity** → prototype only.
- **Performance** → synthetic calls per column are expensive; AOT/pure C later.

## Open Questions

- What is the exact table row/column layout?
- How do `$00A5/$00A7` map to table columns and PPU scroll?
- Does the generator output all 32 rows or only a track band?
- Can the wide map be rendered without changing guest VRAM?
- Is a pure `TrackColumn()` extraction simpler than synthetic calls?
