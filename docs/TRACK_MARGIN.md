# Track margin (widescreen BG1)

In a widescreen race the frame is wider than the 256 px the game draws (342 px at 16:9, up to 512 px). The track layer, BG1, must show something in the extra columns on each side. This document describes how the port fills them, the fallback, and the approaches that were tried and rejected.

The policy is the option `[Widescreen] Margin` (see [`CONFIG.md`](CONFIG.md)): `source` (default) or `clamp`. The code is `game/src/ws_margin.c` (policy and hooks), `game/src/ws_ring.c` (ring observation) and `game/src/ws_source.c` (track table and anchor).

## The problem

BG1 is a 32×32 ring of 16×16 cells, 512 px wide (see [`formats/BG1_RING.md`](formats/BG1_RING.md)). The game keeps only the 17 visible cells of each row current. A wide renderer that samples the ring beyond the native window gets:

- on the **trailing** side (where the camera comes from), cells the game wrote earlier: correct;
- on the **leading** side (where the camera goes), cells written a full ring (512 px of world) earlier: wrong wherever the track does not repeat.

The whole track, however, sits unpacked in WRAM bank `$7F` from load to finish (see [`formats/TRACK_FORMAT.md`](formats/TRACK_FORMAT.md)). `source` uses both facts.

## `source` (default)

Every BG1 sample outside screen X 0..255 goes through a renderer hook (`g_ppu_margin_tile_hook`, called inside the PPU's BG drawing, so guest VRAM is never written). For each sample, in order:

1. **Map check.** If the sample's VRAM address is not the race ring slot for its world cell, the tile is left alone. The HUD bands repoint BG1's map base, and this keeps them untouched.
2. **Ring-verified cell.** A per-line hook (`g_snes_beam_line_hook`) records, for every ring slot the native view shows, its world cell and tile word (the "seen" table). If the slot still holds that world cell and word, the ring tile is drawn as is. This covers the trailing side.
3. **Track table.** Otherwise, when the source anchor is valid, the word comes from the `$7F` table at `world + anchor`. This covers the leading side and any cell the camera has never shown.
4. **Mask.** Otherwise the sample is drawn with a fully transparent 16×16 character found in the current BG1 CHR, so the backdrop shows through.

### The anchor

The table and the screen differ by a per-race offset (`table cell = world cell + anchor`), constant per race (X wraps modulo the grid width). `ws_source.c` finds it after each drawn frame:

- **Evidence:** every verified cell still in the seen table, not only this frame's (the map is static, so older observations stay true, and their wider footprint resolves repetitive geometry such as Switcher). Cells reading `0x0000` (left unwritten at race start) and words that occur nowhere in the piece library (the race-start overlay) are ignored. At least 8 samples are needed.
- **Search:** seeds from the rarest sampled word (up to four distinct seeds), candidate offsets from a per-piece index of where that word occurs (at most 4,096 candidates per seed), and an offset qualifies when it matches at least 95 % of the samples.
- **Ambiguity:** up to 8 qualifying offsets are kept; a margin cell is served from the table only where all of them give the same word. More than 8 means "wait for the camera".
- **Self-check:** every frame, each kept offset is checked against up to 32 of this frame's verified cells and dropped below 95 % agreement. A scene change (new BG1 map base or BG mode, for example leaving NOW PLAYING) drops the anchor and the seen table.

While no anchor is valid, steps 1, 2 and 4 still apply: the trailing side is correct and the leading side is masked, never wrong.

### Measured result

From the archived change `uniracers-ws-source` (`openspec/changes/archive/2026-10-22-uniracers-ws-source/report.md`), at 16:9:

| Track | Leading wrong / masked | Trailing wrong / masked |
|---|---|---|
| Dragster →, Dragster ←, Zoom Zoo, Bowl, Monster | 0 / 0 | 0 / 0 |
| Switcher | 0 / 704 (until its three ambiguous anchors resolved) | 0 / 2,448 |

Cells served without ever being seen on screen match the offline ROM decode (173,567 cells, 0 differences). Both margins are fully served after a race-start wait of about 100 frames. Because the table covers the whole track, BG1 stays exact at every width up to 512 px. The cost over `clamp` is about 0.1 ms per frame, within run-to-run noise (`openspec/changes/archive/2026-10-23-uniracers-code-cleanup-2/report.md`).

`game/tools/validate.sh` keeps a wrong-cell score for this policy in its `logs` case (see [`VALIDATION.md`](VALIDATION.md)).

## `clamp` (fallback)

`PpuSetWidescreenLayerClamp(ppu, BG1)`: BG1 is drawn only over the native 256 px, centred, and the margins show the backdrop. No hook is installed. It is always correct and shows less; it is the safe choice if `source` ever misbehaves on a track nobody has checked.

## Rejected approaches

Approaches tried before `source`, with the reason each was dropped and where the evidence is. Paths are under `openspec/changes/archive/`.

| Approach | What it was | Why rejected | Evidence |
|---|---|---|---|
| Raw ring (`wrap`) | No intervention: the wide renderer samples the ring beyond the native window. | The leading side shows cells written 512 px of world earlier (47 % correct on Dragster's leading edge). Removed; `wrap` now means `clamp`. | `2026-10-22-uniracers-ws-offwindow/report.md` |
| Shadow history / edge fill | The framework's world-keyed `ws_shadow` layer: retained history plus edge extension (`SNESRECOMP_WS_TRACK=fill`). | Promising on some slopes, disconnected pieces on loops; edge extension copies a track-edge column into an unrelated segment, and history cannot provide leading cells the game has not written. | `2026-10-20-uniracers-ws-track-margins/report.md` |
| Plain tilemap copy | Copy wider rows out of `$7F` the way the game's staging loop (`$81:B3D0`, source `$023E`, stride `$0236`) appears to, to prefill the margins. | The row is assembled from several non-contiguous `$7F` segments, not one strided range, so a straight copy gives wrong geometry. | `2026-10-22-uniracers-native-map-spike/report.md` |
| Synthetic generator call | Call the game's own stage-1/2 lookup (`$81:8A4A`, `$81:8B95`) from C with save/restore, at margin offsets. | It returns cleanly but gave blank output at the margin offsets (calibrated under the wrong 8×8 assumption) and costs a guest call per column; useful only as a probe. Removed in the first cleanup. | `2026-10-20-uniracers-ws-track-fill/report.md`, `2026-10-23-uniracers-code-cleanup/report.md` |
| VRAM history stitching | Rebuild the world map from successive BG1 VRAM snapshots keyed by a WRAM world coordinate. | The screen-space ring is rebuilt as the camera moves; keying by `$00A5` gave about 93 % conflicts on Zoom Zoo. No-go. | `2026-10-22-uniracers-native-map-spike/report.md` |
| Track edge repeat | Copy the BG1 edge columns into the margin ring slots during the draw (`SNESRECOMP_WS_TRACK_REPEAT`). | Right on horizontal Dragster, but erased real track on Zoom Zoo's vertical curves. Removed in the first cleanup. | `2026-10-22-uniracers-native-map-spike/report.md` |
| Host VRAM bracket | Write verified/masked cells into VRAM before the draw and restore them after. | 0 wrong cells, but the HUD bands reuse the map and a ring write leaked into the timer glyphs. Replaced by the renderer hook. | `2026-10-22-uniracers-ws-leading-edge/report.md` |
| Widened row builder | Make the game's row builder produce three extra leading cells into a host buffer. | The builder assembles each row from several `$7F` segments through an undecoded list; widening it crosses segments. No-go; superseded by decoding the table. | `2026-10-22-uniracers-ws-leading-edge/report.md` |
| Verified ring only (`ring`) | Steps 1, 2 and 4 of `source`: verified cells, mask the rest. | Correct but leaves most of the leading margin empty (16 % served on Dragster →). Kept as the first stage of `source`; removed as a mode, `ring` now means `source`. | `2026-10-22-uniracers-ws-leading-edge/report.md` |
| Ghost lane (`ghost`) | A snapshot of the machine runs a few frames ahead; its BG1 cells are harvested and served on an exact world-cell match. | Correct (0 wrong, 99.8 % leading coverage) but about +2.3 ms per frame on average (+1.9–2.9 ms end to end) with a 9.6 ms p99, a 324 KB snapshot, and framework hooks to save the raster journal and driver state. `source` gives the same picture for almost nothing. Removed; `ghost` now means `source`. | `2026-10-22-uniracers-ws-ghost-lane/report.md`, `2026-10-23-uniracers-code-cleanup-2/report.md` |

Do not describe `clamp` as "the game has no track data in the margins": the data is in `$7F`, and `source` reads it.
