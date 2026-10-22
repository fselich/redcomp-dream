# Uniracers native-map / widescreen handoff

Date: 2026-10-21 (updated 2026-10-21 pm: ring formula solved)
Purpose: resume the BG1 off-window investigation without repeating failed
experiments.

## 1. Repository state

- Repo: `<workshop>` (older docs say
  `<workspace>/...`; the original `build/` even records
  `<workspace>/...`)
- ROM: `<repo>/Uniracers (USA).sfc`
  - 2,097,152 bytes, CRC32 `383858c7`
  - SHA-256 `859ec99fdc25dd9b239d9085bf656e4f49c93a32faa5bb248da83efd68ebd478`
- Framework pin: `snesrecomp=9c6afcbfd4106e4613e522741d667047a48ded3b`
- Repo is not git-initialized. `.gitignore` ignores `*.sfc`, `*.smc`, `work/`.
- External line-art maps are in `maps/` (15 files, all 1P track sections).

Active OpenSpec changes:

| Change | Progress | State |
|---|---:|---|
| `uniracers-native-map-spike` | 25/40 | no-go for source-backed wide map; reports/docs written |
| `uniracers-ws-offwindow` | 45/46 | ring formula **proven**; sections 3.3–3.7 and 5–6 reported in the leading-edge change; only the widened-builder test (3.5) remains blocked |
| `uniracers-ws-leading-edge` | 25/32 | Option A (verified ring cells) **go**; Option C **no-go** (segment list undecoded); report written |
| `uniracers-ws-map-decode` | 28/30 | spike **go**: track map fully decoded (WRAM + ROM, 45 RNC1 tracks); runtime `source` mode recommended; see `docs/UNIRACERS_TRACK_FORMAT.md` |
| `uniracers-ws-ghost-lane` | 33/34 | ghost lane **go**: leading served 99.84 % Dragster →, 0 wrong all tracks, real lane identical; only the real-time audio listen (5.4) remains. **Parked (2026-10-21):** too slow in practice (+2.3 ms mean, p99 9.6 ms, ~42 fps headless); superseded by `source` |
| `uniracers-ws-ultrawide` | 9/9 | **done**: race width follows the screen (`SNESRECOMP_WS_ASPECT`, default auto ≥ 16:9) up to 512 px (OAM X limit); cull/HUD derived from the margin; BG1 0 wrong at 21:9 and 512 px; 16:9 byte-identical |
| `uniracers-ws-source-default` | 9/9 | **done**: `source` is the default 16:9 BG1 policy (before, normal play showed the raw wrap; clamp needed an env var). Switch values: `source`/`ring`/`ghost`/`clamp`/`wrap` |
| `uniracers-ws-source` | 22/23 | **go**: `SNESRECOMP_WS_OFFWINDOW=source` serves both 16:9 margins from the `$7F` track table; 0 wrong on all tracks, cost within noise; recommended 16:9 default. Next: make it the default; ultrawide |
| `uniracers-ws-ghost-xhigh` | 25/25 | rider OAM X high bit for the widened right cull; on by default, `SNESRECOMP_WS_OAM_XHIGH=0` kills it. Follow-ups in the same change: zoomed tracks (`$03ED`: OAM X unshifted, cull widened by `E << $03ED`), parked culled rider hidden on ultrawide, and the live-race gate fixed for later tours (`$0B4 != 0`, was `== 1`, tour 1 only) |
| `uniracers-ws-radar` | 22/22 | local 32×32 radar from the `$7F` table with player/rival dots; `[Video] MiniMap=0` disables; report written |

All OpenSpec changes/specs validate.

## 2. What is confirmed

### Uploader

- BG1 map base: `0x0C00` words.
- 32×32 map. ~~big-endian tile words in VRAM dumps~~ → **little-endian** (corrected by `uniracers-ws-map-decode`).
- **In race BG1 uses 16×16 tiles (`BGMODE = 0x19`)**: the ring is 512×512 px.
  The 17-word chunk is 16 visible cells + 1 overhang.
- `$81:B3E2` stages 17-word/34-byte chunks in `$0433`/`$0455`.
- DMA: `src 00:0433`, `dest <map address>`, size 34, channel 0, `$2118`.
- At frame 3505, stage chunk = BG1 row 19, x=7..23; DMA dest `0x0E67` =
  row 19, x=7.
- DMA destination walks one map row per frame (3498 `0x0F47` → 3506 `0x0E47`).
- Stage/transfer are offset in time: frame 3504 stage is at row 19, while the
  pending DMA dest is row 20. A simple `stage -> pending dest` model is false.

### Stage 1 / 2

- Stage 1 is `$81:8A4A`; stage 2 is `$81:8B95`.
- Stage 2 conversion works: preloaded `$0260` values `1C00`, `2024`, `7FFF`
  produce nonblank `$0230`; `4800` is an empty segment.
- Stage 1/2 is a vertical tile column keyed mostly by Y.
- Varying `$00A5` (±512), `$041D`/`$041F` (±256) did not change the stage-2
  output at the tested row.
- Stage 1 output (`$0260`: `4800...0802`) does not match any BG1 row/column,
  normal or byte-swapped. It emits intermediate segment codes.

### Ring formula (validated)

Full record: `docs/UNIRACERS_BG1_RING_FINDINGS.md`.

```text
map_x = ((hscroll + screen_x) >> 4) & 31
map_y = ((vscroll + screen_y) >> 4) & 31
```

- Frame 3505: `hscroll 121` → cell 7, phase 9; screen samples cells 7..23 =
  the stage chunk. No offset.
- World key `(unwrapped_scroll + screen) >> 4`: 0 conflicts on Dragster
  2300–2500 (2,895 cells) and Zoom Zoo 3300–3505 (830 cells).
- 16:9 margins without clamp hold real ring cells. Trailing side: 100 %
  correct (Dragster). Leading side: 47 % (cells +0/+1/+2: 70/36/36 %). Zoom Zoo:
  99.9 % / 97.7 %.

The earlier 8×8 formula (`start = hscroll >> 3 = 15`, "right margin = wrapped
left edge", 28,080 conflicts) is **wrong**. Do not reuse it.

## 3. Five-track geometric classification

Using local `maps/` line-art + native/wide captures:

| Track | Section | Margins should be | Clamp correct? |
|---|---|---|---|
| Dragster | horizontal | track continues | **No** |
| Zoom Zoo | vertical loop | background | Yes |
| Bowl | horizontal | track continues | **No** |
| Switcher | horizontal | track continues | **No** |
| Monster | horizontal | track continues | **No** |

Consequences:

- Clamp is **not** a universal 16:9 fix; it hides real horizontal track on four
  of five 1P tracks.
- Clamp is geometrically correct on the Zoom Zoo vertical loop.
- Edge-repeat test (`SNESRECOMP_WS_TRACK_REPEAT=1`) looks right on Dragster but
  **breaks Zoom Zoo**; it is rejected as a general solution.

## 4. Determinism rules

- Keep normal pacing: `DisableFrameDelay = 0` in `build/config.ini`.
  `DisableFrameDelay=1` caused non-deterministic runs.
- Remove `build/saves/save.srm` and `save.srm.bak` before every deterministic
  run. Persistent SRAM changes the starting state.
- With clean SRAM + pacing, two native Dragster runs at frame 2500 were
  byte-identical:
  `3417f91afc8f6d47ea4ab3f7b11bbeec4cc7491a23ab71cc6c956f5fb3f5df44`.

## 5. Test build and switches

Build (use `build-local/`; the original `build/` has another machine's paths):

```bash
cd "<repo>/work/ws-native-map-spike/project/UniracersSNESRecomp"
cmake -S . -B build-local -G Ninja -DCMAKE_BUILD_TYPE=Release -DSNESRECOMP_ENABLE_TRACE=ON
cmake --build build-local --parallel
cp build/config.ini build/keybinds.ini build-local/
```

Headless clamp:

```bash
cd build-local
rm -f saves/save.srm saves/save.srm.bak
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
SNESRECOMP_NO_LAUNCHER=1 SNESRECOMP_RUN_FRAMES=2500 \
SNESRECOMP_WS=1 SNESRECOMP_WS_TRACK_CLAMP=1 \
SNESRECOMP_WS_TRACK_REPEAT= \
SNESRECOMP_SCREENSHOT=/tmp/clamp.ppm SNESRECOMP_SCREENSHOT_FRAME=2500 \
./UniracersSNESRecomp --rom "<repo>/Uniracers (USA).sfc" \
  --no-launcher \
  --script <repo>/work/ws-native-map-spike/scripts/route-dragster-move.txt
```

Test-only switches (in this build only):

- `SNESRECOMP_WS_OAM_XHIGH=0` — disable the rider OAM X high-bit fix for the
  widened right cull (presentation-only; default on).
  `SNESRECOMP_WS_OAM_XHIGH_LOG=<path>` writes `frame,slot,lo,want` for fixed
  slots (`lo=-1`: a parked rider hidden on ultrawide). The same switch also
  disables that hiding.
- `[Video] MiniMap = 0` in `config.ini` — disable the local radar (default on).
  `config.local.ini` overrides; `SNESRECOMP_WS_MINIMAP=0` is a test override.
  `SNESRECOMP_WS_RADAR_LOG=<path>` writes
  `frame,ptx,pty,rtx,rty,ax,ay,pword,rword`.
- `SNESRECOMP_WS_TRACK_CLAMP=1` — accepted fallback.
- `SNESRECOMP_WS_TRACK_REPEAT=1` — edge-repeat experiment; **not production**.
- `SNESRECOMP_WS_TEST_GEN=1` — stage 1/2 logging.
- `SNESRECOMP_WS_TEST_STAGE2=1` — forced `$0260` stage-2 test.
- `SNESRECOMP_WS_TEST_STAGE2_SCAN*` — 1D/2D scans.
- `SNESRECOMP_BGPROBE_FRAME=N[,M]` + `SNESRECOMP_BGPROBE_LAYERS` +
  `SNESRECOMP_BGPROBE_FILE` — renderer pixel-source probe (per drawn span: map
  word, cell, scroll). Patched into `snesrecomp/.../ppu.c`; see
  `work/ws-offwindow/patches/bgprobe.diff`.
- `SNESRECOMP_READ_WATCH_FILE=...` + `SNESRECOMP_READ_WATCH_FRAME_MIN/MAX` +
  `SNESRECOMP_READ_WATCH_ALL=1` — bank `$7F`/all-bank read watch.

Debug scripts live in `work/ws-native-map-spike/scripts/`:
`dbg.py`, `capture_frame.py`, `capture_bg1_sequence.py`, `break_stage.py`,
`same_run_capture.py`, `multi_frame_capture.py`, `trace_upload.py`,
`stage2scan*` harnesses are in `game_rtl.c`.

## 6. What failed / do not repeat

- “No data in margins” — false; level data exists.
- Stage 2 `$00A0` as proof of no track — false; it is a sentinel.
- `$00A5` / `$041D` / `$041F` as horizontal key — no effect in tests.
- Contiguous high-table row read — BG1 row is multi-segment; no exact match.
- Cross-run pre/post comparison — snapshots are not stable.
- Edge-repeat as general fix — breaks vertical loops.
- `step_block` at `$81:B3F9` — stays parked; use `break_continue`.

## 7. Current blocker

The old blocker (no `screen pixel -> BG1 map cell` observation) is **resolved**
by the renderer probe. It showed BG1 is 16×16, which is why the formula failed.

The remaining gap is narrower: **~3 leading-edge cells per row** (the side the
camera moves toward) are stale in the ring. The game writes each column just
in time (~14 px/frame). The trailing side is already authentic.

## 8. Next steps (in order)

`uniracers-ws-leading-edge` has completed the prototype. See its `report.md`.

1. ~~Probe Bowl, Switcher, Monster and a leftward-motion section.~~ Done: 0
   wrong served cells on all five tracks and leftward Dragster.
2. ~~Serve the trailing margin from the ring; mask unverified leading cells.~~
   Done with `SNESRECOMP_WS_OFFWINDOW=ring` (renderer-side substitution, no
   guest VRAM writes).
3. ~~Trace how the leading column is written.~~ Done: the 16/17-word row chunk
   starts at the last native cell; it is not a clean leading-cell source.
4. Decode the `$81:B3D0` / `$7F` segment pointer/row list. Then test a widened
   builder (3 cells per side) into a scratch buffer with full save/restore and
   score the extra cells. This is the remaining Option C work.
5. Option A is go: native 4:3 bit-identical, guest state identical, ≈0.07
   ms/frame, 0 flicker in the measured windows. Recommend `ring` over `clamp`
   in a follow-up user-facing change; clamp stays the default for now.
6. Option C is no-go for this change; keep the leading edge masked.
7. ~~Ghost lane.~~ Done in `uniracers-ws-ghost-lane`: `ghost` mode keeps a
   speculative copy ~4 frames ahead, harvests world-keyed BG1 cells and serves
   them in the leading margin. Leading served share is 99.84 % on Dragster →,
   with 0 wrong cells on all five tracks and leftward, and the real lane is
   bit-identical to `ring` (state trace and frame-2500 dumps). Cost
   ≈+2.3 ms/frame mean (end-to-end +1.9–2.9 ms), but the headless wall rate is
   ~42 fps vs 60 for `ring`; the test build is correctness-first, not yet a
   smooth default. **Next:** listen to a real-time run for audio (the only
   open task) and make the lane cheap enough for 60 fps. The cost is the
   snapshot/restore traffic, not the ghost raster: with AHEAD=0 the build is
   still ~42 fps (`guest_ms` 10.97 vs 4.04, `hook_ms` 0.069). A lighter
   snapshot strategy is the next step, not the design's journal-walk
   optimisation. `ring` remains the smooth-play fallback. Two implementation traps are worth keeping in
   mind: the raster journal and the whole `Ppu` host state are outside the
   rollback blob, and BG1 CHR is not static until a short warm-up.

## 9. Artifact map

Durable docs:

- `docs/UNIRACERS_NATIVE_MAP_SPIKE.md`
- `docs/UNIRACERS_WIDESCREEN_TRACK_FILL.md`
- `openspec/changes/uniracers-native-map-spike/report.md`
- `docs/UNIRACERS_BG1_RING_FINDINGS.md` (ring formula, margin measurements)
- `openspec/changes/uniracers-ws-offwindow/` (active follow-up, interim `report.md`)

Work artifacts:

- `work/ws-native-map-spike/baseline.md`
- `work/ws-native-map-spike/upload-trace.md`
- `work/ws-native-map-spike/logs/`
- `work/ws-native-map-spike/frames/`
- `work/ws-radar/` (local radar logs, panel crops, state dumps, patch)
- `work/ws-offwindow/ring-formula.md` (initial + § Revision)
- `work/ws-offwindow/logs/bgprobe-*.csv`, `scripts/world_key_check.py`,
  `patches/bgprobe.diff`
- `work/ws-offwindow/{project → symlink, logs, frames, metrics, patches}`

Key frames:

- `frames/gate-native-2500-1.ppm`, `frames/gate-clamp-2500.ppm`
- `frames/dragster-repeat-3500.png` (repeat looks right)
- `frames/zoomzoo-repeat-3500.png` (repeat breaks)
- `frames/zoomzoo-native-3505.png`

## 10. Hard truth

The local line-art maps are sufficient for topology, not tile-level rendering.
The clamp is a fallback, not a correct 16:9 presentation for Dragster, Bowl,
Switcher or Monster. The ring formula is now proven and the trailing margin is
authentic; true off-window track still requires a real source for the leading
cells. Until then, do not claim it works.
