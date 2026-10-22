## 1. Setup (isolated)

- [x] 1.1 Read `design.md` (Context + "New evidence"), `docs/UNIRACERS_BG1_RING_FINDINGS.md`, and `uniracers-native-map-spike/report.md`.
- [x] 1.2 Copy the project to `work/ws-map-decode/project/` (exclude `build*/`); record the copied state (git status/diff of project and `snesrecomp`) in `work/ws-map-decode/notes/baseline.md`. *(2026-10-21 18:36; diffs in `notes/project.diff`, `notes/snesrecomp.diff`; includes the ghost-lane work in progress, inert without its switch.)*
- [x] 1.3 Configure `build-decode/` in the copy (Release, `SNESRECOMP_ENABLE_TRACE=ON`); copy `config.ini`/`keybinds.ini`; confirm native Dragster 2500 SHA-256 `3417f91a…df44`. *(Matches; build with `--parallel 8`.)*
- [x] 1.4 Never edit `work/ws-native-map-spike/project/UniracersSNESRecomp` or its `build-local/` (ghost-lane work in progress).

## 2. Table lifetime and layout

- [x] 2.1 Dump WRAM at race start (first live-race frame) and at several later frames per track; record whether `$7F` changes after load (static vs streamed). *(Static: written during load (e.g. Dragster 1409–1421), never after, up to frame 3000 on all five tracks. `notes/findings.md` §2.1.)*
- [x] 2.2 Re-run `scripts/seg_layout.py` on Dragster, Bowl, Switcher, Monster dumps (LE VRAM); record per-track hit rates. *(100 % of map words present in the high table on all five tracks.)*
- [x] 2.3 Determine piece width (expected 4 cells), height H and row pitch (expected 8 bytes) in the high table; list piece start addresses for one track. *(Pieces 4×4 cells, 32 bytes, pitch 8; address = `$7F:800F + id*32`.)*
- [x] 2.4 Decode the low table at `$7F000F` (value set, spatial pattern); test whether it indexes pieces or encodes surface/collision. *(Piece-id grid `cols=$04F1` × rows, stride `$04F3`; it is the course shape. Collision not yet checked.)*
- [x] 2.5 *(Deferred: not needed for decoding; see report §5.)* Find the real entry and caller of the `$81:B3D0` loop (entry is before `B3D0`); document how `$023E` is set per piece and what the `B3FF` swizzle table is used for.
- [x] 2.6 Relate `$04F1`/`$04F3`, `$00A5`/`$00A7` and the unwrapped BG1 scroll to table column/row (world-origin mapping). *(cam_y − vscroll constant per track; cam_x − hscroll constant mod 512 → anchor = unwrapped scroll + per-race offset found by matching. No direct WRAM camera word found; open.)*

## 3. WRAM decode and validation

- [x] 3.1 Write `scripts/track_cell.py`: `cell(wx, wy)` from a WRAM snapshot.
- [x] 3.2 Build the oracle: world-keyed BG1 words from probe captures (reuse the `world_key_check.py` unwrap); verify old `bg1-seq-*.json` endianness before using them. *(`scripts/validate_probe.py`; ring-only spans, HUD bands excluded. Old json not needed.)*
- [x] 3.3 Score `cell()` on Dragster and Zoom Zoo: mismatches must be 0 on covered cells; report coverage. *(Dragster →/← 100 %, Zoom Zoo 100 %.)*
- [x] 3.4 Extend to Bowl, Switcher, Monster. *(Bowl 100 %; Switcher 97.9 %, Monster 97.2 %: all mismatches are a blank start-area rectangle the game leaves unwritten at race start, not decode errors.)*
- [x] 3.5 Time-box check: if coverage < 95 % after three sessions, go to section 6 with a no-go. *(Passed in session 1.)*
- [x] 3.6 Check the race-start blank area on Dragster, Zoom Zoo and Bowl (start frames), and decide table vs blank for a margin source. *(Dragster clean; Bowl/Switcher/Monster show transient unwritten ring cells; Zoom Zoo inconclusive. Decision: table is authoritative.)*

## 4. Loader and ROM source

- [x] 4.1 Write-watch bank `$7F` during track load (NOW PLAYING → first race frame); record writer PCs and source addresses. *(Writer `$81:B983/B98E/B99B` = RNC1 unpacker entry `$81:B8F1`; one call per track, dest `$7F:0000`; sources Dragster `$18:8000`, Zoom Zoo `$18:8183`, Bowl `$18:9B4A`, Switcher `$18:A07E`, Monster `$18:C6DA`. `scripts/trace_loader.py`, `scripts/trace_decomp_args.py`, `logs/trace-loader-dragster.json`, `logs/decomp-calls-*.json`.)*
- [x] 4.2 Disassemble the writer(s) with `dis.py`; identify compression and per-track pointer tables. *(Rob Northen Compression "RNC" method 1, 18-byte header. The ROM table that maps track → source address is not located yet; addresses were captured at runtime.)*
- [x] 4.3 Reproduce `$7F` from the ROM offline (Python), byte-compare with the race-start dump. *(`scripts/rnc.py`: identical on all five tracks except `$7F:000B-000C`, a header word the game rewrites after unpacking.)*

- [x] 4.4 Locate the ROM table that maps a track number to its RNC source address (search bank `$18` addresses as 16/24-bit pointers in ROM; or trace the caller's argument push before `$81:B8F1`). *(Directory `$82:B6F9`, 45 × 5-byte entries, read by `$82:B2AD` via `[$4F]`; index = track number; 15 names assigned. `scripts/track_table.py`, `logs/track-table.txt`.)*
- [x] 4.6 Scan the ROM for every RNC1 blob and classify it. *(45 blobs in banks `$18`–`$1F`, all track images; `scripts/scan_rnc.py`, `logs/rnc-scan.txt`, shape maps `renders/shapes/`.)*
- [x] 4.5 *(Open; see report §5.)* Decode the remaining `$7F:0000-000E` header fields (`000D` = cols/4 with 0 = 256, `000E` = rows/4; `000B-000C` = end of the piece table, rewritten by the game; `0002-0009` probably start positions).

## 5. Offline renderer and feasibility

- [x] 5.1 `scripts/render_track.py`: full-track BG1 PNG from decoded data (map words + static CHR + CGRAM from a dump). *(Script works: `renders/dragster-full-div4.png`, `renders/zoomzoo-full-div8.png`, crop `renders/dragster-crop-cam2300.png`. All five from the ROM alone: `renders/rom-<track>-div8.png`; CHR/CGRAM still from `dumps/<track>/{vram,cgram}-2300.bin`.)*
- [x] 5.2 Validate: native-frame crop equals the emulator BG1 at the same world position (pixel diff). *(Bowl 2300: 3,741/3,741 opaque BG1 pixels; screen row r = BG line vscroll+r+1. `scripts/compare_window.py`.)*
- [x] 5.3 Topology check against `maps/` line-art for the five tracks. *(Zoom Zoo: full-track render matches `maps/uniracers-crawler-zoom-zoo-snes-map.webp` shape for shape (loops, ramps, spur lines, the vertical line top-right). Monster: matches `maps/uniracers-crawler-monster-snes-map.webp`. Bowl: matches (extra decoration block not in line-art). Dragster/Switcher: size-consistent strips, not checked visually.)*
- [x] 5.4 Ultrawide note: for 16:9, 21:9 and 32:9, cells needed per side and whether the decoded map covers them on every track (track edges, vertical sections).
- [x] 5.5 Track-editing note: which data a new track needs besides BG1 words (collision/surface, objects, palettes, CPU path); what is decoded vs unknown.

## 6. Report and decision

- [x] 6.1 Write `report.md`: layout, addresses, validation numbers, loader/ROM status, renders, feasibility notes.
- [x] 6.2 Go/no-go for a runtime `source` mode; estimate cost compared with the ghost lane.
- [x] 6.3 Update `docs/UNIRACERS_BG1_RING_FINDINGS.md` and the handoff; correct the "big-endian VRAM dump" statements in the older docs.
