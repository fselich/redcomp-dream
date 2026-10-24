# BG1 ring formula — initial derivation

Date: 2026-10-21
Change: `uniracers-ws-offwindow`

## Anchor

Frame 3505, same-run breakpoint `$81:B3F9`:

```text
BG1 map base     0x0C00 words
hScroll[0]       121
vScroll[0]       314
stage chunk      BG1 row 19, cell x=7..23
DMA dest         0x0E67 = row 19, x=7
```

## Native sampling

For native screen x:

```text
ring_cell_x(x) = ((hscroll >> 3) + (x >> 3)) & 31
```

At hscroll 121:

```text
start = 15
```

The native 32 columns sample ring cells 15..31, 0..14.

This is a ring, not a world-window: the cell at x=20 also appears at
screen x=39 after the modulo, and the right margin samples the **left edge**
of the ring. That is the wrap, not the adjacent world column.

## Margin columns

Wide screen is 342 px: 43 px left + 256 native + 43 px right.

For the right margin:

```text
screen x = 256..299
ring cell = (15 + 32..37) & 31 = 15..20
```

Those cells currently hold world columns 15..20 (the left edge), so the right
margin is a wrapped copy, not the continuation. To show the true right margin
we need world columns:

```text
15 + 32 .. 15 + 37 = 47..52
```

For the left margin, screen x < 0 samples ring cells 9..14; we need world
columns 9..14 (which are not in the native ring window).

## Consequence

The clamp hides the wrap. Edge-repeat copies the edge into the wrap slots and
therefore looks right on a straight horizontal track but is wrong on curves.
The real fill must source world columns 9..14 and 47..52 from the level
table/generator, not from the current ring.

## Validation attempt (negative)

I tested the first formula offline against the Dragster BG1 sequence:

```text
world_col = (hscroll >> 3) + screen_x
cell      = map[y][(start + screen_x) & 31]
```

Result:

```text
world cells = 5056
overlaps    = 149568
conflicts   = 28080
```

The world-keyed columns are not stable. The formula is missing an offset/base
(or the scroll is not absolute across wraps). A candidate offset of one tile
comes from the stage anchor (map cell x=7 vs `start=15`, hscroll=121), but it is
not proven. Do not use this formula for margins yet.

---

## Revision 2026-10-21 (pm): BG1 is 16×16 — formula validated

The initial derivation above assumed 8×8 tiles. It is **wrong**. A renderer
pixel-source probe (`SNESRECOMP_BGPROBE_*`, patch in
`patches/bgprobe.diff`) shows that in race `BGMODE = 0x19`: mode 1, BG3
priority, **BG1 big tiles (16×16)**. The PPU dump that reported `bgmode 1`
was taken outside the race.

So the BG1 ring is 32×32 cells × 16 px = **512×512 px**, and:

```text
map_x = ((hscroll + screen_x) >> 4) & 31
map_y = ((vscroll + screen_y) >> 4) & 31
```

Anchor, frame 3505 (Zoom Zoo route), probe line sy=150:
`hscroll=121 → 121>>4 = 7`, phase `121&15 = 9` → first span is 7 px, then
16-px spans. Screen 0..255 samples cells **7..23** — exactly the 17-word stage
chunk. No hidden offset; the 17-word chunk is 16 visible cells + 1 overhang.

### World-key validation (same-run, native)

`scripts/world_key_check.py`: world cell = `(unwrapped_scroll + screen) >> 4`,
scroll unwrapped frame-to-frame mod 1024.

| Capture | Frames | World cells | Conflicts |
|---|---:|---:|---:|
| Dragster 2300–2500 | 201 | 2895 | **0** |
| Zoom Zoo 3300–3505 | 206 | 830 | **0** |

Negative controls on Dragster: 32-px key → 194 conflicts; raw register (no
unwrap) → 720/1200. The test discriminates. The old 28,080 conflicts came from
the 8-px assumption plus mixing tile and pixel units.

Native 4:3 with the probe compiled in (off) and on: frame 2500 SHA-256
`3417f91a…df44`, identical to the baseline.

### What the wide margins actually hold (no clamp, `SNESRECOMP_WS=1`)

Each margin span scored against the native ground truth of the same world cell:

| Capture | West margin | East margin |
|---|---|---|
| Dragster 2300–2500 (moving right, ~14 px/frame) | **100 %** correct | **47 %** correct |
| Zoom Zoo 3300–3505 | 99.9 % | 97.7 % |

Dragster east by column past the edge: +0 → 70 %, +1 → 36 %, +2 → 36 %.

Conclusion:

- The right margin is **not** a wrapped copy of the left edge. The ring is
  512 px wide; the margins sample real ring cells.
- The **trailing** edge already holds correct world data (ring history).
- Only the **leading** edge (≈3 cells, 43 px, in the direction of motion) is
  stale: the game writes each column just in time, at almost one cell/frame.
- So the off-window problem reduces to: source ≈3 leading-edge cells per row
  each frame. The trailing side can be served directly (or via `ws_shadow`
  history with `tileShift = 4`).

### Reproduce

Build dir `build-local/` (the old `build/` points at `<workspace>/...`):

```sh
cd work/ws-native-map-spike/project/UniracersSNESRecomp
cmake -S . -B build-local -G Ninja -DCMAKE_BUILD_TYPE=Release -DSNESRECOMP_ENABLE_TRACE=ON
cmake --build build-local --parallel
cd build-local && cp ../build/config.ini ../build/keybinds.ini .
rm -f saves/save.srm saves/save.srm.bak
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_NO_LAUNCHER=1 \
SNESRECOMP_RUN_FRAMES=2500 SNESRECOMP_WS=1 \
SNESRECOMP_BGPROBE_FRAME=2300,2500 SNESRECOMP_BGPROBE_FILE=probe.csv \
./UniracersSNESRecomp --rom ".../Uniracers (USA).sfc" --no-launcher \
  --script ../../../scripts/route-dragster-move.txt
python3 ../../../../ws-offwindow/scripts/world_key_check.py probe.csv
```

Probe switches: `SNESRECOMP_BGPROBE_FRAME=N[,M]`, `SNESRECOMP_BGPROBE_LAYERS`
(bitmask, default 1 = BG1), `SNESRECOMP_BGPROBE_FILE`. CSV columns:
`frame,layer,sy,sx,w,bgx,bgy,hscroll,vscroll,map_word,map_x,map_y,real,served`.
The probe lives in the framework renderer (`snesrecomp/runner/src/snes/ppu.c`,
both the 8×8 4bpp path and `PpuDrawBackgroundBig`); original saved as
`patches/ppu.c.orig`.
