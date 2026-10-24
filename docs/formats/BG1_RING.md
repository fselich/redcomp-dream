# BG1 ring (race track layer)

How the race track layer (BG1) is laid out in VRAM and how screen positions map to world cells. Proven in the archived change `uniracers-ws-offwindow` (`openspec/changes/archive/2026-10-22-uniracers-ws-offwindow/report.md`). The port's ring observation (`game/src/ws_ring.c`) and margin policy (`game/src/ws_margin.c`) are built on this model.

## Geometry

- In a race BG1 uses **16×16 tiles** (`BGMODE = 0x19`: mode 1, BG3 priority, BG1 tile size 16). A register dump taken outside race rendering shows mode 1 without the size bit; the per-line state the renderer uses is what counts.
- The BG1 map is 32×32 entries of 16-px cells at VRAM word `$0C00`: a **512×512 px ring**, not a 256×256 window.
- Ring slot for a screen pixel:

  ```text
  map_x = ((hscroll + screen_x) >> 4) & 31
  map_y = ((vscroll + screen_y) >> 4) & 31
  ```

  There is no hidden base offset. Validated with 0 conflicts on Dragster and Zoom Zoo.
- World cell for a screen pixel: `world = (unwrapped_scroll + screen) >> 4`, where the 10-bit scroll registers are unwrapped frame to frame (`WsWrapDelta10` in `ws_ring.c`). The world cell is the key the port uses for "is this ring slot still showing what the native view showed".

## How the game fills the ring

```text
      ring (512 px = 32 cells)
 ┌──────────────────────────────────────────────────────────────┐
 │ … history … │ W margin │ native 16+1 cells │ E margin │ …    │
 │  (correct)  │ 3 cells  │  written by game  │ 3 cells  │stale │
 └──────────────────────────────────────────────────────────────┘
                 trailing                         leading
```

- Each frame the game uploads one 17-word row chunk (`$81:B3D0` → `$0433` → DMA to `$2118`): **16 visible cells + 1 fine-scroll overhang**, plus the column refresh the horizontal motion needs.
- Cells written earlier stay in the ring until it wraps 512 px later. The **trailing** margin (the side the camera moves away from) therefore holds valid world data.
- The **leading** margin (the side the camera moves toward) holds cells last written 512 px of world earlier. They are right only where the track happens to repeat.
- At race start the game can leave a few ring cells unwritten (`0x0000`) for a short time (seen on Bowl, Switcher and Monster).

Measured at 16:9 with the raw ring in the margins (same-run native ground truth):

| Capture | West margin | East margin |
|---|---|---|
| Dragster 2300–2500 (moving right, 13–15 px/frame) | 100 % correct | 47 % (70 % / 36 % / 36 % for cells +0 / +1 / +2 past the edge) |
| Zoom Zoo 3300–3505 (mostly vertical) | 99.9 % | 97.7 % |

So the margin problem is sourcing about three leading-edge cells per row per frame; [`TRACK_MARGIN.md`](../TRACK_MARGIN.md) describes how the port does it.

## Checking a claim

`tools/scripts/ws-offwindow/world_key_check.py` scores a BG pixel-source probe capture (`SNESRECOMP_BGPROBE_FRAME`, `SNESRECOMP_BGPROBE_FILE`, see [`CONFIG.md`](../CONFIG.md)): every span is keyed by world cell, and a cell conflicts when two observations carry different map words. Negative controls on the Dragster capture (a 32-px key, or the raw instead of unwrapped register) give hundreds of conflicts, so the test discriminates. `game/tools/validate.sh` uses the same score for its `logs` case.
