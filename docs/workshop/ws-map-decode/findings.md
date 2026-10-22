# Map decode spike — working findings

Change: `openspec/changes/uniracers-ws-map-decode/`. Isolated build:
`work/ws-map-decode/project/UniracersSNESRecomp/build-decode/`.

## Tooling added (isolated copy only)

`src/game_rtl.c` → `DecodeFrame()` at the end of `GameRunOneFrame()`:

| Env | Effect |
|---|---|
| `SNESRECOMP_DECODE_LOG=path` | one line per frame where bank `$7F` low half (`$7F:0000-7FFF`) or high half changes (FNV-1a), plus live-race flag |
| `SNESRECOMP_DECODE_DUMP=f1,f2` + `SNESRECOMP_DECODE_DIR=dir` | `wram-<f>.bin` (128 KiB), `vram-<f>.bin` (64 KiB, LE words as the renderer reads them), `cgram-<f>.bin` (512 B), `ppu-<f>.txt` (bgmode, scroll, `$00A5/$00A7/$04F1/$04F3`) |
| `SNESRECOMP_DECODE_DUMP_LIVE=1` | also dump at the first `InLiveRace()` frame (too early: it is NOW PLAYING, tables still empty) |

Dumps: `dumps/<track>/{wram,vram,ppu}-{2300,2900,live-N}.*`. Runs to frame 3000
with the move routes (Dragster/Zoom Zoo: `ws-native-map-spike/scripts`; Bowl,
Switcher, Monster: `ws-leading-edge/scripts`).

## 2.1 Table lifetime: loaded once, then static

Logs `logs/t7f-<track>.log`. `$7F` is written during track load and never again
up to frame 3000:

| Track | Load writes | Race start (last change, `live=1`) | Changes after |
|---|---|---|---|
| Dragster | 1409–1421 | 1422 | none |
| Zoom Zoo | 1432–1501 | 1502 | none |
| Bowl | 1535–1555 | 1556 | none |
| Switcher | 1638–1738 | 1740 | none |
| Monster | 1741–1819 | 1820 | none |

`InLiveRace()` is already true at NOW PLAYING, before the load.

## 2.2–2.4 Layout (hypothesis H1, validated)

```text
low  table  $7F:000F  u16 piece id, grid cols x rows (cols = $04F1),
                      row stride $04F3 bytes = 2*cols; last row may be partial
high table  $7F:800F  piece library, piece = 4x4 BG1 cells = 16 u16 = 32 bytes,
                      row pitch 8 bytes, cell (x,y) at +y*8 + x*2
cell(wx, wy) = u16 at $7F:800F + id*32 + (wy&3)*8 + (wx&3)*2,
               id = u16 at $7F:000F + 2*((wy>>2)*cols + (wx>>2))
outside the grid -> 0x0000
```

One piece = 64×64 px. Grid sizes: Dragster 1024×15(.99), Zoom Zoo 256×63,
Bowl 256×63, Switcher 1024×15(.99), Monster 128×127. The low table drawn as an
occupancy map is the course shape (`2D` printout in the session; regenerate it
from `track_cell.py`).

All BG1 map words seen in VRAM are present in the high table (100 % on all five
tracks; `seg_layout.py` logs `logs/seg-layout-<track>-2300.txt`).

## 3. Validation against the renderer probe

`scripts/validate_probe.py wram.bin cols probe.csv`: native spans, race ring
only (map address `0x0C00–0x0FFF`; HUD bands repoint BG1). World origin
searched as a free integer offset. Results (`logs/validate-h1.txt`):

| Capture | Cells | Match |
|---|---:|---:|
| Dragster → 2300–2500 | 2,895 | **100 %** |
| Dragster ← 3300–3600 | 3,765 | **100 %** |
| Zoom Zoo 3300–3505 | 830 | **100 %** |
| Bowl 2000–2300 | 2,378 | **100 %** |
| Switcher 2000–2300 | 3,978 | 97.9 % (84 cells) |
| Monster 2000–2300 | 2,564 | 97.2 % (72 cells) |

Every Switcher/Monster mismatch has the same signature:

- only during the first 60–140 frames of the window;
- camera almost still near the start (unwrapped x 152 → 317);
- screen y 1–100;
- a 12×7-cell rectangle where the table holds solid pieces (`0400`, `1800`)
  and the ring holds `0000`;
- values constant over time.

Reading: the game leaves that part of the ring unwritten at race start (blank),
so the native picture differs from the level data there. This is not a decode
error. Open: check the same start area on Dragster/Zoom Zoo/Bowl (their windows
did not include the start), and decide whether a margin source should prefer
the table or mimic the blank.

## 2.6 World origin

Table-pixel camera vs PPU scroll: `cam_y - vscroll` is constant per track
(Bowl 1168, Monster 160, Dragster 544). `cam_x - hscroll` is constant modulo
512 px. Anchoring for runtime use: unwrapped scroll + a per-race offset found
once by matching the visible ring against the table. No `$7E:0000-1FFF` word
equals the camera in table coordinates, nor differs from it by a constant
(the camera is probably stored with sub-pixels or in other units). Open.

## 5. Offline render (first results)

`scripts/render_track.py wram vram cgram cols out.png [div] [cx0 cx1 cy0 cy1]`:
BG1 from the decoded tables plus static CHR (word base `0x2000`, 4bpp, 16×16)
and CGRAM. Uses stdlib zlib, not PIL.

- `renders/zoomzoo-full-div8.png` (1440×456 at 1/8; cells 216..936 × 4..232).
  It matches the external line map `maps/uniracers-crawler-zoom-zoo-snes-map.webp`
  shape for shape.
- `renders/dragster-full-div4.png` (8608×160 at 1/4; 2152 cells long, a single
  horizontal strip).
- `renders/dragster-crop-cam2300.png` (full scale; striped track tube).

All five tracks rendered **from the ROM alone** (`rom:<track>`, unpacked by
`rnc.py`; grid width from the header): `renders/rom-<track>-div8.png`. CHR and
CGRAM still come from `dumps/<track>/{vram,cgram}-2300.bin`. Topology matches
the external maps for Zoom Zoo and Monster. Thin features vanish at 1/8 scale
(nearest sampling); render a crop at `div 1` to inspect them.

## 4. Loader and ROM format

Trace (`scripts/trace_loader.py`, `logs/trace-loader-dragster.json`): bank `$7F`
is written by `$81:B983/B98E/B99B` inside the routine entered at **`$81:B8F1`**.
That routine is a **Rob Northen Compression (RNC, ProPack) method 1** unpacker:

- args on the stack: `[6]` dest offset → `$86`, `[8]` source bank → `$84`,
  `[A]` source address → `$82`, `[4]` high byte = data bank (`$7F`);
- skips 17 header bytes, then reads an LSB-first bit stream with Huffman
  tables (`$BADC`) and LZ back-references (`$86 − $9D`);
- the unpack runs across several frames (Dragster 1409–1420).

One call per track (`scripts/trace_decomp_args.py`, `logs/decomp-calls-*.json`;
query the watch log one frame at a time, because it returns at most 1024
events oldest-first):

| Track | ROM source (LoROM) | File offset | Unpacked | Packed |
|---|---|---|---:|---:|
| Dragster | `$18:8000` | `0xC0000` | 33,815 | 369 |
| Zoom Zoo | `$18:8183` | `0xC0183` | 50,665 | 6,581 |
| Bowl | `$18:9B4A` | `0xC1B4A` | 35,810 | 1,314 |
| Switcher | `$18:A07E` | `0xC207E` | 62,789 | 9,802 |
| Monster | `$18:C6DA` | `0xC46DA` | 52,234 | 7,950 |

`scripts/rnc.py` (port of public-domain `dernc.c`) reproduces `$7F` **byte for
byte** on all five tracks (`rnc.py verify`). The only exception is
`$7F:000B-000C`, which the game rewrites after unpacking (ROM value = end of
the piece table, e.g. Dragster `0x840F`).

Unpacked layout (`$7F:0000`):

```text
0000-000A  header words; 0002-0009 look like two (x,y) pairs (start positions?)
000B-000C  end of piece table (rewritten by the game at load)
000D       cols / 4   (0 = 256 -> 1024 cols)
000E       rows / 4
000F       low table: u16 piece id, cols x rows, always 0x8000 bytes here
800F       piece library: 32 bytes per piece, up to the unpacked size
```

Open: the ROM table that maps a track number to its RNC source (tasks 4.4), and
the remaining header fields (4.5).

## 4.6 All tracks in the ROM

`scripts/scan_rnc.py` finds **45 RNC1 blobs** in ROM banks `$18`–`$1F`
(file `0x0C0000`–`0x0FFFFF`). All 45 unpack cleanly and all are track images
with the same layout (`logs/rnc-scan.txt`, `unpacked/rnc-<file offset>.bin`).
The first five are the 1P tracks traced above. Grid shapes, each 0x8000 bytes
of piece ids (cols × rows of 64×64-px blocks; header `000D/000E`, 0 = 256):

| Grid | Count |
|---|---:|
| 256×64 | 22 |
| 128×128 | 11 |
| 1024×16 | 4 |
| 64×256 | 4 |
| 512×32 | 3 |
| 16×1024 | 1 |

Shape maps, one pixel per 16×16 cell, dark = cell with a non-zero character
(tube, slopes, posts): `renders/shapes/rnc-<offset>.png`
(`scripts/track_shape.py`). Colour renders at 1/16
(`renders/all/rnc-<offset>-div16.png`) lose the one-cell-thick tube to
nearest sampling; use the shape maps for overviews.

Track name ↔ blob is only known for the five traced tracks. The table that maps
the game's track number to its blob is task 4.4.

## 4.4 Track directory

`$82:B6F9` (file `0x0136F9`): 45 entries of 5 bytes,
`bank | address (u16) | blob length incl. 18-byte header (u16)`
(`scripts/track_table.py`, `logs/track-table.txt`). The generic resource loader
`$82:B2DA` calls `$82:B2AD`, which reads entry `id*5` through the long pointer
`[$4F]`. If `$4D == $80` the resource is RNC-unpacked with `JSL $81:B8F1`;
otherwise it is copied raw through `$2180`. Entry index = track number:

| Index | Track | Evidence |
|---|---|---|
| 0–4 | Crawler: Dragster, Zoom Zoo, Bowl, Switcher, Monster | load traces + PICK TRACK order |
| 11 | Shuffler Megajump | shape vs `maps/` (visual) |
| 12, 13, 14 | Shuffler Jumps, Flat Fun, Infinity | size match vs `maps/` |
| 10 | Shuffler Looper (likely) | height 935 vs 929 cells |
| 20–24 | Walker: Dragrace, Pingpong, Hill Climb, Hybrid, Short Cut | size match vs `maps/` |

Tracks look grouped in tours of five (45 = 9 × 5). The remaining names are not
identified. That `$4F` points at `$82:B6F9` during a track load is inferred,
not traced.

## 5.2 Pixel validation

`scripts/compare_window.py`: Bowl frame 2300, a 256×224 window rendered from
the ROM vs the emulator screenshot (`renders/bowl-native-2300.png`,
`renders/bowl-rom-window-2300.png`). **3,741/3,741 opaque BG1 pixels equal**
(within 8/255 for the 5→8-bit colour rounding) with the window one row lower
than the scroll register: screen row r shows BG line `vscroll + r + 1`. The
backdrop, BG2 blocks, HUD and sprites are other layers.

## 3.6 Race start

Probe from the first race frame (`logs/probe-start-<track>.csv`):

- Dragster: 512/512 cells match, no blank area.
- Bowl: one map row of unwritten ring cells (`0000`) while the ring fills.
- Zoom Zoo: inconclusive (camera nearly still; origin ambiguous).
- Switcher and Monster (earlier windows): a 12×7-cell blank rectangle near the
  start.

Decision: the **table is authoritative**. A runtime source serves the table
value and does not imitate the transient blank.

## 5.3 Topology

The shape maps match the external maps for Zoom Zoo, Monster, Bowl and Megajump.
Bowl's shape map also shows a tall block of shape characters above the central
U; the line-art does not draw it (decoration?).

## 5.4 Ultrawide

> Implemented and superseded by `openspec/changes/uniracers-ws-ultrawide/`. The
> practical cap is 512 px (9-bit OAM X), not the PPU's 800 px.


The whole map is in WRAM `$7F` from load to finish, so BG1 width is no longer a
data problem at any aspect. Extra width per side at 224 lines with the default
4:3 CRT pixel aspect (7:6):

| Target | Frame width | Extra per side | 16-px cells per side |
|---|---:|---:|---:|
| 16:9 | 342 | 43 | 3 |
| 21:9 | ~448 | ~96 | 6–7 |
| 32:9 | ~684 | ~214 | 14 |

- Renderer budget: `kPpuExtraLeftRight = 272` px per side, so 800 px wide
  (~36:9) is the PPU ceiling.
- Outside the grid every cell is `0x0000` (empty).
- Not covered by this spike, but needed for a real ultrawide build:
  - BG2 background (`bgXsc[1] = 0x73`, a 64×64-entry map; probably a repeating
    pattern);
  - object cull (`$0421/$0423`, currently +43 px);
  - HUD bands;
  - CPU rider/ghost visibility.

## 5.5 Track editing

Known:

- the full BG1 picture (piece grid + piece library);
- the compression (RNC1; a packer is needed to write tracks back; ProPack
  packers exist);
- the directory format;
- the grid dimensions in the header.

Unknown:

- whether collision and surface come from the same BG1 shapes or from other
  data. Test: patch one piece id in `$7F` at runtime and watch rider physics;
- header `0002–0009` (two equal (x,y)-like pairs on Dragster, e.g. `0x4400,
  0x3200`; start positions?);
- per-track palette/CHR, objects (boost/stunt markers appear as one-cell posts
  inside pieces), CPU ghost data, lap/finish logic.

## Still to do

- 2.5 caller of `$81:B3D0` (how `$023E` walks pieces; `B3FF` swizzle table).
- 2.5 caller of the `$81:B3D0` row copy (no longer needed for decoding).
- 4.5 header fields `0002–0009`.
- Names of the remaining 30 tracks; collision source (see 5.5).
