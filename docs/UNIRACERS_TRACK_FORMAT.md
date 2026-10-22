# Uniracers track format (BG1 track map)

Date: 2026-10-21
Source: OpenSpec change `uniracers-ws-map-decode` (report in
`openspec/changes/uniracers-ws-map-decode/report.md`; working notes and tools in
`work/ws-map-decode/`, gitignored and ROM-derived).

ROM: Uniracers (USA), CRC32 `383858c7`. Addresses are LoROM.

## 1. Overview

Every track is one compressed blob in ROM. At race load the game unpacks it to
WRAM bank `$7F`, where it stays unchanged until the race ends. The BG1 track
layer that the game streams into its 32×32 VRAM ring is read from that image.

```text
ROM directory  ->  RNC1 blob (banks $18-$1F)  ->  WRAM $7F:0000  ->  BG1 ring (VRAM $0C00)
   $82:B6F9          unpacked by $81:B8F1          static image       row chunks via $0433
```

## 2. Directory

`$82:B6F9` (file `0x0136F9`): 45 entries of 5 bytes.

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 1 | ROM bank (with `$80` bit), `$98`–`$9F` |
| 1 | 2 | address (LE) |
| 3 | 2 | blob length including the 18-byte RNC header (LE) |

Read by `$82:B2AD`: `Y = id*5`, then `LDA [$4F],Y` (`$4F` is a long pointer to
the directory). `$82:B2DA` either unpacks the resource (`$4D == $80`, `JSL $81:B8F1`)
or copies it raw through `$2180`. Index = track number.

Known indices (load traces and comparison with external maps):

| Index | Track |
|---|---|
| 0 | Crawler – Dragster |
| 1 | Crawler – Zoom Zoo |
| 2 | Crawler – Bowl |
| 3 | Crawler – Switcher |
| 4 | Crawler – Monster |
| 10 | Shuffler – Looper (likely) |
| 11 | Shuffler – Megajump |
| 12 | Shuffler – Jumps |
| 13 | Shuffler – Flat Fun |
| 14 | Shuffler – Infinity |
| 20 | Walker – Dragrace |
| 21 | Walker – Pingpong |
| 22 | Walker – Hill Climb |
| 23 | Walker – Hybrid |
| 24 | Walker – Short Cut |

The tracks appear grouped in tours of five (45 = 9 × 5).

## 3. Compression

Rob Northen Compression (ProPack), **method 1**. Header, 18 bytes, big-endian:

```text
"RNC" 01 | unpacked size u32 | packed size u32 | unpacked CRC u16 | packed CRC u16 | leeway u8 | chunks u8
```

The game's unpacker is `$81:B8F1`. Stack arguments: dest offset, source bank,
source address, data bank. It runs over several frames during the load. The
Python port `work/ws-map-decode/scripts/rnc.py` (after public-domain `dernc.c`)
reproduces `$7F` byte for byte on all traced tracks.

## 4. Unpacked image (`$7F:0000`)

| Offset | Meaning |
|---|---|
| `0000–000A` | header; `0002–0009` hold two (x,y)-like pairs (start positions? unknown) |
| `000B–000C` | end of the piece table (the game rewrites this word after unpacking) |
| `000D` | grid columns / 4 (0 = 256) |
| `000E` | grid rows / 4 (0 = 256) |
| `000F` | piece-id grid: u16 per block, `cols × rows`, always 0x8000 bytes |
| `800F` | piece library: 32 bytes per piece = 4×4 BG1 map words, row pitch 8 bytes |

A block (one piece) is 4×4 BG1 cells = 64×64 px. One BG1 cell is a 16×16 tile.

```text
id          = u16 at $7F:000F + 2*((wy>>2)*cols + (wx>>2))
cell(wx,wy) = u16 at $7F:800F + id*32 + (wy&3)*8 + (wx&3)*2      ; BG1 map word
outside the grid in Y -> 0x0000
```

The grid is **circular in X**: lookups wrap `wx` modulo the grid width.
Found on a tour-2 track (grid 64×256 blocks = 256×1024 cells): the race
crosses the right edge and the anchor re-appeared at `ax ± 256`
(`work/ws-source/xwrap/`). The tour-1 tracks never cross the X edge, so their
BG1 probe scores are unchanged (0 wrong on all six captures).

Grid shapes across the 45 tracks: 256×64 (22), 128×128 (11), 1024×16 (4),
64×256 (4), 512×32 (3), 16×1024 (1).

BG1 map words are ordinary SNES tilemap entries: character, palette, priority,
flips. In race the CHR area (word `0x2000–0x5FFF`) does not change. Most cells
are character 0/2 with palette bits (solid stripes); the tube edges, slopes and
posts use other characters.

## 5. Relation to the screen

- The BG1 ring is 32×32 cells (512×512 px) at VRAM `0x0C00`. The game uploads
  the visible 16+1 cells of a row per frame (`$81:B3D0` → `$0433` → DMA).
- Table world cell ↔ screen: `world = unwrapped scroll + per-race offset`. The
  vertical offset is constant per track. The horizontal one is constant modulo
  512 px. No plain `$7E` word holds the camera in table coordinates.
- Screen row r shows BG line `vscroll + r + 1`.
- At race start the game can leave a few ring cells unwritten for a short time
  (seen on Bowl, Switcher and Monster). The table is the authoritative value.

## 6. Validation

| Check | Result |
|---|---|
| Decode vs renderer probe | 100 % Dragster →/←, Zoom Zoo, Bowl; 97–98 % Switcher/Monster (transient start blanks only) |
| ROM unpack vs WRAM dump | byte-identical except `000B–000C` |
| Pixels (Bowl 2300 window) | 3,741/3,741 opaque BG1 pixels equal |
| Topology vs external maps | Zoom Zoo, Monster, Bowl, Megajump match |

## 7. Tools (`work/ws-map-decode/scripts/`)

| Script | Use |
|---|---|
| `rnc.py` | unpack a blob / a named track; verify against a WRAM dump |
| `scan_rnc.py` | list, unpack and classify every RNC1 blob in the ROM |
| `track_table.py` | dump the directory |
| `track_cell.py` | `TrackMap(wram, cols).cell(wx, wy)` |
| `render_track.py` | colour BG1 render (`wram` / `rom:<track>` / `blob:<file>`) |
| `track_shape.py` | 1-px-per-cell shape map |
| `validate_probe.py`, `compare_window.py` | validation against the probe / a screenshot |

## 8. Runtime use

Implemented in `openspec/changes/uniracers-ws-source/` (`SNESRECOMP_WS_OFFWINDOW=source`,
`src/game_rtl.c` block "Track-table source"). The 16:9 margin hook reads BG1
cells from live `$7F` at `world + anchor`:

- the anchor is searched from verified native cells (ignoring `0x0000` and
  words absent from the piece library);
- up to 8 ambiguous anchors are kept, and a cell is served only where they
  agree;
- every frame, the anchor is re-checked against the native view.

**Default since `uniracers-ws-source-default`:** with widescreen on and
`SNESRECOMP_WS_OFFWINDOW` unset, BG1 margins use `source`. Other values are
`ring`, `ghost`, `clamp` (global BG1 clamp) and `wrap` (raw ring).

Results: 0 wrong cells on the five 1P tracks; both margins fully served after
a ~100-frame race-start wait; no measurable CPU cost. See its `report.md`.

## 8b. Ultrawide

`openspec/changes/uniracers-ws-ultrawide/`: the race frame follows the target
aspect (`SNESRECOMP_WS_ASPECT`, default auto = max(16:9, window)) up to
**512 px**. 512 px is the 9-bit OAM X limit: margin E ≤ 128. The object cull
and the HUD segments are derived from E. BG1 stays exact at every width
because it is served from this table.

## 9. Unknown

- Header `0002–0009`.
- Collision/surface data source, which a track editor needs.
- Names of the remaining 30 tracks.
- BG2, objects, CPU ghost data, finish/lap logic.
