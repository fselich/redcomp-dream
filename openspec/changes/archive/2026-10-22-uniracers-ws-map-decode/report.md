# uniracers-ws-map-decode — report

Date: 2026-10-21
Status: **go**. The track map is fully decoded from WRAM and from the ROM. A
runtime `source` mode is recommended as a follow-up change.

Durable reference: `docs/UNIRACERS_TRACK_FORMAT.md`.
Working notes: `work/ws-map-decode/notes/findings.md`.
File list: `design.md` → "Artifact index".

## 1. Result

| Goal | Result |
|---|---|
| G1 WRAM decode | `cell(wx, wy) = piece[low[wy>>2][wx>>2]][wy&3][wx&3]`. **100 %** on Dragster →/←, Zoom Zoo and Bowl; 97–98 % on Switcher/Monster, where every miss is a transient unwritten ring cell near the start |
| G2 loader + ROM | RNC (ProPack) method 1 blobs, unpacked by `$81:B8F1` into `$7F:0000`; directory `$82:B6F9` with 45 entries; Python unpacker reproduces `$7F` byte for byte |
| G3 offline render | All 45 tracks as shape maps; the five 1P tracks in colour from the ROM; pixel-exact against the emulator (Bowl window: 3,741/3,741 opaque BG1 pixels) |
| G4 feasibility | Ultrawide: BG1 data is complete at any width (PPU limit 800 px). Editing: picture/compression/directory known; collision source unknown |

## 2. Format (summary)

```text
ROM  $82:B6F9  directory, 5-byte entries: bank | addr16 | length16   (index = track number)
ROM  $18-$1F   45 blobs "RNC\x01" + 18-byte header (unpacked/packed size, CRCs, chunks)
WRAM $7F:0000  unpacked image, static from load to finish
     0000-000A  header (0002-0009: two (x,y)-like pairs, unknown)
     000B-000C  end of piece table (game rewrites it after unpacking)
     000D       cols/4 (0 = 256)     000E  rows/4 (0 = 256)
     000F       piece-id grid, u16, cols x rows, 0x8000 bytes
     800F       piece library, 4x4 BG1 map words per piece (32 bytes, row pitch 8)
BG1  16x16 tiles, CHR static during the race; 1 cell = 16 px, 1 piece = 64 px
```

Grid shapes: 22 × 256×64, 11 × 128×128, 4 × 1024×16, 4 × 64×256, 3 × 512×32,
1 × 16×1024.

## 3. Evidence

- Table lifetime: `work/ws-map-decode/logs/t7f-<track>.log`, written only
  during load.
- Probe validation: `logs/validate-h1.txt`, `logs/probe-start-<track>.csv`.
- Loader traces: `logs/trace-loader-dragster.json`, `logs/decomp-calls-*.json`.
- ROM: `logs/rnc-scan.txt`, `logs/track-table.txt`, `unpacked/`.
- Renders: `renders/rom-<track>-div8.png`, `renders/shapes/`,
  `renders/bowl-{native,rom-window}-2300.png`.

## 4. Decision: runtime `source` mode

**Go**, as a separate change: `openspec/changes/uniracers-ws-source/` (created 2026-10-21; the ghost lane was parked as too slow).

- **Source:** read live WRAM `$7F` (static after load). There is no need to
  unpack in the host.
- **Anchor:** the world origin is `unwrapped scroll + per-race offset`. Find the
  offset once per race by matching the visible native ring against the table, or
  find the game's camera variable. It is not a plain `$7E` word; it is probably
  stored with sub-pixels.
- **Serving:** in the existing margin hook (`WsOffwindowTile`), the order is:
  verified ring cell (Option A), then table cell, then mask.
- **Cost:** two WRAM reads per margin cell. That is negligible next to the ghost
  lane's ~1 extra emulated frame per frame (estimate +3–4 ms).
- **Coverage:** all cells, including those the camera never shows (vertical
  sections) and any ultrawide width.
- **Caveats:**
  - the transient blank start area: serve the table;
  - row r is BG line `vscroll + r + 1`, already handled by the renderer;
  - BG1 only; BG2, cull and HUD are separate topics for ultrawide.

Relation to `uniracers-ws-ghost-lane`: the ghost lane finished with a go
(99.84 % of the Dragster leading edge served, 0 wrong). `source` gives the same
BG1 result without speculative frames:

- coverage of cells the camera never reaches;
- far lower cost (WRAM reads against ~1 extra emulated frame per frame);
- no dependency on rollback-snapshot completeness.

Suggested path:

1. keep `ghost` as the validated reference;
2. build `source`;
3. cross-check `source` against `ghost` per served cell (they must agree);
4. pick the default by cost.

The ghost lane stays useful for non-BG1 content.

## 5. Not done / open

- 2.5 caller of the `$81:B3D0` row copy: not needed for decoding; deferred.
- 4.5 header `0002–0009` meaning (start positions?).
- Names of 30 of the 45 tracks (15 assigned, see the format doc).
- Collision/surface source, which a track editor needs (test: patch a piece id
  in `$7F` at runtime and watch the rider).
- `$4F → $82:B6F9` during load is inferred, not traced.

## 6. Isolation

All runtime work used `work/ws-map-decode/project/` (`build-decode/`, debug port
9998). The shared tree and `build-local/` were not modified. Hooks in the copy:
`SNESRECOMP_DECODE_{LOG,DUMP,DIR,DUMP_LIVE}` in `src/game_rtl.c`.
