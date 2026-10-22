## Context

From `uniracers-ws-offwindow` (see `docs/UNIRACERS_BG1_RING_FINDINGS.md`):

- In race, BG1 is 16×16 (`BGMODE = 0x19`). The ring is 32×32 cells (512×512
  px) at map base `0x0C00`.
- `map_x = ((hscroll + x) >> 4) & 31`, `map_y = ((vscroll + y) >> 4) & 31`.
  World cell = `(unwrapped_scroll + screen) >> 4`. This gives 0 conflicts.
- The native view samples 17 cells per row (`start..start+16`). A 43-px margin
  needs 3 more per side: `start-3..start-1` and `start+17..start+19`. That is 23
  of 32 ring cells, so the margin cells never overlap native cells.
- Unclamped 16:9: the trailing side is 100 % correct; the leading side is 47 %
  (+0/+1/+2: 70/36/36 %) on Dragster; Zoom Zoo is 99.9 / 97.7 %.
- Tools already in place: the renderer probe (`SNESRECOMP_BGPROBE_*`),
  `work/ws-offwindow/scripts/world_key_check.py`, the `build-local/` build, and
  the deterministic routes `route-dragster-move.txt` and `route-zoomzoo-move.txt`.
- Precedent: the test-only edge-repeat (`WsRepeatBegin`/`WsRepeatEnd` in
  `game_rtl.c`) already writes BG1 ring cells before rasterization and
  restores them afterwards, inside `GameDrawPpuFrame`.

## Goals / Non-Goals

**Goals:**

- Option A: every BG1 margin cell is either verified authentic or background.
  The result is never stale or wrapped.
- Option C: decide, with evidence, whether leading-edge cells can be sourced
  from the game's own data path, and prototype it if so.
- A measurable acceptance: 0 wrong served cells against the probe ground truth
  on all five 1P tracks; native 4:3 bit-identical.

**Non-Goals:**

- Inventing, repeating or stretching track (edge-repeat stays rejected).
- Changing guest camera, scroll, DMA timing, RNG, input or 2P.
- Making a mode the default in this change. That is a follow-up decision based
  on the report.

## Decisions

### D1 — Option A uses per-cell verification, not a direction heuristic

Each frame, after simulation and before drawing, the host updates a 32×32
**ring validity table**:

```text
for every ring cell (cx, cy) inside the native view this frame:
    seen[cx][cy] = { world_x, world_y, tile_word }      # from unwrapped scroll
```

A margin cell is served from the ring only if:

```text
seen[cx][cy].world == world cell the margin pixel needs
and VRAM[cell] == seen[cx][cy].tile_word                 # not rewritten since
```

Otherwise it is masked to background.

Why not "clamp the leading side by scroll-delta sign": that breaks on direction
flips, bounces, a stopped camera and mixed vertical motion, and it needs tuned
hysteresis. Per-cell verification follows the same rule in every case. It also
keeps the correct cells on Zoom Zoo (97–100 %) instead of clamping a whole side.

Alternatives considered: `ws_shadow` with `tileShift = 4` gives similar
history semantics. It is kept as the storage option for Option C, but Option A
does not need a world-keyed store. The ring already holds the data; only its
validity needs tracking.

### D2 — Masking mechanism: renderer substitution (final)

**Outcome:** a host-side draw bracket was prototyped and rejected. It produced
0 wrong cells, but the game's HUD bands reuse/repoint map entries, so writing a
ring cell during the field corrupted the timer glyphs (proved with a BG3-only
capture). The final mechanism is a host hook inside `PpuDrawBackgroundBig`
(`g_ppu_margin_tile_hook`): the host returns the verified tile or a transparent
one, and guest VRAM is never written. This also makes 4:3 exact by construction
(native samples are never passed to the hook). The bracket description below is
kept as the recorded alternative.

Reuse the edge-repeat pattern: in `GameDrawPpuFrame`, save the margin ring
cells that fail verification, write a transparent entry, rasterize, and
restore. This touches only cells outside the native 17, so the native view is
unaffected by construction. A VRAM hash before and after each frame must match,
so the guest never observes the bracket.

This refines `uniracers-ws-offwindow` D4 ("do not write the guest VRAM ring"):
a write inside the draw bracket that is restored before the guest runs is
allowed, provided the per-frame VRAM hash check passes. If the renderer reads a
VRAM snapshot asynchronously (render workers), fall back to a renderer-side
substitution hook in `PpuDrawBackgroundBig`, as a local patch.

The transparent entry is a tile word whose 16×16 character is fully
transparent. Use the `0x0000` entries observed in the probe, after checking that
their characters are empty.

### D3 — Option C: investigate before building

Order:

1. **Trace the leading-column write.** Use the WRAM/VRAM watch and the probe
   to find when and by whom ring cells `start+17..` are written while moving
   right. Is it the 17-wide row chunk on a later frame, or a separate column
   path?
2. **Row-builder extension.** In a test hook, run `$81:B3D0` with a widened
   count (`$76`) and a start shifted by −3, into a scratch buffer instead of
   `$0433`. Score the extra cells against ground truth with
   `world_key_check.py`.
3. **Synthetic call (fallback).** If the builder cannot be widened, call the
   resolver for the missing cells with full CPU/WRAM save and restore. Measure
   cost.

Sourced cells go into a host store (`ws_shadow`, `tileShift = 4`, or a
dedicated 32-cell-row buffer) and override the Option A mask. They never go
into guest WRAM or DMA.

Go/no-go for C: 0 wrong cells across the five tracks, VRAM/WRAM/CPU state
unchanged after the hook, and a cost budget of ≤ 0.5 ms/frame on the reference
machine.

### D4 — Switch and modes

`SNESRECOMP_WS_OFFWINDOW`:

| Value | Behaviour |
|---|---|
| unset / `0` | current behaviour (`SNESRECOMP_WS_TRACK_CLAMP` decides) |
| `ring` | Option A |
| `source` | Option A + Option C cells |

When set, it disables the global BG1 clamp and the repeat experiment for BG1.
It does not change HUD/BG3 or cull policies.

### D5 — Verification harness

Every acceptance run uses the probe on the same run. "Served" is the probe's
`served`/rendered tile for margin spans. `world_key_check.py` is extended to
score masked cells separately (masked ≠ wrong) and to report:

- wrong served cells (must be 0);
- masked cells that ground truth says are track (the coverage loss);
- coverage = correct served / verifiable margin cells.

## Risks / Trade-offs

- [Render workers read VRAM asynchronously] → check how `PpuRenderVram` is fed.
  If the bracket races, use the renderer-side hook (D2 fallback).
- [A transparent tile is not truly empty in every track's tileset] → validate
  the character data per track. Otherwise mask with the window/clamp mechanism
  restricted to the cell span.
- [Option A coverage on the leading side stays ~0 %] → accepted. It is never
  worse than the clamp, and the trailing side is a strict gain.
- [Seen-table invalidation misses a game write outside the view] → the
  tile-word equality check catches content changes. A same-word rewrite for a
  different world cell is harmless by definition.
- [Option C hook perturbs guest state] → full save/restore, plus a WRAM/CPU hash
  before and after, plus native bit-identical runs.
- [Seam flicker when a cell flips between served and masked] → measure the
  per-cell toggle rate. Add a minimum dwell only if visible.

## Migration Plan

Test switch only. Rollback = unset `SNESRECOMP_WS_OFFWINDOW`. Promotion to
default is out of scope and needs the report's go.

## Open Questions

- Does the renderer consume live VRAM or a snapshot during `GameDrawPpuFrame`?
- Which code writes the leading column during horizontal motion?
- Is `$76`/start of `$81:B3D0` parameterisable without side effects on `$023E`
  and `$0339`?
- Is the 70 % at +0 explained by the game having already written the next
  column (real data not yet seen natively)? If so, Option C step 1 may give a
  partial source for free.
