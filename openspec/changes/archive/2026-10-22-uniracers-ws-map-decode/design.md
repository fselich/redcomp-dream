## Context

Read first: `docs/UNIRACERS_BG1_RING_FINDINGS.md` (ring formula, probe, world
key), `openspec/changes/uniracers-ws-leading-edge/report.md` §6 (Option C
no-go), and `openspec/changes/uniracers-native-map-spike/report.md` (tables,
stage 1/2 history).

### Known before this spike

- BG1 in race: 16×16 tiles, 32×32-cell ring at VRAM `0x0C00`, BGMODE `0x19`.
  World cell = `(unwrapped_scroll + screen) >> 4`, with 0 conflicts on all
  tracks: the world map is static per cell.
- Row uploader `$81:B3D0..B3FE`, disassembled from the ROM
  (`work/ws-leading-edge/scripts/dis.py`):

  ```text
  B3DE  LDA $7F000F,X     ; table read (bank $7F, base 000F)
  B3E2  STA $0000,Y       ; staging ($0433/$0455)
        INY ; INY
        LDA $023E ; CLC ; ADC $0236 ; STA $023E ; TAX   ; X += stride
        DEC $76 ; BMI B3F9 ; JMP B3DE
  B3F9  STY $0339 ; REP #$20 ; RTS
  B3FF  data: 0000 0002 … 000E 0020 0022 … 002E 0040 …   ; swizzle table
  ```

  The inner loop is a plain strided copy. The non-contiguous `$7F` pointers seen
  by the earlier read watch come from the caller, which reloads `$023E` per
  piece, and/or from other readers (stage 1). The routine entry is before
  `B3D0`; the linear disassembly from `B3D0` starts mid-instruction. Find the
  real entry and caller.
  The table at `B3FF` maps index i → `(i & 7) * 2 + (i >> 3) * 0x20` bytes. It
  is probably a piece-internal offset table. Confirm it.
- Table shape per track (native-map spike): Dragster `$04F1 = 0x0400` (cols),
  `$04F3 = 0x0800` (stride); Zoom Zoo `0x0100` / `0x0200`. World X/Y in
  `$00A5` / `$00A7`.
- Stage 1 `$81:8A4A` reads the low table and emits codes (`$0260`: `4800`,
  `3800`, …). Stage 2 `$81:8B95` converts them (`$0230`); `$00A0` is its
  sentinel. Never fully understood.

### New evidence (2026-10-21, this spike's seed)

1. **VRAM dumps must be read little-endian.** That is what
   `PpuRenderVram` uses, and it matches the probe's `real` words (`0x3400`,
   `0x3800`, `0x2C00`, `0x01CA`, …). The earlier "big-endian tile words" note in
   the spike docs was a misreading.
2. **BG1 characters are static during the race.** The CHR region
   `0x2000–0x5FFF` is byte-identical in Dragster dumps at frames 2500, 3500 and
   5000. Most BG1 cells use character 0/2 with different palette/priority bits;
   a few use real shape characters (slopes, `0x01CA`…). So the track picture is
   fully determined by the map words.
3. **`$7F` high table holds final map words.** Zoom Zoo 3504: 108/108 distinct
   BG1 words are in the high table and 245/458 4-cell row windows are found
   byte-for-byte. Dragster: 6/6 and 11/22 (other dump).
4. **Piece layout.** `work/ws-map-decode/logs/seg-layout-zoomzoo-3504.txt`
   (from `work/ws-map-decode/scripts/seg_layout.py`): +2 bytes per cell to the
   right inside a group of 4; +8 bytes per row down (e.g. column 13, rows 0…6:
   `9697, 969F, 96A7, …`). So the pieces are 4 cells wide × H rows, with a
   4-word row pitch. Many windows have multiple hits (`*`), which shows that
   pieces are reused. Windows not found are those that straddle two pieces.

## Goals / Non-Goals

**Goals:**

- G1: a function `cell(world_x, world_y) → tile word` computed from one WRAM
  snapshot taken after race load, with 0 mismatches against probe ground truth
  on the five tracks.
- G2: the loader, i.e. how `$7F` is filled at race start, and the ROM format it
  reads.
- G3: an offline full-track renderer (PNG per track) from WRAM, and from the ROM
  if G2 succeeds.
- G4: written answers on ultrawide feasibility and track editing, plus a
  go/no-go for a runtime `source` mode.

**Non-Goals:**

- Implementing the runtime `source` mode (later change).
- Building a track editor or new tracks.
- Changing any presentation default, or touching the ghost-lane implementation.

## Decisions

### D1 — Work in an isolated copy

Copy the project into `work/ws-map-decode/project/` (rsync, excluding
`build*/`). Configure `build-decode/` there. All instrumentation (write
watches, test hooks) goes into that copy only. Reason: the ghost-lane agent is
editing `src/game_rtl.c`, `src/main.c` and `snesrecomp/runner/src/beam_frame_driver.c`
in the shared tree. Record the source commit/diff state copied in
`work/ws-map-decode/notes/baseline.md`.

Coordination rules while the ghost lane is in progress:

1. **Performance measurements.** Do not build or run emulator jobs while the
   ghost-lane agent measures cost (its tasks 7.x). If that cannot be
   coordinated, flag in both reports that its numbers must be re-measured
   without the spike running. Limit builds to `--parallel 8`.
2. **Debug port.** Use `SNESRECOMP_DEBUG_PORT=9998`, never 9999.
3. **Copy timing.** The copy may catch half-edited files. Accept it only if it
   builds and native Dragster 2500 gives `3417f91a…df44` with no
   `SNESRECOMP_WS_OFFWINDOW` set. Otherwise copy again.
4. **Shared docs.** Write only to this change and `work/ws-map-decode/` until
   the report. Update the handoff and `UNIRACERS_BG1_RING_FINDINGS.md` at the
   end, re-reading them immediately before editing.

### D2 — Decode WRAM first, ROM second

The WRAM tables are directly observable and validate against the probe.
Decoding the ROM format without a WRAM decode would have no oracle. Order:
piece library → placement → validation → loader → ROM format.

### D3 — Oracle

Ground truth for G1 is world-keyed BG1 words from:

- probe captures (`SNESRECOMP_BGPROBE_*` + `world_key_check.py`-style unwrap);
- `bg1-seq-*.json` and `bg1-stitched-dragster.*` in
  `work/ws-native-map-spike/dumps/`, which hold earlier sequences. Re-check
  their endianness.
- the ghost-lane harvest table, if that change has landed.

Score: exact match per world cell; report coverage of the track.

The world origin must be tied to the table index. Find the mapping
`world_x/world_y ↔ table column/row`. Use `$00A5` / `$00A7` versus the unwrapped
scroll as the anchor.

### D4 — Tools

Use the existing tools rather than new infrastructure:

- `dis.py` (ROM disassembly with REP/SEP tracking);
- the debug server commands listed in
  `docs/UNIRACERS_WIDESCREEN_TRACK_FILL.md` §9 (`set_wram_watch`,
  `wram_watch_log_get`, `dump_frame_wram`, `interp_edges`);
- `SNESRECOMP_READ_WATCH_*` read watches;
- Python with `-I` on dumps.

### D5 — Time box and exit

Time box: three working sessions for G1, two for G2/G3. The spike ends early as
no-go if, after G1's time box, fewer than 95 % of probe cells are reproduced.
The report states which parts are decoded, which are not, and the cheapest next
step.

## Risks / Trade-offs

- [Placement is computed by stage-1 code, not stored] → then G1 is "re-implement
  stage 1 in Python". Still valid; validate the same way.
- [Tables are streamed or partially rebuilt during the race instead of loaded
  once] → compare `$7F` at race start against later frames first (task 2.1).
  If they are streamed, model the window.
- [ROM data is compressed with a custom scheme] → find the decompressor via a
  write watch on `$7F` during load; port it, or call it in a harness.
- [Dynamic BG elements (animated or object-drawn cells)] → the world-key check
  already shows 0 conflicts; re-check per track.
- [Parallel work collision] → D1 isolation; never write to the shared tree.

## Open Questions

- Piece height H, and whether the high table is a flat library or a placed
  2D map with 4-cell column strips.
- What the low table (`$7F000F`, small values `0x001C`, `0x000A`…) encodes:
  piece IDs per world block, collision/surface type, or both.
- Where per-track pointers live in ROM.
- How the 1P track sections relate to the `maps/` line-art (topology check for
  the offline renderer).

## Artifact index

All paths are under `work/ws-map-decode/` unless stated otherwise. They are
gitignored and ROM-derived.

**Notes and results**

| Path | Content |
|---|---|
| `docs/UNIRACERS_TRACK_FORMAT.md` (repo docs, not under `work/`) | **Durable format reference**: directory, RNC1, unpacked layout, screen relation, validation, tools |
| `report.md` (this change) | Spike result and the runtime `source` decision |
| `notes/findings.md` | Running findings: table lifetime, layout, validation, world origin, loader/RNC format, all-tracks scan, renders. **Start here.** |
| `notes/baseline.md`, `notes/project.diff`, `notes/snesrecomp.diff` | State of the isolated copy when it was taken |
| `logs/t7f-<track>.log` | Per-frame `$7F` change log (table lifetime, task 2.1) |
| `logs/seg-layout-<track>-*.txt` | Map-row windows located in `$7F` (tasks 2.2–2.3) |
| `logs/probe-start-<track>.csv` | Probe captures from race start (task 3.6) |
| `logs/track-table.txt` | Track directory `$82:B6F9` (task 4.4) |
| `renders/bowl-{native,rom-window,rom-window-raw}-2300.*` | Pixel validation (task 5.2) |
| `logs/validate-h1.txt` | Decode vs renderer-probe ground truth, all tracks (task 3.x) |
| `logs/trace-loader-dragster.json` | `$7F` writers during load (task 4.1) |
| `logs/decomp-calls-<track>.json[.raw.json]` | RNC unpacker call arguments (task 4.1) |
| `logs/rnc-scan.txt` | All 45 RNC blobs in the ROM (task 4.6) |
| `renders/rom-<track>-div8.png` | Colour BG1 renders of the five 1P tracks from the ROM |
| `renders/zoomzoo-full-div8.png`, `renders/dragster-*.png` | First renders from WRAM dumps |
| `renders/shapes/rnc-<offset>.png` | One-pixel-per-cell shape maps of all 45 tracks |
| `renders/all/rnc-<offset>-div16.png` | Colour 1/16 renders of all 45 (tube mostly lost; use shapes) |
| `unpacked/rnc-<offset>.bin` | Unpacked `$7F` images of all 45 tracks |
| `dumps/<track>/{wram,vram,cgram,ppu}-<frame>.*` | Race dumps (2300/2900/live) |

**Scripts** (`scripts/`; run with `python3 -I`)

| Script | Purpose |
|---|---|
| `seg_layout.py` | Locate BG1 row windows inside `$7F` |
| `track_cell.py` | `TrackMap(wram, cols).cell(wx, wy)`: the decode (H1) |
| `validate_probe.py` | Score the decode against probe CSVs (ring-only spans) |
| `rnc.py` | RNC1 unpacker; `unpack` / `track` / `verify` commands; `TRACKS` = known 1P sources |
| `scan_rnc.py` | Find, unpack and classify every RNC1 blob in the ROM |
| `render_track.py` | Colour BG1 render from `wram` / `rom:<track>` / `blob:<file>` |
| `track_shape.py` | Shape map (1 px per cell) |
| `track_table.py` | Dump the track directory |
| `compare_window.py` | Pixel-compare a ROM render window with a screenshot |
| `trace_loader.py`, `trace_decomp_args.py` | Debug-server traces (port 9998) of the loader |
| `../ws-leading-edge/scripts/dis.py` | 65816 disassembler used for `$81:B3D0`, `$81:B8F1` |

**Isolated build hooks** (`project/UniracersSNESRecomp/src/game_rtl.c`,
`DecodeFrame()`): `SNESRECOMP_DECODE_LOG`, `SNESRECOMP_DECODE_DUMP`,
`SNESRECOMP_DECODE_DIR`, `SNESRECOMP_DECODE_DUMP_LIVE`. Documented in
`notes/findings.md`.
