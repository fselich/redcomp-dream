# Uniracers BG1 ring — findings (16×16 tiles, validated formula)

Date: 2026-10-21
Change: `uniracers-ws-offwindow` (Phase F2, section 2 "Ring formula")
Related: `docs/UNIRACERS_NATIVE_MAP_HANDOFF.md`,
`docs/UNIRACERS_NATIVE_MAP_SPIKE.md`, `docs/UNIRACERS_WIDESCREEN_TRACK_FILL.md`,
`work/ws-offwindow/ring-formula.md`

Status: **ring formula proven; trailing margin already correct; leading margin
(~3 cells) is the only unsourced gap.** Option A (verified ring cells) is now
prototyped and validated (see §8); the leading edge is still background. Clamp
stays the default in the current build; `ring` is the recommended follow-up
default. Option C (source-backed leading cells) is a no-go for this change.

---

## 1. TL;DR

- In race, BG1 uses **16×16 tiles** (`BGMODE = 0x19`). Every earlier ring
  analysis assumed 8×8. That is why the first formula produced 28,080
  conflicts.
- BG1 is a 32×32-entry map of 16-px cells: a **512×512 px ring**, not a
  256×256 window.
- Ring formula, validated with **0 conflicts** on Dragster and Zoom Zoo:

  ```text
  map_x = ((hscroll + screen_x) >> 4) & 31
  map_y = ((vscroll + screen_y) >> 4) & 31
  ```

- The 17-word row chunk the game uploads (`$81:B3D0` → `$0433` → DMA) is
  exactly **16 visible cells + 1 fine-scroll overhang**. There is no hidden
  base offset.
- The 16:9 margins do **not** show a "wrapped copy of the left edge". They
  sample real ring cells that are outside the native window:
  - the **trailing** edge (the side the camera moves away from) is already
    correct world data: 100 % on Dragster;
  - the **leading** edge (the side the camera moves toward) is stale: 47 % on
    Dragster, because the game writes each column just in time
    (~14 px/frame ≈ 1 cell/frame).
- The off-window problem therefore reduces to **sourcing ~3 leading-edge
  cells per row per frame**. That is far smaller than the "decode the whole
  level table" problem it was framed as.

---

## 2. How it was found

### 2.1 Unblocking the build

`work/ws-native-map-spike/project/UniracersSNESRecomp/build/` was configured
on another machine (`<workspace>/...` in `CMakeCache.txt`)
and cannot be rebuilt here. A fresh build directory was created alongside it
without touching the original:

```sh
cd work/ws-native-map-spike/project/UniracersSNESRecomp
cmake -S . -B build-local -G Ninja -DCMAKE_BUILD_TYPE=Release -DSNESRECOMP_ENABLE_TRACE=ON
cmake --build build-local --parallel
cp build/config.ini build/keybinds.ini build-local/   # keeps DisableFrameDelay = 0
```

Determinism check (clean SRAM, normal pacing, `route-dragster-move.txt`):
native frame 2500 SHA-256
`3417f91afc8f6d47ea4ab3f7b11bbeec4cc7491a23ab71cc6c956f5fb3f5df44`, which is
byte-identical to the recorded baseline `frames/gate-native-2500-1.ppm`.

### 2.2 Renderer pixel-source probe

This was the probe the handoff named as the blocker (§7–8). It is a test-only
log inside the framework renderer
(`snesrecomp/runner/src/snes/ppu.c`). For every tile span the renderer draws,
it records the exact VRAM map word the pixels were fetched from.

| Variable | Meaning |
|---|---|
| `SNESRECOMP_BGPROBE_FRAME=N[,M]` | Enable for frames N..M (single frame if no `,M`). |
| `SNESRECOMP_BGPROBE_LAYERS=mask` | Layer bitmask, default `1` (BG1). |
| `SNESRECOMP_BGPROBE_FILE=path` | CSV output, default stderr. |

CSV columns:

```text
frame,layer,sy,sx,w,bgx,bgy,hscroll,vscroll,map_word,map_x,map_y,real,served
```

One row is one span: screen pixels `sx .. sx+w-1` on line `sy` came from VRAM
word `map_word` (`real` = the VRAM entry, `served` = the entry after any
`ws_shadow` substitution). `sx` is negative or ≥256 in the wide margins.

It hooks both 4bpp paths: the 8×8 `PpuDrawBackground_4bpp` and the 16×16
`PpuDrawBackgroundBig`. With the probe compiled in but off, and also when it is
on, native frame 2500 keeps the baseline hash.

- Patch: `work/ws-offwindow/patches/bgprobe.diff`
- Pristine file: `work/ws-offwindow/patches/ppu.c.orig`

### 2.3 The 16×16 discovery

The first probe run logged BG2 but **never** BG1 through the 8×8 path. A
temporary branch log at the mode-1 dispatch showed `bgmode = 0x19` on every
race frame:

```text
0x19 = 0001 1001b
        │   └┴┴─ mode 1
        │  └──── BG3 priority
        └─────── BG1 tile size = 16×16
```

The PPU-state dump the earlier work relied on
(`clean-zoomzoo-3504-ppu-state-live.json`, `bgmode 1`) reflects the register
outside race rendering, not the per-line state the renderer uses.

### 2.4 Anchor check (frame 3505, Zoom Zoo route)

Probe at screen line 150, `hscroll = 121`, `vscroll = 314`:

```text
sx   0  w  7  map_x  7     (121 & 15 = 9 → first cell shows its last 7 px)
sx   7  w 16  map_x  8
...
sx 247  w  9  map_x 23
```

Screen 0..255 samples cells **7..23**: exactly the 17-word stage chunk
recorded at `$81:B3F9` (BG1 row 19, x = 7..23). `121 >> 4 = 7`. The anchor
fits without any offset.

### 2.5 World-key validation

`work/ws-offwindow/scripts/world_key_check.py`. World cell =
`(unwrapped_scroll + screen) >> 4`, with the scroll registers unwrapped
frame-to-frame mod 1024. A cell conflicts if two observations carry different
map words.

| Capture (same run, native) | Frames | World cells | Conflicts |
|---|---:|---:|---:|
| Dragster 2300–2500 | 201 | 2,895 | **0** |
| Zoom Zoo 3300–3505 | 206 | 830 | **0** |

Negative controls on the same Dragster data: a 32-px key gives 194 conflicts;
the raw (not unwrapped) register gives 720 of 1,200 cells. The test
discriminates.

Why the original test failed (28,080 / 149,568): it used 8-px cells, mixed
units (`world_col = (hscroll >> 3) + screen_x`), and ignored the fine phase
and register wrap.

### 2.6 What the 16:9 margins actually contain

Wide run (`SNESRECOMP_WS=1`, no clamp, no repeat). Each margin span was scored
against the native ground truth for the same world cell from the same run:

| Capture | West margin | East margin |
|---|---|---|
| Dragster 2300–2500 (moving right, 13–15 px/frame) | **100 %** correct (139,104 / 139,104) | **47 %** (65,783 / 140,000) |
| Zoom Zoo 3300–3505 | 99.9 % | 97.7 % |

Dragster east margin by cell past the native edge:

| Cell | Correct |
|---|---:|
| +0 | 70 % |
| +1 | 36 % |
| +2 | 36 % |

"Unverifiable" spans (world cells never seen natively in the window) are
excluded from the percentages. They are 1,344 per side on Dragster, and more on
Zoom Zoo because of its vertical motion.

---

## 3. Model of the BG1 ring

```text
      ring (512 px = 32 cells)
 ┌──────────────────────────────────────────────────────────────┐
 │ … history … │ W margin │ native 16+1 cells │ E margin │ …    │
 │  (correct)  │ 3 cells  │  written by game  │ 3 cells  │stale │
 └──────────────────────────────────────────────────────────────┘
                 trailing                         leading
                 (correct)                 (written just in time)
```

- The game keeps the visible 17 cells per row current. It uploads one
  17-word row chunk per frame through `$0433` → `$2118`, plus whatever column
  refresh the horizontal motion needs.
- Cells it wrote earlier stay in the ring until the ring wraps 512 px later.
  The trailing margin therefore reads valid history for free.
- The leading margin reads cells last written 512 px of world earlier. They are
  wrong whenever the track differs, and right by coincidence where it repeats
  (empty sky, long straights). That explains the 36–70 %.
- Under mostly vertical motion (Zoom Zoo loops), the horizontal neighbours
  stay current, so both margins are almost all correct.

---

## 4. Corrections to earlier documents

| Earlier claim | Status |
|---|---|
| "BG1 is a 32×32 (256×256) tilemap" / "256-wide map wrap" | **Wrong in race.** 32×32 entries of 16×16 = 512×512 px. |
| "`ring_cell_x = (hscroll>>3 + x>>3) & 31`, start = 15 at frame 3505" | **Wrong.** `((hscroll + x) >> 4) & 31`, start = 7. |
| "Right margin samples the wrapped left edge (cells 15..20)" | **Wrong.** It samples cells 24..26 of the ring: stale leading-edge cells. |
| "Missing offset/base (candidate one tile)" | **No offset.** The anchor fits exactly. |
| "Stage chunk x = 7..23 vs start = 15 is unexplained" | **Explained.** 17 cells = 16 visible + 1 overhang. |
| "`ws_shadow` cannot help; no true leading-edge columns" (track-fill §5.2) | **Half right.** It can serve the trailing edge. The leading edge does need a new source. The earlier shadow run should be re-checked with `tileShift = 4` keys. |
| Track-fill §5.4 generator scan "8 units per pixel", "blank beyond ±1024" | **Suspect.** Calibrated under the 256-px assumption; re-check before reusing. |
| "Clamp hides the wrap" | Still true as behaviour, but the content it hides is mostly **real** track on the trailing side. |

The archived OpenSpec changes are historical records and were not rewritten.
The durable docs carry correction banners pointing here.

---

## 5. Implications and options

The goal changes from "find a world-column source for both margins" to:

1. **Trailing margin:** serve the ring as-is. It is already authentic. This can
   be done host-side with no guest changes.
2. **Leading margin (~3 cells × ~15 visible rows per frame):** needs a source.
   Candidates, cheapest first:

| Option | Idea | Risk |
|---|---|---|
| A. Asymmetric clamp (quick win) | Draw the trailing margin from the ring; clamp only the leading side, picking the side from the sign of the frame's scroll delta. The renderer already has per-frame asymmetric fill (`extraLeftCur` / `extraRightCur`, `ppu.c` ~l.342). | Leading side still background; needs hysteresis when the direction flips; vertical motion should allow both sides. |
| B. `ws_shadow` world history | Register BG1 with `tileShift = 4`, world key = unwrapped scroll; history covers the trailing side and any area seen before. | Leading edge on first pass is still unknown. |
| C. Widen the row builder | Make `$81:B3D0` produce 20 cells instead of 17 on the motion side (`$76` loop count), into a host shadow, not guest VRAM. | Each cell's source jumps between `$7F` segments. Need to prove the segment lookup gives valid cells beyond the native 17. |
| D. Synthetic guest call | Run the row/cell resolver for `start+17..start+19` (and `start-3..start-1`) with full save/restore. | Cost and state-isolation complexity. |

Follow-up change: `openspec/changes/uniracers-ws-leading-edge/`. It refines A into
per-cell verification (world key + tile word) instead of a direction
heuristic, and stages C as investigate → prototype.

Recommended order: **A** as an immediate improvement behind a test switch, then
investigate **C**, which is the only route to a fully source-backed leading
edge.

---

## 6. Reproduce

```sh
P="$(git rev-parse --show-toplevel)"
cd "$P/game/build-local"
rm -f saves/save.srm saves/save.srm.bak

# native + probe
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_NO_LAUNCHER=1 \
SNESRECOMP_RUN_FRAMES=2500 \
SNESRECOMP_BGPROBE_FRAME=2300,2500 \
SNESRECOMP_BGPROBE_FILE="$P/work/ws-offwindow/logs/bgprobe-dragster-2300-2500.csv" \
./UniracersSNESRecomp --rom "$P/rom/Uniracers (USA).sfc" --no-launcher \
  --script "$P/tools/scripts/ws-native-map-spike/route-dragster-move.txt"

# wide (no clamp) + probe: add SNESRECOMP_WS=1 and a different output file

python3 -I "$P/tools/scripts/ws-offwindow/world_key_check.py" <probe.csv> [--shift 4]
```

Zoom Zoo: `route-zoomzoo-move.txt`, frames `3300,3505`.

Artifacts (`work/ws-offwindow/`, gitignored, ROM-derived; do not publish):

- `logs/bgprobe-dragster-2300-2500.csv`, `logs/bgprobe-zoomzoo-3300-3505.csv`
  (native)
- `logs/bgprobe-wide-dragster-2300-2500.csv`,
  `logs/bgprobe-wide-zoomzoo-3300-3505.csv` (16:9, no clamp)
- `frames/probe-native-2500.ppm` (baseline hash), `frames/probe-zoomzoo-3505.ppm`,
  `frames/probe-wide-*.ppm`
- `logs/run-probe-*.log`, `logs/build-bgprobe.log`

---

## 7. Open points

- Bowl, Switcher and Monster have not been probed yet.
- Leftward and mixed motion: the trailing/leading split should mirror, but this
  is not measured.
- How the horizontal leading column is written. A single 17-cell row per frame
  seems unlikely to keep up with ~1 cell/frame of horizontal motion, so there is
  probably a column path as well. Trace it before choosing option C.
- The probe lives in the framework submodule (`snesrecomp`). It is a local
  patch on pin `9c6afcb`. Keep it as a patch, or upstream it as a generic debug
  feature.


---

## 8. Leading-edge prototype result (2026-10-21 pm)

Change: `uniracers-ws-leading-edge` (Option A go, Option C no-go). Full method,
numbers and artifacts: `openspec/changes/uniracers-ws-leading-edge/report.md`.

### 8.1 Mechanism

Each field the host records, for every one of the 32×32 ring cells the native
view shows, its unwrapped world cell and tile word. A margin BG1 cell is drawn
from the ring only when that key matches and the VRAM word is unchanged;
otherwise the renderer substitutes a fully transparent 16×16 character. The
substitution is in `PpuDrawBackgroundBig`, is limited to `screen_x < 0` or
`>= 256`, and never writes guest VRAM.

A host-side VRAM bracket was tried first. It produced 0 wrong cells but
corrupted the HUD timer: the game's HUD bands repoint map usage, so a ring-cell
write leaked into the timer glyphs (proved with a BG3-only capture). **Do not
use the VRAM bracket for this game.** The renderer hook is the safe mechanism.

### 8.2 Validation

0 wrong served margin cells on Dragster (rightward and leftward), Zoom Zoo,
Bowl, Switcher and Monster; 0 native probe conflicts. Native 4:3 is bit-identical
(`3417f91a…df44` at Dragster 2500). WRAM, VRAM, CPU and DMA state at frame 2500
are identical to the clamp run. Cost ≈0.07 ms/frame; 0 masked/served changes
over 84,871 consecutive visible cell-frame pairs.

The trailing side is authentic; the leading side is masked (background), so
this is never worse than the clamp and restores real track behind the camera.

### 8.3 Option C

At Dragster 2756 the only stage-buffer DMA is `00:0433 → 0x0DBE`, 16 words,
starting at the last native cell and crossing into the next map row. The 17-word
chunk at Zoom Zoo 3504 is the same shape. It is not a clean pre-write of the
three leading world cells. `$76` is the count, `$023E` the source pointer,
`$0236` the stride and `$0339` the staging pointer, but the builder is a
multi-segment assembler whose segment list is computed/queued and still
undecoded. Widening it by 3 cells would cross segments. **Option C is a no-go
for now;** the next step is to decode the segment pointer/row list, then test a
widened scratch-buffer call with full save/restore and score the extra cells.

### 8.4 Recommendation

Promote `ring` over `clamp` in a follow-up user-facing change. `source` is not
available. The clamp remains the default in the leading-edge change itself.

## 9. Leading edge without decoding: ghost lane (implemented)

Change: `openspec/changes/uniracers-ws-ghost-lane/`. Status: **go** for
`SNESRECOMP_WS_OFFWINDOW=ghost`; report with all numbers and artifacts in that
change.

Idea: the level map is static per world cell (0 conflicts everywhere), so any
BG1 cell the game writes in a *speculative future* is correct for that world
cell. A rollback snapshot of the machine runs a few frames ahead. Its BG1 cells
are harvested with the same observation code as Option A and served in the
leading margin on an **exact world-cell match**. The real machine is snapshotted
around the lane and restored before its draw.

Result (2,500-frame Dragster and the five-track windows from Option A):

| Check | Result |
|---|---|
| Wrong served cells, Dragster →/←, Zoom Zoo, Bowl, Switcher, Monster | **0** |
| Leading served share, Dragster → | **99.84 %** (`ring`: 16.0 %) |
| Leading served share, Dragster ← | **100 %** (`ring`: 44.3 %) |
| Real-lane state trace, ring vs ghost, 2,500 frames | **identical** |
| Frame-2500 WRAM/VRAM/CPU/DMA dumps | **identical** |
| Native 4:3 SHA-256 | `3417f91a…df44` |
| Extra CPU time | **+1.9–2.6 ms/frame**; hook p99 9.6 ms |
| Rollback snapshot | 324,175 bytes (two blobs + ~50 KB raster state) |

Bowl, Switcher and Monster gain substantially on the leading side; Zoom Zoo
barely changes, as expected for a vertical loop. Input reversals still give 0
wrong (coverage drops while the lane resyncs).

Implementation traps (fully documented in the change's report):

- The **raster journal** is host state outside the rollback blob. A ghost run
  overwrites it; the real draw then replays the ghost field. `ppu_rasterState*`
  saves/restores it.
- The **whole `Ppu` struct** must be saved/restored: the guest saveload does not
  cover the host rendering caches, overscan/interlace flags, render pointers or
  the port state. Without this the native image was corrupted while the
  CPU/WRAM trace stayed identical.
- `snes_frame_counter` is in the current rollback residue (the earlier planning
  note was wrong); the lane still uses a sentinel to mute the probe.
- `s_resume_pc` / `s_wai_halted` remain outside the snapshot; the beam driver
  gained get/set accessors.
- BG1 CHR is **not** static from boot: it changes at race entry (Dragster
  1381–1430, Zoom Zoo 1404–1510), then stabilises. A 30-frame stable warm-up
  gates ghost serving; `SNESRECOMP_WS_GHOST_CHR_LOG` records the hashes.
- The resync period dominates the p99; raising it from 60 to 600 frames gave
  9.6 ms p99 instead of 25 ms, with equal or better coverage. Input and scene
  changes still resync immediately.

## 10. Map decode spike (Option C revisited)

Change: `openspec/changes/uniracers-ws-map-decode/` (spike, runs in parallel with
the ghost lane in an isolated project copy).

Seed evidence (details in its `design.md`):

- VRAM dumps must be read **little-endian**. That matches the renderer and the
  probe. The older "big-endian tile words" remark is wrong.
- BG1 CHR (`0x2000–0x5FFF`) is static across Dragster frames 2500/3500/5000.
  The track picture is fully determined by the map words, which are mostly
  character 0/2 with palette bits plus a few shape characters.
- The `$7F800F` high table holds **final BG1 map words**: Zoom Zoo 3504 has
  108/108 distinct words present, and ~53 % of 4-cell row windows appear
  byte-for-byte.
- Layout: pieces 4 cells wide, +2 bytes per cell, +8 bytes per row
  (`work/ws-map-decode/logs/seg-layout-zoomzoo-3504.txt`).
- `$81:B3DE` is a plain strided copy. The per-piece pointer comes from its
  caller.

**Result (spike go):** the track map is fully decoded.

- WRAM `$7F` holds an RNC1-unpacked image that is static from load to finish:
  a piece-id grid at `$7F:000F` and a library of 4×4-cell pieces at `$7F:800F`.
- `cell = piece[grid[wy>>2][wx>>2]][wy&3][wx&3]` matches the probe at 100 % on
  Dragster, Zoom Zoo and Bowl. The rest of the misses are transient blank start
  cells.
- All 45 tracks are RNC1 blobs in ROM banks `$18`–`$1F`, listed by the directory
  `$82:B6F9`.
- They render offline pixel-exact.

Full format: `docs/UNIRACERS_TRACK_FORMAT.md`; report:
`openspec/changes/uniracers-ws-map-decode/report.md`. Recommendation: a runtime
`source` mode that reads `$7F` directly. It is cheaper than the ghost lane and
covers cells the camera never shows (ultrawide).

## 11. Runtime source mode (final 16:9 BG1 answer)

`openspec/changes/uniracers-ws-source/` implements `SNESRECOMP_WS_OFFWINDOW=source`:
margin cells come from the `$7F` track table at `world + anchor`.

| Track | Leading wrong / masked | Trailing wrong / masked |
|---|---|---|
| Dragster →, Dragster ←, Zoom Zoo, Bowl, Monster | 0 / 0 | 0 / 0 |
| Switcher | 0 / 704 (before the 3 ambiguous anchors resolved) | 0 / 2,448 |

- The guest state trace is identical to `ring`.
- The CPU cost is within noise.
- Never-seen served cells match the offline ROM decode (173,567 / 0 differences).
- The ghost lane is parked: +2.3 ms/frame for the same result.
- Report: `openspec/changes/uniracers-ws-source/report.md`.

