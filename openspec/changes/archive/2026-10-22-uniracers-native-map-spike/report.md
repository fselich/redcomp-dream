# Native map spike (Phase F1) — report

Date: 2026-10-21
Change: `uniracers-native-map-spike`
Outcome: **no-go for a source-backed wide-map renderer in this spike; clamp stays.**

> **Addendum 2026-10-21 (pm), from `uniracers-ws-offwindow` — BG1 is 16×16 in race.**
> Several statements below assume a 256×256 (8×8-tile) BG1 map. In race
> `BGMODE = 0x19`: BG1 uses 16×16 tiles, so the 32×32 ring is **512×512 px**.
> The 16:9 margins sample real ring cells: the trailing side is already correct
> world data, and only ~3 leading-edge cells per row are stale. They are not a
> "wrap of the 256-wide map" or the "opposite edge". Validated formula and
> measurements: `docs/UNIRACERS_BG1_RING_FINDINGS.md`.
> The "chunk-to-screen transform" blocker below is resolved: the 17-word chunk
> is the 16 visible cells + 1 overhang at `(hscroll >> 4) & 31`.

## Question

The full-track maps show complete courses. Can we use the level data to fill the
43 px BG1 margins with real track, instead of clamping to the native 256 columns?

## What was proven

### 1. The full level data exists

At frame 2500 (Dragster, live race) and frame 3504 (Zoom Zoo, loop/slope), WRAM
contains two 16-bit tables at `$7F000F` (low) and `$7F800F` (high), addressed
with the frame's row stride and column count:

| Frame | `$00A5` | `$00A7` | `$04F1` (cols) | `$04F3` (stride) | Table shape |
|---|---:|---:|---:|---:|---|
| Dragster 2500 | `0x303E` | `0x0360` | `0x0400` | `0x0800` | 32×1024 |
| Zoom Zoo 3504 | `0x1F40` | `0x05F0` | `0x0100` | `0x0200` | 32×256 |

Both tables contain thousands of nonzero cells, including the loop/slope
section. They are not empty, and the level is not limited to 256 columns.

### 2. Stage 1 reads real cells

`$81:8A4A` was called from C on a saved `CpuState` + WRAM snapshot with the
frame's `$00A5/$00A7`, `$04F1/$04F3`:

```text
Dragster 2500 off=0:
  0232=000C 0234=000D 0236=001C 0238=01A0
  segment/high words 0260=3800 0262=3800 ... 0272=3400

Zoom Zoo 3504 off=0:
  0232=0001 0234=0002 0236=0048 0238=0020
  segment/high words 0260=4800 ... 0272=4800
```

The four low words come from the table and change with the offset; the call
returns cleanly. The generator can address real table cells.

### 3. Stage 2 is not yet understood

`$81:8B95` (stage 1 + stage 2) always produced `$00A0` in `$0230..$0242` for
the sampled offsets. `$00A0` is a stage-2 sentinel, not proof of missing level
data. The conversion from stage-1 words to the final BG1 tilemap word is the
unresolved link.

### 4. BG1 is a screen-space window, not a world-keyed tilemap

- *(Corrected later: little-endian is right; see `docs/UNIRACERS_TRACK_FORMAT.md`.)* Decoding VRAM as **big-endian words** is required; little-endian makes the
  HUD and tile indices look blank.
- The 32×32 BG1 map is rebuilt in place as the camera moves. Mapping columns by
  PPU `hScroll` and by `$00A5` world tile both fail to form a stable world map.
- A world-keyed cache keyed by `a5_tile + (column - 16)` produced
  5,619 overlaps and **5,210 conflicts** (≈93%) on Zoom Zoo. The same screen
  column can show different track cells at the same world key, so VRAM
  stitching is not a valid source for unseen margins.

This is the blocker: the table exists, but the runtime map is a screen-space
projection whose exact world↔cell transform is still undecoded.

## Determinism gate

Two earlier-looking “nondeterministic” runs were traced to the harness, not the
game:

- **`DisableFrameDelay=1`** changes frame scheduling and is not valid for
  deterministic captures. Keep normal pacing.
- **Persistent `build/saves/save.srm`** made each run start from the previous
  run's SRAM. Remove `saves/save.srm` and `saves/save.srm.bak` before each
  deterministic run.

With pacing on and clean SRAM, two native Dragster runs at frame 2500 produced
byte-identical PPMs:

```text
SHA-256 3417f91afc8f6d47ea4ab3f7b11bbeec4cc7491a23ab71cc6c956f5fb3f5df44
```

Native 256×224 and clamp 342×224 captures are in `frames/gate-native-2500-1.ppm`
and `frames/gate-clamp-2500.ppm`.

## Decision

- Do **not** ship a native margin renderer yet.
- Keep `SNESRECOMP_WS_TRACK_CLAMP` as the validated 16:9 BG1 behavior.
- Do not use the old “no data” explanation; the table is real. The open problem
  is the stage-2/tilemap transformation and the screen-space projection.

## Next diagnostic

1. Trace `$81:8B95` stage 2 at the exact call site, including `$12C7`, `$0200`,
   `$0202`, `$0FAB`, `$0EEF`, and `$7E:C000/$7E:A000`.
2. Capture the **VRAM DMA write** that installs a BG1 column and follow its
   source buffer from `$0230`/`$0260` to VRAM.
3. Only after that, try a table-backed margin renderer under a new test switch.

## Artifacts

- `baseline.md`
- `dumps/clean-dragster-2500-*`, `dumps/clean-zoomzoo-3504-*`
- `dumps/tables-dragster-2500.{json,txt}`,
  `dumps/tables-zoomzoo-3504.{json,txt}`
- `logs/clean-genscan-dragster-2500.log`,
  `logs/clean-genscan-zoomzoo-3504.log`
- `frames/gate-native-2500-1.ppm`, `frames/gate-clamp-2500.ppm`
- scripts: `capture_frame.py`, `capture_bg1_sequence.py`, `capture_wram_series.py`,
  `dump_tables.py`, `stitch_bg1.py`


## Follow-up upload trace (after the first report)

The stage-2 sentinel is not the BG1 source. At frame 3504 the real BG1 map
upload is:

```text
PC 0x81B3E2:  A=0x4400, X=0x805C, Y=0x0433
DMA:          src 00:0433, dest 0x0E88, size 34, channel 0, $2118
Source:       bank $7F high table at $7F800F + 0x5C
```

The 17-word staging chunk byte-swaps to the exact BG1 map row 18, x=7..23:

```text
0x44 0x44 0x44 0x44 0x44 0x44 0x682 0x882 0x80 0x80 0x9c 0x80 0x80 0x80 0 0 0
```

So the upload path is a **17-word horizontal BG1 chunk**, not a generic column
copy. The destination `0x0E88` does not equal the nominal
`0x0C00 + 18*32 + 7`, so the map-base/scroll offset is still open.

Across frames 3490–3504 the destination steps down one map row per frame
(`0x0FC1` → `0x0E88`), while the stage chunk matches a map row two rows higher.
The frame-3504 chunk is still at map row 18 x=7..23 in the 3505/3506 snapshots,
not at the destination row 20. DMA and present-time VRAM are not the same
instant; the chunk-to-screen transform remains the blocker.

Evidence is in `work/ws-native-map-spike/upload-trace.md`. This moves the
blocker from “where is the data?” to “what is the chunk-to-screen mapping?”.

The clamp remains the accepted behavior until that mapping is proven and a
chunk-level margin prototype is validated.


## Pre-DMA breakpoint (frame 3504)

A breakpoint at `$81:B3F9` captured the exact pre-DMA state. At that instant:

```text
$0433 chunk:  00 44 00 44 ... 00 86 02 86 04 86 ...
BG1 map:      exact match at row 19, x=7..23
DMA:          src 00:0433, dest 0x0E88, size 34
```

Map base `0x0C00` makes the destination row 20, x=8. The chunk is one row above
and one column left of the destination: the uploader writes **one row ahead** of
the displayed row. This confirms the map-base semantics. `step_block` remained
parked at `$B3F9`, so the capture is pre-DMA.

Remaining blocker: horizontal chunk mapping (left/right adjacent chunks).


## Read-watch result

A test-only bank-`$7F` read watch was added under `work/`. At frame 3504 it
showed that `$81:B3D0` assembles the staging row from **multiple non-contiguous
high-table segments** (`$7F806B`, `$7F87C7`, `$7F87E7`, `$7F8807`, `$7F8027`,
`$7F8C33`, `$7F81B3`, ...), not one contiguous `source ± 34` range. The `Y`
register walks `$0433` upward; `X` jumps between table segments.

This explains why the stage chunk did not match a single table offset. The next
decode target is the segment/pointer table that feeds those `X` values; only
then can the adjacent left/right margin chunks be selected.

## Caller/read-watch limit

An all-bank read watch at frame 3504 attributes the high-table reads to
`interp@$82D4EE`; disassembly shows that is a small `STZ $57 / BIT $57 / BPL`
wait loop, so the label is stale (interrupt/NMI) or the path is queued. No read
carries the literal pointer values, so the segment list is computed or queued.
A breakpoint at `$81:B3D0` entry does not hit; `$81:B3F9` does.

The caller/interrupt queue is the remaining decode target for horizontal margin
chunks. The clamp remains the accepted behavior.

## PB/S caveat

The read-watch records `PB=81, S=01EB`; the stack at `S` contains
`7C 8A 81`, a return to `$81:8A7C` inside stage 1. The
`g_last_recomp_func` value (`interp@$82D4EE`, a wait loop) is stale. The
high-table read addresses may belong to stage 1, not `$81:B3D0`; the
multi-segment interpretation is therefore not yet proven as the uploader.
Separate those two paths before using them as a mapping model.

## NMI check

The read-watch logs `NMI=0` for the frame-3504 high-table reads. They run with
`PB=81, S=01EB`; the host stack is only `interp@$82D4EE`, while the guest stack
points to `$81:8A7C`. The path is not NMI; it is either a reentrant IRQ/queued
routine or a host stack that does not mirror the guest call. The `$B3D0`
attribution remains unproven.

## IRQ / vblank check

The frame-3504 high-table reads report `NMI=0 IRQ=0 VB=0`, `PB=81`,
`S=01EB`, with the guest stack pointing at `$81:8A7C`. They are not
interrupts; the stale function label is an AOT/inlined-call artifact. Break at
`$81:8A7C` is the next step to separate stage 1 from the staging builder.

## Breakpoint separation result

`$81:8A7C` does not hit; `$81:8A4A` (stage 1 entry) does hit at frame 3504.
The high-table reads belong to stage 1 or its callees, not `$81:B3D0`. The
previous “B3D0 multi-segment staging” attribution is invalid. Instrument
stage-1 exit and correlate `$0260` with the BG1 row next.

## Stage-1 correlation: negative

The frame-3504 stage-1 output (`$0260`: `4800...0802`) does not match any BG1
row/column window, normal or byte-swapped. Stage 1 emits intermediate segment
codes, not final tile words. Direct stage-1-to-BG1 margin generation is
rejected; stage 2/uploader remains the only path.

## Stage-2 controlled inputs

Stage 2 at `$81:8B98` converts segment values: `$4800` is empty; `$1C00`,
`$2024`, `$7FFF` produce nonblank output. A 2D scan at frame 3504 found
`oy=+64 -> 0x007F` and `oy=+128 -> 0x0001/0x0006`, invariant across `ox`.
The stage-1/2 path is a vertical column keyed by Y; `$00A5` alone is not the
horizontal margin key. Scroll/map-column state remains the missing axis.

2D scan detail: `oy=+64` produced `0230=007F` for all `ox`; `oy=+128`
produced `0230=0001`, with only `$0262` alternating `5ACC/5AE0` (edge bit
variation). `$00A5` is not the horizontal margin key. Stage 2 conversion is
confirmed; horizontal scroll/map-column state is the remaining missing input.

## Fine scans

Fine `oy` scan found real stage-2 rows at `-16..168`. At `oy=64`, `ox` step 8
still gave `0230=007F` for every `ox`; at `oy=128` only the low `$0262` edge
varied. Stage 1/2 is a vertical tile column driven by Y; `$00A5` is not the
horizontal margin key. Horizontal margins require map-column/scroll or the
high-table chunk sequence.

## Candidate horizontal keys

With `oy=+64`, varying `$041D`, `$041F` and `$00A5` over ±256/512 produced the
same stage-2 column (`0230=007F`, `0260=1C00`). None is the horizontal margin
key for stage 1/2. The horizontal source must be the BG1 map/chunk layer, not
the generator.

## BG1 row / table search

The full BG1 row 19 and its 17-word central segment do not occur contiguously
in bank `$7F` (normal or byte-swapped). The row is assembled from multiple
segments; margin columns are not a simple table slice. The segment
assembler/stream remains the horizontal source.

## Staging word → source mapping

At `$81:B3F9`, staging x=7 maps to `$7F806B`, x=8 to `$7F806D`, x=9 to
`$7F87C7`, etc.; source bytes are byte-reversed stage words. The VRAM snapshot
at that breakpoint has BG1 row 19 zero (pre-DMA), so x→map placement cannot be
closed there. A post-DMA map capture remains required for margin columns.

## Post-DMA placement confirmed

Frame 3505 pre-DMA: stage bytes match BG1 row 19 x=7..23; DMA dest `0x0E67`
is row 19 x=7. Placement of the 17-word chunk is confirmed. Margins x<7/x>23
still require adjacent source segments.

## Same-run capture requirement

The adjacent-source test exposed a snapshot inconsistency: the BG1 row at
`$B3F9` is not reproducible across separate runs. Cross-run pre/post evidence
is invalid. The next capture must break at frame N, continue in the same
process, and capture post-DMA at N+1 before comparing adjacent source segments.

## Same-run pre/post result

Same-process capture: stage at `$0433` (frame 3504) appears in post-DMA BG1 row
19 x=7..23, but the frame's DMA dest is row 20 x=8 and post row 20 is unchanged.
The stage is one row above the pending transfer; `stage -> dest` is not direct.
The transfer that actually installs the stage remains the missing link for
adjacent margin chunks.

## Multi-frame same-run capture

The DMA destination walks one map row per frame (`0x0F47` → `0x0E47` across
3498–3506). Stage content is `0004...` through 3502 and `0044...` from 3503;
BG1 row 19 stays stable. At 3505 the destination is row 19 x=7; at 3504 it is
row 20 x=8 (one row ahead). This is a ring/scroll row writer, explaining the
row-19 match but not horizontal margin columns outside 0..31.

## External oracle status

VGMaps metadata was consulted, but full-map/thumbnail downloads return HTTP
403 in this environment. The image oracle is unavailable. Without it, there is
no proof that the ±43 px margins contain real track; the 32-column BG1 map is
the only runtime source.

## Local line-art oracle

The local `maps/uniracers-crawler-zoom-zoo-snes-map.webp` (line-art, not a tile
rip) shows that frame 3504 is a vertical-loop section with a single narrow
track line. There is no adjacent parallel track in the ±43 px margins; the
margins are background. The clamp is geometrically correct for this frame.
Line-art maps are sufficient for topology/visibility, not tile-level checks.

## Dragster correction: clamp is not universally correct

The local Dragster line map is a long horizontal strip. At native frame 3500
the track fills the full 256 px width, so the ±43 px margins should continue
the track horizontally. The clamp hides that continuation. Clamp is correct
for vertical/loop sections (Zoom Zoo 3504) but not for Dragster horizontal
sections. It remains a conservative fallback, not a universal fix; the no-go is
still technical (off-window columns are unsourced).

## Five-track geometric classification (local maps)

| Track | Section | Margins should be | Clamp correct? |
|---|---|---|---|
| Dragster | horizontal | track | No |
| Zoom Zoo | vertical loop | background | Yes |
| Bowl | horizontal | track | No |
| Switcher | horizontal | track | No |
| Monster | horizontal | track | No |

Four of five tracks need real off-window track. The clamp hides it; it is a
fallback, not a universal 16:9 solution. Zoom Zoo's vertical loop is the one
case where the clamp is geometrically correct.

## Test-only edge-repeat experiment

A test-only `SNESRECOMP_WS_TRACK_REPEAT=1` copied the BG1 edge columns into the
margin ring slots and restored them after rasterization.

- Dragster 3500: full-width horizontal track, matching the line-art.
- Zoom Zoo 3500: track disappeared/broke; the edge overwrite damaged a vertical
  curve.

Edge-repeat is rejected as a general solution. It confirms the horizontal
Dragster expectation but can erase real track; source-backed off-window columns
are still required.

## Closure (2026-10-22)

Archived as a historical no-go. The mapping questions left open here were
resolved by `uniracers-ws-offwindow`, the full table was decoded by
`uniracers-ws-map-decode`, and the production margin source was implemented by
`uniracers-ws-source` (all archived). The prototype tasks in this change were
made unnecessary by the no-go decision; no work remains here.
