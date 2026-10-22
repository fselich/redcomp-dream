# Design: Uniracers HDMA margin extension (Phase E3)

## Context

The 16:9 preview renders 342×224 with the HUD clamped, gated to live race. The
margins currently show the PPU's natural sampling of BG1/BG2; the per-line HDMA
scroll still covers only the native 256 columns. On flat track sections this
looks plausible; on curves and hills the margin seam is expected to diverge.

Phase C established:

- HDMA ch3 → `$210F` (BG2HOFS), ch4 → `$210D` (BG1HOFS), mode 3.
- Current A-bus pointers `$7E:8262` (ch4) and `$7E:8280` (ch3); the per-frame
  block loads the ch4 A1T from `$7E:2046`.
- The WRAM at the current pointers was zero at the dump boundary, so the table
  is consumed/rebuilt per frame.

The builder was not found. This change finds it and extends the per-line values
for the 43-pixel margins.

## Goals / Non-Goals

**Goals:**

- Find the writer PC and table layout for the per-line HDMA tables.
- Understand how per-line scroll is derived from the camera/track.
- Extend the table for the margin columns, or document P-U1 blocked with
  evidence.
- Prove 4:3 unchanged and the wide seam matches the native curve on a curve or
  hill.

**Non-Goals:**

- Cull/OAM/spawn/staging, final HUD, netplay, audio.
- Simulation, collision, RNG or input changes.
- Guessing curve values that the game did not author.

## Decisions

### D1 — Trace the table writes

Use a trace build and the debug server:

```text
set_wram_watch 7E 8262 <width> ...
set_wram_watch 7E 8280 <width> ...
set_wram_watch 7E 2046 2 ...
wram_watch_log_get 7E8262
```

Record the writer PC, the written width/value and the frame range. Watch a
window around the pointers and the pointer variable, not just one address, so
pointer-relative stores are caught.

*Alternative:* static scan for stores to `$8262`/`$8280`. Rejected: the builder
may use `STA [$ptr],Y` or a copied routine.

### D2 — Map the table layout

Mode 3 uses a count byte followed by the line entries (and a zero terminator).
Determine:

- table base and end;
- number of lines and the count blocks;
- the value written per line and how it maps to `$210D`/`$210F`;
- whether the table is rebuilt every frame or from a ROM curve table.

Record the first/last lines and the camera/scroll inputs.

### D3 — Pick a curve or hill section

Use the deterministic route and wide captures to find frames where the track
curves or changes height, so the seam is visible. The flat section at frame
2400 is not sufficient evidence. Record the chosen frames and the native edge
columns.

### D4 — Extension strategy

Preferred order:

1. The builder computes per-line scroll from an analytic camera/track function:
   evaluate the same function for the margin columns and write the extended
   table.
2. The builder copies a fixed/ROM curve table: extend the table or the copy
   range for the margins.
3. Neither is safe: record P-U1 as blocked with the writer PC, layout and the
   missing data, and keep the renderer-only margins.

Do not invent curve values from the final hscroll; the per-line curve is the
track geometry.

**Measured result (2026-10-20):** the enabled HDMA BG-scroll channels have
**empty tables** (count byte 0 at `$7E:8262/$8280`), the only CPU writer is the
clear loop (`$82E16D`, zeros), and no DMA to `$2180`/WMDATA was observed. The
visible curves are BG tilemap/scroll; the final scroll is CPU-driven. P-U1 is
therefore **not applicable** and no table extension is needed.

### D5 — Verification

- 4:3 remains aligned with the preserved baseline.
- 16:9 on a curve/hill shows the margin curve continuous with the native edge
  (no seam step, no wrapped column).
- HUD remains clamped and readable.
- No simulation, camera, collision, RNG or input changes.
- If the extension regresses, revert it and keep the blocker.

### D6 — Workspace

```text
work/ws-hdma/
├── project/       # copy of the ws-discriminator project
├── patches/       # hook/extension diff
├── logs/          # trace, watch logs, runs
├── frames/        # 4:3 and 16:9 curve captures
├── metrics/
└── coverage/
```

### D7 — No object work

P7/P8 and P9–P13 remain deferred. The wide frame may still show object pop-in;
that is recorded, not fixed here.

## Risks / Trade-offs

- **Builder not found** → P-U1 remains blocked; document writer/layout gaps.
- **Pointer-relative stores** → watch a range and the pointer, not one address.
- **Table is generated from ROM curve data** → extension may need the same
  function/RTL; do not guess.
- **Wrong curve extension** → visible seam; revert and record the first bad
  frame.
- **Wide gate/timing** → use the aligned gate from Phase E2.
- **Performance** → measure the added per-frame work.
- **Simulation changes** → revert.

## Open Questions

- Is the table rebuilt in WRAM every frame, or copied from a ROM curve table?
- Is the per-line value analytic (camera + slope) or a lookup?
- Where is a clear curve/hill in the deterministic route?
- Can the extension be host-side after the builder, or does it need a
  generated-C hook in `CameraUpdate`/the display block?
- Does the extension need to account for BG1 and BG2 differently (parallax)?
