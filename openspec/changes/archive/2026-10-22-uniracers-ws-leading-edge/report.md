# uniracers-ws-leading-edge — report

Date: 2026-10-21
Status: Option A **go** (test switch); Option C **no-go** for this change.

## 1. Summary

Option A (verified ring cells) is implemented behind
`SNESRECOMP_WS_OFFWINDOW=ring` and validated on all five 1P tracks plus a
leftward-motion section:

- **0 wrong served margin cells** against the probe ground truth on every run;
- native 4:3 is **bit-identical** (`3417f91a…df44` at Dragster frame 2500);
- WRAM, VRAM, CPU and DMA state at frame 2500 are **identical to the clamp run**;
- cost is **≈0.07 ms/frame** over 2,500 frames (inside measurement noise);
- **0 masked/served state changes** across 84,871 consecutive visible
  cell-frame pairs (no seam flicker in the measured windows).

The trailing margin now shows authentic track. The leading margin is still
background, exactly like the clamp, so this is a strict improvement over the
clamp on horizontal tracks rather than a full 16:9 fill.

Option C (source-backed leading cells) is **no-go for this change**. The
uploader writes a 16/17-word row chunk that starts at the last native cell; it
does not provide a clean source for the three leading world cells. The builder
is a multi-segment assembler, and the segment pointer list has not been decoded.
The next change can continue from that point.

## 2. What was built

### 2.1 Seen table

Every field, before line 1 is rasterized, the host records for each of the
32×32 BG1 ring cells seen by the native 256-px view:

```text
world_x, world_y, tile_word
```

The world keys come from the unwrapped BG1 scroll (`& 0x3ff` per frame,
shortest-delta unwrap). The table is refreshed per line from that line's
journal + HDMA scroll, so a raster split cannot move the key.

The `NOW PLAYING` screen can already satisfy `InLiveRace()` (it was observed
with `base=0000, mode=03`). The table and unwrap are reset whenever the BG1 map
base or `BGMODE` changes at line 1, so pre-race entries cannot survive into the
race. Race frames report `base=0c00, mode=19`.

### 2.2 Renderer-side verification

A margin BG1 cell is drawn from the ring only when

```text
seen[cy][cx].world == the world cell the margin pixel needs
and VRAM[sc] == seen[cy][cx].tile
```

Otherwise the renderer substitutes a fully transparent 16×16 character
(pixel 0 in all four sub-characters; found per track by scanning VRAM). The
substitution happens inside `PpuDrawBackgroundBig`, before pixel decode, and
only for `screen_x < 0 || screen_x >= 256`. It never writes guest VRAM.

The check `sc == race_map_base + cy*32 + cx` makes the hook inert on scanlines
where the game repoints BG1's map base (the HUD bands).

### 2.3 Why not the VRAM draw bracket

A host bracket (save/write/restore around the raster) was implemented first and
produced 0 wrong cells on Dragster, but it corrupted the HUD timer: the HUD
bands repoint BG1/BG3 map usage, and a ring-cell write leaked into the timer
glyphs. A BG3-only capture proved it. The renderer hook fixes that by
construction and is the final mechanism (design D2 fallback).

## 3. Results

Probe window, same run as the presentation. "Wrong" is a served cell whose
probe word differs from the native ground truth for that world cell. "Masked"
cells are background and are never counted as wrong.

| Track / window | Frames | Native conflicts | West correct / wrong / masked | East correct / wrong / masked |
|---|---:|---:|---|---|
| Dragster 2300–2500 | 201 | 0 | 139,104 / 0 / 0 | 22,624 / 0 / 118,720 |
| Zoom Zoo 3300–3505 | 206 | 0 | 113,621 / 0 / 9,352 | 70,860 / 0 / 46,518 |
| Bowl 2000–2300 | 301 | 0 | 103,867 / 0 / 109,275 | 150,915 / 0 / 62,369 |
| Switcher 2000–2300 | 301 | 0 | 174,180 / 0 / 13,635 | 41,714 / 0 / 185,248 |
| Monster 2000–2300 | 301 | 0 | 119,972 / 0 / 38,447 | 49,382 / 0 / 190,848 |
| Dragster left 3300–3600 | 301 | 0 | 91,392 / 0 / 114,912 | 170,912 / 0 / 34,048 |

The leftward route reverses the camera (244 of 300 frame deltas negative,
mean −11 px/frame). The trailing side mirrors as expected: the east side is
authentic and the west side is masked.

"Unverifiable" spans (world cells never observed natively in the window) are
excluded by the scorer; they are 0–29,566 per side depending on the capture.
The primary guarantee is by construction (world key + tile equality), not the
ground-truth join.

Track selection was confirmed with a `dump` at `NOW PLAYING` for Bowl,
Switcher and Monster (screenshots in `work/ws-leading-edge/frames/dumps/`).

## 4. Invariants

| Check | Result |
|---|---|
| Native 4:3, switch unset vs set | `3417f91afc8f6d47ea4ab3f7b11bbeec4cc7491a23ab71cc6c956f5fb3f5df44` both |
| Dragster ring vs clamp at frame 2500 | WRAM, VRAM, CPU, DMA dumps byte-identical |
| Native probe conflicts, all tracks | 0 |
| Guest VRAM writes | none (renderer substitution only) |
| HUD timer / BG3 | unchanged (wide clamp-vs-ring capture) |

The clamp run comparison used a live frame 2600 and historical dumps at frame
2500, so the capture boundary is the same for both modes.

## 5. Cost and flicker

- CPU time, Dragster 2500 frames: ring `15.2336 s`, wide no-clamp `15.0622 s`
  → **+0.1714 s / 2500 = +0.0686 ms/frame**. This is process CPU seconds and
  includes the seen-table work; it is below the 0.5 ms budget.
- Masked/served toggle rate: **0 changes** across 32,825 consecutive visible
  cell-frame pairs on Dragster and 52,046 on Zoom Zoo. A cell that is visible on
  consecutive frames keeps its state in these captures.

## 6. Option C — investigation and no-go

### 6.1 What writes the leading edge

At Dragster frame 2756 (`hscroll=737`, native cells 14..30) the only DMA from
the staging buffer is:

```text
src 00:0433  dest 0x0DBE  size 32 bytes  channel 0  -> $2118
```

`0x0DBE` is row 13, x=30 at map base `0x0C00`; the transfer starts at the last
native cell and crosses into the next map row. It is not a clean 3-cell
pre-write of the leading world row. The earlier Zoom Zoo trace at frame 3504
shows the same shape: a 17-word/34-byte chunk staged at `$0433`, matching the
visible row segment, installed one map row ahead. The leading cells beyond the
chunk are written only when the chunk shifts on a later frame.

### 6.2 The +0 cell

The last native cell (`start+16`) is the same ring cell as the first margin
pixel. Its 70% is native visibility, not a game pre-write. The +1/+2 cells are
stale (36%), which matches the measured per-cell percentages.

### 6.3 Builder inputs

From the earlier trace and the frame-2756/3504 captures:

| Symbol | Meaning |
|---|---|
| `$76` | loop count (the 16/17-word row window) |
| `$023E` | source pointer into the `$7F` high table |
| `$0236` | source stride (observed `0x0002`) |
| `$0339` | staging destination (`$0433`, parallel `$0455`) |
| `$0433` → DMA | `$2118`, one horizontal chunk per frame |

The builder is **not** a contiguous `table + offset` copy. The read watch shows
non-contiguous `$7F` pointers inside the row assembly (stage 1 / segment
lookup), and the pointer list is computed or queued rather than read from a
visible table. Widening `$76`/`$023E` by 3 cells would read across segment
boundaries and mix unrelated rows; it cannot be accepted without decoding the
segment list.

### 6.4 Go/no-go

**Option C: no-go for this change.** The evidence does not support a safe,
source-backed leading fill within the change's cost and state-isolation budget.
No synthetic resolver call was attempted because the segment list is still
unknown and the hook would have to reproduce the caller's segment selection.
The next change should decode the segment pointer/row table, then prototype a
widened scratch-buffer call and score it against the probe.

## 7. Decision

| Option | Decision | Notes |
|---|---|---|
| A — verified ring cells | **go** | 0 wrong on five tracks + leftward; 4:3 and guest state exact; ≈0.07 ms/frame; no flicker measured |
| C — source-backed leading | **no-go** | leading cells are not in the row chunk; segment list undecoded |

Recommended default: promote `ring` over `clamp` in a follow-up user-facing
change. It is never worse than the clamp and restores authentic trailing track.
`source` remains unavailable. The clamp stays the default in this change (the
non-goal is to change the default here).

## 8. Artifacts and reproduce

Work artifacts (gitignored, ROM-derived): `work/ws-leading-edge/`.

- logs: `ow-ring-<track>-<window>.csv` (probe), `ow-ring-<track>-mask.csv`
  (visit log: `frame,cell_x,cell_y,world_x,world_y,real,mask,masked`)
- frames: `ow-ring-<track>-<frame>.ppm`, `wide-clamp-vs-ring-dragster-2500.png`
- dumps: `frames/dumps/<track>-nowplaying.png` (track confirmation)
- patch: `patches/ws-leading-edge.diff` (game_rtl + framework hook)
- scorer: `scripts/world_key_check.py` (`--mask-log`, `--served`)

Reproduce one track:

```sh
P="$HOME/Seafile/Personal Projects/unirally"
cd "$P/work/ws-native-map-spike/project/UniracersSNESRecomp/build-local"
rm -f saves/save.srm saves/save.srm.bak
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_NO_LAUNCHER=1 \
SNESRECOMP_RUN_FRAMES=2500 SNESRECOMP_WS=1 \
SNESRECOMP_WS_OFFWINDOW=ring \
SNESRECOMP_WS_OFFWINDOW_LOG="$P/work/ws-leading-edge/logs/ow-ring-dragster-mask.csv" \
SNESRECOMP_BGPROBE_FRAME=2300,2500 \
SNESRECOMP_BGPROBE_FILE="$P/work/ws-leading-edge/logs/ow-ring-dragster-2300-2500.csv" \
./UniracersSNESRecomp --rom "$P/Uniracers (USA).sfc" --no-launcher \
  --script "$P/work/ws-native-map-spike/scripts/route-dragster-move.txt"

python3 -I "$P/work/ws-leading-edge/scripts/world_key_check.py" \
  "$P/work/ws-leading-edge/logs/ow-ring-dragster-2300-2500.csv" \
  --mask-log "$P/work/ws-leading-edge/logs/ow-ring-dragster-mask.csv" --served
```

## 9. Caveats / next

- The masked leading edge is still background; this is not a source-backed
  16:9 fill.
- The renderer hook is a local patch to the `snesrecomp` submodule (next to the
  probe). It must be upstreamed or carried as a patch when the pin moves.
- `unverifiable` spans are excluded by the ground-truth join; the by-construction
  rule is the real guarantee.
- Option C next: decode the `$7F` segment pointer/row list, then test a widened
  `$81:B3D0` into a scratch buffer and score the extra cells.

## Closure (2026-10-22)

Archived with Option A shipped and Option C recorded as a no-go. The Option C
investigation was later superseded by `uniracers-ws-source`, which fills both
margins from the track table. Tasks 4.4/4.5 were deferred and the 5.x prototype
steps were not applicable; nothing remains outstanding in this change.
