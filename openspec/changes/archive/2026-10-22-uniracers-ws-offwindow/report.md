# Off-window track columns (Phase F2) — interim report

Date: 2026-10-21
Change: `uniracers-ws-offwindow`
Scope covered: sections 1–3.2 of `tasks.md`
Outcome so far: **ring formula proven; trailing margin authentic; leading
margin unsourced.** No go/no-go yet. Clamp stays the default.

Full write-up: `docs/UNIRACERS_BG1_RING_FINDINGS.md`.

## 1. Environment

- `build/` was configured at `<workspace>/...` and does not
  rebuild here. A new `build-local/` was configured next to it (Release,
  `SNESRECOMP_ENABLE_TRACE=ON`), with `config.ini`/`keybinds.ini` copied
  (`DisableFrameDelay = 0`).
- Native Dragster frame 2500, clean SRAM: SHA-256 `3417f91a…df44`, identical to
  `work/ws-native-map-spike/frames/gate-native-2500-1.ppm`.

## 2. Pixel-source probe

Test-only, in `snesrecomp/runner/src/snes/ppu.c` (8×8
`PpuDrawBackground_4bpp` and 16×16 `PpuDrawBackgroundBig`):

- `SNESRECOMP_BGPROBE_FRAME=N[,M]`, `SNESRECOMP_BGPROBE_LAYERS` (mask, default
  BG1), `SNESRECOMP_BGPROBE_FILE`.
- CSV: `frame,layer,sy,sx,w,bgx,bgy,hscroll,vscroll,map_word,map_x,map_y,real,served`.
- Native hash unchanged with the probe off and on.
- Patch: `work/ws-offwindow/patches/bgprobe.diff`; original `ppu.c.orig`.

## 3. Ring formula

In race `BGMODE = 0x19`: **BG1 is 16×16**. Ring = 32×32 cells = 512×512 px.

```text
map_x = ((hscroll + x) >> 4) & 31
map_y = ((vscroll + y) >> 4) & 31
```

Frame 3505: `hscroll 121` → cell 7, phase 9; screen spans cells 7..23, which
is the stage chunk. No offset.

| Same-run native capture | Frames | Cells | Conflicts |
|---|---:|---:|---:|
| Dragster 2300–2500 | 201 | 2,895 | 0 |
| Zoom Zoo 3300–3505 | 206 | 830 | 0 |

Negative controls: 32-px key gives 194 conflicts; raw register gives 720/1,200.

## 4. Margin content (16:9, no clamp)

| Capture | Trailing | Leading |
|---|---|---|
| Dragster 2300–2500 (→, ~14 px/frame) | 100 % (W) | 47 % (E); per cell +0/+1/+2 = 70/36/36 % |
| Zoom Zoo 3300–3505 | 99.9 % (W) | 97.7 % (E) |

The margins sample real ring cells, not a wrapped left edge. The leading cells
are stale because the game writes each column just in time.

## 5. Consequence for the remaining tasks

- Section 3 is now "source ~3 leading-edge cells per row", not "decode the
  whole adjacent table".
- Prototype path (section 5): trailing side from the ring, leading side from a
  source or an asymmetric clamp, in a host shadow (`tileShift = 4`).
- Open: Bowl/Switcher/Monster; leftward motion; how the leading column is
  written; whether the `$7F` segment lookup is valid beyond the native 17 cells.

## 6. Artifacts (`work/ws-offwindow/`, local only)

- `logs/bgprobe-{dragster-2300-2500,zoomzoo-3300-3505}.csv` (native)
- `logs/bgprobe-wide-{dragster-2300-2500,zoomzoo-3300-3505}.csv` (16:9)
- `frames/probe-native-2500.ppm`, `frames/probe-zoomzoo-3505.ppm`,
  `frames/probe-wide-*.ppm`
- `scripts/world_key_check.py`
- `ring-formula.md` (working notes, with the § Revision)


---

## Addendum (2026-10-21 pm)

Sections 3.3–3.7 and 5–6 were carried out in
`openspec/changes/uniracers-ws-leading-edge/`. Result: **Option A go, Option C
no-go.**

- 0 wrong served margin cells on Dragster (rightward and leftward), Zoom Zoo,
  Bowl, Switcher and Monster.
- Native 4:3 bit-identical; WRAM/VRAM/CPU/DMA identical to the clamp run.
- Cost ≈0.07 ms/frame; 0 masked/served changes over 84,871 visible cell-frame
  pairs.
- Mechanism: renderer-side substitution in `PpuDrawBackgroundBig` (no guest
  VRAM writes). The host-side VRAM bracket was rejected: it corrupted the HUD
  timer because the HUD bands repoint map usage.
- Option C: the row chunk starts at the last native cell; the `$7F` segment
  pointer list is still undecoded, so the builder cannot be widened safely
  without a new trace.

Clamp remains the default in this change. Recommend `ring` as the follow-up
default; `source` is unavailable. Full numbers and artifacts:
`openspec/changes/uniracers-ws-leading-edge/report.md`.
