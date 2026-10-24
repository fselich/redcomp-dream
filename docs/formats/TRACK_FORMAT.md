# Track format (BG1 track map)

How a Uniracers track is stored in ROM and in WRAM, as decoded in the archived change `uniracers-ws-map-decode` (evidence in `openspec/changes/archive/2026-10-22-uniracers-ws-map-decode/report.md`). The port reads this image at run time for the widescreen margins and the radar.

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
Python port `tools/scripts/ws-map-decode/rnc.py` (after public-domain `dernc.c`)
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
(`openspec/changes/archive/2026-10-22-uniracers-ws-ghost-xhigh/report.md`).
The tour-1 tracks never cross the X edge.

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

## 7. Tools (`tools/scripts/ws-map-decode/`)

| Script | Use |
|---|---|
| `rnc.py` | unpack a blob / a named track; verify against a WRAM dump |
| `scan_rnc.py` | list, unpack and classify every RNC1 blob in the ROM |
| `track_table.py` | dump the directory |
| `track_cell.py` | `TrackMap(wram, cols).cell(wx, wy)` |
| `render_track.py` | colour BG1 render (`wram` / `rom:<track>` / `blob:<file>`) |
| `track_shape.py` | 1-px-per-cell shape map |
| `validate_probe.py`, `compare_window.py` | validation against a BG probe capture / a screenshot |
| `seg_layout.py`, `trace_loader.py`, `trace_decomp_args.py` | the load-time traces used to find the directory and the unpacker |

## 8. Runtime use

`game/src/ws_source.c` reads the live `$7F` image with the formulas above:

- the BG1 margins (`source` policy) are served from the table at `world + anchor`; the anchor is searched from cells the native view has verified and re-checked every frame (see [`TRACK_MARGIN.md`](../TRACK_MARGIN.md));
- the radar (`game/src/ws_radar.c`) draws a 128×64-cell window of the table around the player through the same anchor.

Both are presentation-only: nothing writes bank `$7F`.

## 9. Unknown

- Header `0002–0009`.
- Collision/surface data source, which a track editor needs.
- Names of the remaining 30 tracks.
- BG2, objects, rival rider data, finish/lap logic.
