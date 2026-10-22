# Uniracers native-map spike — findings and decision

Status: **no-go for a source-backed wide BG1 renderer in this spike; clamp
remains.** The level data and the uploader are real; the missing piece is the
chunk-to-screen transform.
Date: 2026-10-21
Change: `uniracers-native-map-spike`
Related: `docs/UNIRACERS_WIDESCREEN_TRACK_FILL.md`

> **Correction 2026-10-21 (pm) — BG1 is 16×16 in race.**
> Several statements below assume a 256×256 (8×8-tile) BG1 map. In race
> `BGMODE = 0x19`: BG1 uses 16×16 tiles, so the 32×32 ring is **512×512 px**.
> The 16:9 margins sample real ring cells: the trailing side is already correct
> world data, and only ~3 leading-edge cells per row are stale. They are not a
> "wrap of the 256-wide map" or the "opposite edge". Validated formula and
> measurements: `docs/UNIRACERS_BG1_RING_FINDINGS.md`.

> This document corrects the older “the game does not author track in the
> margins” conclusion. Do not repeat that claim.

## 1. Question

The full-course maps show loops and long tracks. Can the real level data fill
the 43 px BG1 margins, instead of clamping BG1 to the native 256 columns?

## 2. Short answer

Yes, the data exists; no, the current spike cannot yet turn it into a validated
margin renderer.

- The level is stored in two 16-bit tables in bank `$7F`.
- The runtime uploader copies 17-word horizontal chunks from the high table
  into WRAM `$0433` and DMAs them to the BG1 map.
- The exact chunk → screen row/x / map-base transform is not decoded. Until it
  is, a margin renderer would be guesswork.

## 3. Determinism rules (must read)

Two runs can differ if the harness is wrong:

- Use **normal frame pacing**. `DisableFrameDelay=1` changed scheduling in this
  environment and is not valid for deterministic gates.
- Remove `build/saves/save.srm` and `save.srm.bak` before each run. Persistent
  SRAM makes each run start from the previous run's save.

With pacing on and clean SRAM, two native Dragster runs at frame 2500 produced
byte-identical PPMs (SHA-256
`3417f91afc8f6d47ea4ab3f7b11bbeec4cc7491a23ab71cc6c956f5fb3f5df44`).

## 4. Level table layout

At a live frame, the game exposes:

| Symbol | Meaning | Dragster 2500 | Zoom Zoo 3504 |
|---|---|---|---|
| `$00A5` | world/track X (1/64 tile units) | `0x303E` | `0x1F40` |
| `$00A7` | world/track Y (1/64 tile units) | `0x0360` | `0x05F0` |
| `$04F1` | column count | `0x0400` | `0x0100` |
| `$04F3` | row stride (bytes) | `0x0800` | `0x0200` |
| `$7F000F` | low table base | — | — |
| `$7F800F` | high table base | — | — |

The table is 32 rows high. Dragster uses 32×1024 cells (64 KB); Zoom Zoo uses
32×256 cells (16 KB). The earlier `$0236/$8FA` interpretation was wrong.

The low table contains small values/attributes; the high table contains the
chunk/tile data that the uploader copies. Both are real: Dragster had 1,946
nonzero low cells; Zoom Zoo had 4,331 low and 6,653 high nonzero cells.

Offline dumps:

- `work/ws-native-map-spike/dumps/tables-dragster-2500.json`
- `work/ws-native-map-spike/dumps/tables-dragster-2500.txt`
- `work/ws-native-map-spike/dumps/tables-zoomzoo-3504.json`
- `work/ws-native-map-spike/dumps/tables-zoomzoo-3504.txt`

## 5. What stage 1 does

`$81:8A4A` is called at the frame's `$00A5/$00A7/$04F1/$04F3`. A test-only
C probe (`SNESRECOMP_WS_TEST_GEN=1`, frame selected by
`SNESRECOMP_WS_TEST_GEN_FRAME`) saves `CpuState` + WRAM, calls the guest
function, and restores everything.

Examples at `off=0`:

```text
Dragster 2500:
  a5=303E a7=0360 0405=00C0 0407=000D
  0232=000C 0234=000D 0236=001C 0238=01A0
  0260=3800 ... 0272=3400

Zoom Zoo 3504:
  a5=1F40 a7=05F0 0405=007D 0407=0017
  0232=0001 0234=0002 0236=0048 0238=0020
  0260=4800 ... 0272=4800
```

So stage 1 addresses real table cells; the generator is not starved of data.

## 6. What stage 2 does not prove

`$81:8B95` (stage 1 + stage 2) writes `$00A0` to `$0230..$0242` for the
sampled frames. `$00A0` is a stage-2 sentinel. It is **not** evidence that BG1
has no track and it is **not** the BG1 upload source.

The real BG1 upload path is the `$0433/$0455` staging + DMA path.

## 7. The real uploader

At Zoom Zoo frame 3504:

```text
PC 0x81B3E2:  A = 0x4400, X = 0x805C, Y = 0x0433
DMA:          src 00:0433, dest 0x0E88, size 34, ch 0, $2118
Source table: $7F800F + 0x5C = $7F806B
```

`$81:B3D0` copies a 17-word (34-byte) chunk from the bank `$7F` high table to
low WRAM staging. The staging bytes are byte-swapped relative to the table:
table `00 44 ...` → staging `44 00 ...` (what the DMA/VRAM path expects).

The staging chunk matches BG1 map **row 18, x = 7..23** exactly:

```text
0x44 0x44 0x44 0x44 0x44 0x44 0x682 0x882
0x80 0x80 0x9c 0x80 0x80 0x80 0 0 0
```

So the uploader is a **horizontal 17-word chunk**, not a whole column. A single
DMA installs one chunk; it is not a full 32×32 map upload.

Cross-frame (Zoom Zoo 3490–3504), the DMA destination steps down by `0x20`
(one map row) on most frames:

```text
3490  0x0FC1   stage match map row 30
3494  0x0FA4   stage match map row 28
3498  0x0F47   stage match map row 24
3500  0x0F08   stage match map row 22
3503  0x0EA8   stage match map row 19
3504  0x0E88   stage match map row 18
```

The stage chunk matches a map row **two rows higher** than the destination
(e.g. frame 3504: match row 18, destination row 20). The stage chunk from frame
3504 also does not appear at its destination in the frame-3505/3506 snapshots.
So the DMA and the present-time VRAM snapshot are not the same instant, and the
offset is not yet a simple `base + row*32 + x`. Do not treat the match row as
the final destination.

Detail and raw correlation: `work/ws-native-map-spike/upload-trace.md`.

## 8. Why VRAM stitching is not the answer

> **Correction (2026-10-21, `uniracers-ws-map-decode`):** VRAM dumps (both the debug-server dumps and the spike's own) decode correctly **little-endian**. That is what the renderer reads; it matches the probe's words and the `$7F` tables byte for byte. The big-endian reading below was a misinterpretation. See `docs/UNIRACERS_TRACK_FORMAT.md`.

Decoding BG1 correctly requires **big-endian** words. Little-endian makes the
HUD and tile indices look blank.

The visible 32×32 map is rebuilt in screen space as the camera moves. Attempts
to key its columns by `$00A5` world tile produced 5,619 overlaps and 5,210
conflicts (~93%) on Zoom Zoo. The same world key can show different map cells;
a VRAM history cache cannot provide unseen margins.

## 9. Decision

- Do **not** ship a native margin renderer.
- Keep `SNESRECOMP_WS_TRACK_CLAMP` as the validated 16:9 behavior.
- Do **not** describe the clamp as “the game has no track data”; the data is
  there.
- Do **not** use the stage-2 `$00A0` output as proof of absence.

## 10. Next experiment (reproducible)

1. Trace `$81:B3D0`/`$81:B3E2` and the DMA for **consecutive frames**.
2. Record `$023E` source offset, `$0433` chunk, DMA dest, and the map at the
   next present.
3. Derive the chunk → (map row, x) and map-base/scroll transform.
4. Read the adjacent high-table chunks for the left/right 43 px margins.
5. Only then build a test switch (new, not `SNESRECOMP_WS_TRACK`) and validate
   native 4:3, clamp, loops/slopes and no duplicate/wrap fragments.

If step 3 cannot be proven, the clamp is the final answer.

## 11. Artifact map

- `work/ws-native-map-spike/baseline.md` — build, routes, captures, switch
  mismatch, determinism.
- `work/ws-native-map-spike/upload-trace.md` — staging/DMA evidence.
- `work/ws-native-map-spike/dumps/clean-*` — clean-SRAM WRAM/VRAM snapshots.
- `work/ws-native-map-spike/logs/clean-genscan-*` — stage 1/2 probe logs.
- `work/ws-native-map-spike/logs/trace-upload-3504.json` — watchpoint log.
- `work/ws-native-map-spike/logs/dma-3504.json` — DMA list.
- `work/ws-native-map-spike/scripts/` — capture/stitch/table tools.
- `openspec/changes/uniracers-native-map-spike/report.md` — full report.


## Follow-up: pre-DMA breakpoint (frame 3504)

A breakpoint at `$81:B3F9` captured the exact pre-DMA instant:

- `$0433` chunk: `00 44 00 44 ... 00 86 02 86 04 86 ...`
- BG1 map: exact match at **row 19, x=7..23**
- DMA: `src 00:0433`, `dest 0x0E88`, `size 34`

With map base `0x0C00`, `0x0E88` is **row 20, x=8**. The chunk is visible one
row above and one column left of the DMA destination: the uploader writes one
row **ahead** of the displayed row. This confirms the map base and the DMA
destination semantics.

`step_block` did not advance at this breakpoint; capture at `$B3F9` pre-DMA
instead. The open part is now horizontal chunk mapping: the current 17-word
segment is known, but adjacent left/right chunks are not yet mapped.

### Breakpoint state caveat

At `$81:B3F9` frame 3504, `$023E=0x8060`, `$0236=0x0002`,
`$0339=0x0433`; the stage chunk matches map row 19 x=7..23, and DMA dest
`0x0E88` is row 20 x=8 at base `0x0C00`. The raw high table at
`$7F000F+0x8060` is `0x5800...`, not the stage sequence `0x0044...`. So `$B3D0`
is not a plain read-only copy of that offset, or a second pass built the
staging. Treat the current source formula as unproven.

### Read-watch evidence: multi-segment row assembly

A test-only bank-`$7F` read watch (`SNESRECOMP_READ_WATCH_FILE`, frame range)
showed that `$81:B3D0` does not copy one contiguous table range. At frame 3504
it reads the high table at multiple non-contiguous addresses while walking the
staging buffer:

```text
7f:806b X=805C  → $0433
7f:87c7 X=87B8  → $0437
7f:87e7 X=87D8  → $043F
7f:8807 X=87F8  → $0447
7f:8027 X=8018  → $044F
7f:8c33 X=8C24  → $0457
7f:81b3 X=81A4  → $045F
```

So it is a **multi-segment row assembler**. The adjacent margin x segments must
come from the segment/pointer table that produces those `X` values, not from a
simple `source ± 34` read. This is the next decode target; the raw log is
`work/ws-native-map-spike/logs/readwatchregs-3504.log`.

### Pointer sequence

At frame 3504 the high-table source pointer ($023E) walks groups, not a
contiguous row:

```text
805C,805E,8060,
87B8,87BA,87BC,87BE,87C0,
87D8,87DA,87DC,87DE,87E0,
87F8,87FA,87FC,87FE,8800,
8018,801A,801C,801E,
8C24,8C2C,8C34,8C3C,8C44,
81A4,81AC,81B4,81BC
```

`$0236=2` inside a group; jumps between groups come from the caller selecting
another segment. The actual read address is `pointer + 0x0F` (`805C → 806B`,
`87B8 → 87C7`, ...). The caller's segment list is the next decode target.

### Caller status

The all-bank read watch attributes the high-table reads to `interp@$82D4EE`;
that address is a tiny `STZ $57 / BIT $57 / BPL` wait loop, so the function
label is stale (interrupt/NMI context) or the path is queued. No read carries
the literal segment pointers (`0x805C`, `0x87B8`, ...), so they are computed or
queued rather than loaded from a directly visible table. A breakpoint at
`$81:B3D0` entry does not hit; `$81:B3F9` does. The next decode target is the
interrupt/queue path that produces the pointer list.

### PB/S caveat

The read-watch logs `PB=81, S=01EB`; the stack contains `7C 8A 81`, i.e. a
return to `$81:8A7C` inside stage 1. The `g_last_recomp_func` label
(`interp@$82D4EE`, a wait loop) is stale. The high-table read addresses may be
stage-1 reads, not the `$0433` uploader. This attribution is now explicitly
**unproven**; separate stage 1 from the staging builder before using it as a
mapping model.

### NMI check

The read-watch logs `NMI=0` for the frame-3504 high-table reads, with
`PB=81, S=01EB`; the host stack is only `interp@$82D4EE`, while the guest stack
points to `$81:8A7C`. They are **not** in the NMI handler. The path is a
reentrant IRQ/queued routine or a host stack that does not mirror the guest
call. Do not attribute those reads to `$B3D0` without a separate breakpoint.

### IRQ / vblank check

For the frame-3504 high-table reads, the read-watch reports:

```text
NMI=0 IRQ=0 VB=0 V=45/56
PB=81 S=01EB
```

They are not NMI, not IRQ, and not in vblank. The stale `interp@$82D4EE` label
is an AOT/inlined-call artifact, not an interrupt. The guest stack points at
`$81:8A7C`; that is the next breakpoint to separate stage 1 from `$B3D0`.

### Breakpoint separation

`$81:8A7C` does not hit (mid-block). `$81:8A4A` (stage 1 entry) does hit at
frame 3504. The high-table reads therefore belong to stage 1 or its callees,
not to `$81:B3D0`. This invalidates the earlier “B3D0 multi-segment staging”
attribution. The next step is to instrument stage-1 exit and correlate `$0260`
with the BG1 row; the `$0433` uploader is separate.

### Stage-1 output correlation

At Zoom Zoo frame 3504 stage 1 emits:

```text
$0260: 4800 4800 4800 4800 4800 4800 4800 4800 0802 0802
```

That 10-word sequence does not match any BG1 row, column, reversed row/column,
or byte-swapped window. Stage 1 emits intermediate segment codes, not final
BG1 tile words. It cannot be used directly for margins; stage 2/the uploader
still has to be decoded.

### Stage-2 controlled inputs and 2D scan

`$81:8B98` with a preloaded `$0260` converts segment values; `$4800` is empty,
while `$1C00`, `$2024`, `$7FFF` produce nonblank output. A 2D scan at frame 3504
found real columns at `oy=+64` (`0x007F`) and `oy=+128` (`0x0001/0x0006`), but
the result was invariant across `ox`. The stage-1/2 path is producing a
**vertical column** keyed by Y; `$00A5` alone is not the horizontal margin key.
The horizontal axis must come from scroll/map-column state instead.

`oy=+64` gave `0x007F` for every `ox`; `oy=+128` gave `0x0001`, with only
`$0262` alternating `5ACC/5AE0` (low-bit edge variation). So `$00A5` is not
the horizontal margin key. Stage 2 conversion is confirmed; the horizontal
scroll/map-column key remains missing.

### Fine scans

A fine `oy` scan found real stage-2 rows at `-16..168` (not every offset).
At `oy=64`, `ox` step 8 produced `0230=007F` for every `ox`; at `oy=128` only
the low `$0262` edge varied. So stage 1/2 is a vertical tile column driven by
Y, and `$00A5` is not the horizontal margin key in this call. Horizontal
margins must come from map-column/scroll or the high-table chunk sequence.

### Candidate horizontal keys

With `oy=+64` (valid row), varying `$041D`, `$041F` or `$00A5` by ±256/512
left the stage-2 result at `0230=007F`, `0260=1C00`. None is the horizontal
margin key for stage 1/2. That path is a vertical column; the horizontal source
must be the BG1 map/chunk layer or another map-column state.

### BG1 row / high-table search

The full BG1 row 19 and its central 17-word segment do not occur contiguously
in bank `$7F`, normal or byte-swapped. The row is assembled from multiple
segments; the margin columns are not a simple `table + row*stride + x` slice.
The segment assembler/stream remains the horizontal source to decode.

### Staging word → source mapping

At `$81:B3F9`, staging x=7 comes from `$7F806B`, x=8 from `$7F806D`, x=9 from
`$7F87C7`, etc.; the source bytes are the stage words byte-reversed. But the
BG1 VRAM snapshot at that breakpoint has row 19 zero (pre-DMA), so the x→map
placement cannot be closed at that instant. A post-DMA map capture is still
required for the adjacent margin columns.

### Post-DMA placement confirmed

At the `$81:B3F9` breakpoint for frame 3505, the staging bytes match BG1 row 19
x=7..23 and the DMA destination is `0x0E67` = row 19, x=7. The 17-word chunk
placement is therefore confirmed. It still covers only x=7..23; margins need
the adjacent source segments.

### Same-run capture requirement

The adjacent-source test exposed a snapshot inconsistency: the BG1 row seen at
`$B3F9` is not reproducible across separate runs (one run has row 19 data,
another zero). Cross-run pre/post comparisons are invalid. Capture pre-DMA at
frame N, continue in the same process, and capture post-DMA at N+1 before
comparing adjacent source segments.

### Same-run result (pre 3504 / post 3505)

One-process capture shows the stage at `$0433` matches post-DMA BG1 row 19
x=7..23, while the frame's DMA dest is row 20 x=8 and post row 20 is unchanged.
The stage is displayed one row above the pending DMA; the transfer is offset
in time/row. A simple `stage -> dest` model is invalid. The actual transfer
that installs the stage is still the missing link for adjacent margin chunks.

### Multi-frame same-run capture

At `$81:B3F9`, DMA destination walks one map row per frame:

```text
3498 0x0F47 ... 3504 0x0E88, 3505 0x0E67, 3506 0x0E47
```

Stage content is `0004...` through 3502 and `0044...` from 3503. BG1 row 19 is
stable. At 3505 the destination is row 19 x=7; at 3504 it is row 20 x=8 (one
row ahead). This is a **ring/scroll row writer**, not a direct stage→next-row
copy. It explains the row-19 match, but not horizontal margin columns outside
0..31.

### External oracle status

The VGMaps page was consulted, but the full-map/thumbnail download returns
HTTP 403 in this environment. The external image could not be used as a pixel
oracle. Without it, there is no proof that the ±43 px margins contain real
track rather than background; the 32-column BG1 map remains the only runtime
source.

### Local line-art oracle: Zoom Zoo frame 3504

The local `maps/uniracers-crawler-zoom-zoo-snes-map.webp` is a line-art track
map (not a tile rip). Its right-hand section contains the vertical loops seen
at frame 3504. In that section the map shows a single narrow track line; there
is no adjacent parallel track in the ±43 px margins. The clamp is therefore
geometrically correct for this frame: the margins are background, not missing
track.

The line-art maps are sufficient for this kind of topology/visibility check,
but not for tile-level validation.

### Dragster correction: clamp is not universally correct

The local `maps/uniracers-crawler-dragster-snes-map.png` is a long horizontal
track strip. At the native 4:3 Dragster frame 3500 the track fills the entire
256 px width; therefore the ±43 px wide margins should continue the track
horizontally, not show background.

The clamp hides that real continuation. So:

- clamp is **geometrically correct** for vertical/loop sections where the
  margins are background (Zoom Zoo frame 3504);
- clamp is **not correct** for Dragster horizontal sections.

This does not make the clamp a production solution; it makes it a conservative
fallback. The no-go remains technical: we still cannot source the off-window
columns.

### Five-track geometric classification

Using the local line-art maps and the native/wide captures:

| Track | Section in capture | Margins should be | Clamp correct? |
|---|---|---|---|
| Dragster | horizontal track | track continues | **No** |
| Zoom Zoo | vertical loop | background | Yes |
| Bowl | horizontal track | track continues | **No** |
| Switcher | horizontal track | track continues | **No** |
| Monster | horizontal track | track continues | **No** |

The line-art maps show a continuous horizontal track on Dragster, Bowl,
Switcher and Monster; the native captures already have track spanning the full
256 px width. The clamp hides that continuation.

Therefore the clamp is **not a universal 16:9 solution**. It is correct only on
sections where the margins are genuinely background (e.g. the Zoom Zoo vertical
loop). The source-backed off-window columns are still required for the other
four tracks.

### Test-only edge-repeat experiment

Added a **test-only** switch under `work/`:

```text
SNESRECOMP_WS_TRACK_REPEAT=1
```

It copies the visible left/right BG1 edge columns into the ring columns that
the margins sample, then restores them after rasterization.

Results:

- **Dragster 3500:** track continues across the full 342 px width; visually
  matches the horizontal line-art. `frames/dragster-repeat-3500.png`.
- **Zoom Zoo 3500:** the track disappears/breaks; the edge copy overwrote map
  columns needed by the vertical curve. `frames/zoomzoo-repeat-3500.png`.

Conclusion: edge-repeat is not a general solution and must not be default. It
confirms the horizontal Dragster expectation, but it can erase real track on
curves. The source-backed off-window columns remain required.

## Handoff

For resuming without repeating experiments, see
`docs/UNIRACERS_NATIVE_MAP_HANDOFF.md`. It records the active changes, the
validated uploader facts, the failed hypotheses, the five-track classification,
and the next probe.
