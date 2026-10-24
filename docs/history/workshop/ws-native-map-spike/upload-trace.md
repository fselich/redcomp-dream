# BG1 upload trace (Zoom Zoo, frame 3504)

Date: 2026-10-21
Change: `uniracers-native-map-spike`

Run: clean SRAM, normal pacing, `route-zoomzoo-move.txt`,
`SNESRECOMP_RUN_FRAMES=3600`, debug server port 10701.

## Watchpoints

Armed at frame 1000 on `$7E:0433`, `$0455`, `$023E`, `$0236`, `$0238`, `$0339`.
Queried frames 3484..3508.

## What was observed

### Staging write

At frame 3504 the BG1 staging buffer is filled by:

```text
PC 0x81B3E2
  A = 0x4400
  X = 0x805C
  Y = 0x0433
```

`$81:B3D0` copies a 17-word/34-byte chunk from the bank `$7F` high table into
low WRAM staging at `$0433` (and a parallel buffer at `$0455`). The source
offset is carried in `$023E` and advances by `$0236` (observed `0x0002`) per
word. This is a **linear chunk**, not a generic world-column stride.

### Source is the high table

At the same frame:

```text
$7F800F + 0x5C = $_7F806B
```

contains `00 44 00 44 ...` (17 words). Staging contains the byte-swapped form
`44 00 44 00 ...`, which is what the DMA/VRAM path expects.

### It is a BG1 map row, not a column

Decoding VRAM big-endian, the staging values match exactly:

```text
BG1 map row 18, x = 7..23
  0x44 0x44 0x44 0x44 0x44 0x44 0x682 0x882 0x80 0x80 0x9c 0x80 0x80 0x80 0 0 0
```

The match is exact for 17 words. So the upload is a **horizontal map chunk**,
despite the earlier “column” wording in the archived recon.

### DMA destination

At frame 3504 the BG1 map DMA is:

```text
src 00:0433, dest 0x0E88, size 34 bytes, channel 0, breg $2118
```

The earlier frame-5000 trace had the same shape: `src 00:0433`, size 34,
destination `0x0FC4`. The destination is a word address in the BG1 map area;
it is not the start of the map and is not a full 32x32 upload. One DMA installs
one 17-word horizontal chunk.

The stage bytes match BG1 map row 18, x=7..23 exactly. The DMA destination
`0x0E88` is a different map row/column address (`0x0E88 - 0x0C00 = 0x288`,
row 20 col 8 in the nominal 0x0C00 base), so the destination is not a simple
`map_base + row*32 + x` with the nominal base. The likely explanation is a
map-base/scroll offset or a double-buffered row origin; the exact offset is
the next thing to decode.

### Stage 2 status

`$81:8B95` still writes `$00A0` to `$0230..$0242` in the sampled frames. That
buffer is a **different** stage-2 output; the BG1 map upload path is the
`$0433/$0455` staging + `$B3D0` chunk copy. The `$00A0` sentinel is therefore
not evidence that BG1 has no track.

## Consequence

The real BG1 source is the high table `$7F800F` (plus the low table for the
other buffer), and the runtime uploader copies 17-word horizontal chunks into
VRAM. A wide renderer would need to:

1. decode the high-table chunk layout (which offset owns which map row/x);
2. read the adjacent chunks for the left/right margins;
3. byte-swap and place them at the correct map coordinates.

This is materially closer than the earlier “no data” conclusion. The remaining
blocker is the chunk-to-screen mapping, not the existence of track data.

## Next

- Trace the DMA destination at frame 3504 (not just 5000) and correlate
  `$0433` chunks with `dest`.
- Record the sequence of `$023E` values and the map row/x each one produces.
- Once one row of chunks is mapped, test the adjacent chunk at `source ± 34`
  against the margin cells.

## Cross-frame correlation (frames 3490..3504)

| Frame | DMA dest | Map row/x matching stage |
|---:|---:|---|
| 3490 | `0x0FC1` | 30 / 3 |
| 3494 | `0x0FA4` | 28 / 6 |
| 3498 | `0x0F47` | 24 / 8 |
| 3500 | `0x0F08` | 22 / 8 |
| 3502 | `0x0EC8` | (no exact match) |
| 3503 | `0x0EA8` | 19 / 7 |
| 3504 | `0x0E88` | 18 / 7 |

The destination decreases by `0x20` (one map row) on most frames. The stage
chunk matches a map row **two rows higher** than the destination (e.g. frame
3504: match row 18, dest row 20), which is consistent with a scroll/double
buffer offset that is not yet decoded. The stage chunk from frame 3504 does
not appear at its destination in the frame-3505/3506 snapshots, so the DMA
point and the recorded frame are not the same instant. Do not treat the match
row as the final destination until this is resolved.

This is still evidence that the uploader and the level chunk data are real.
It does **not** justify a wide renderer yet.


## Breakpoint at the pre-DMA instant

A breakpoint at `$81:B3F9` was hit at frame 3504. At that instant:

```text
$0433 chunk:  00 44 00 44 00 44 00 44 00 44 00 44 00 86 02 86 04 86 ...
BG1 map:      exact match at row 19, x = 7..23
DMA (listed): src 00:0433, dest 0x0E88, size 34
```

With map base `0x0C00`, destination `0x0E88` is row 20, x=8. The stage chunk
matches the row **above and one column left** of the destination. In other
words, at this instant BG1 displays the previous row while the DMA is installing
the next row. The uploader is writing **one row ahead**.

This resolves the map base and the destination semantics:

```text
map word address = 0x0C00 + row*32 + x
DMA destination  = the row being installed (ahead of the visible row)
```

The `step_block` command did not advance at this breakpoint (it remained parked),
so the capture was taken at pre-DMA `$B3F9`; the DMA list still shows the pending
transfer. Do not use `step_block` here.

The remaining part for margins is horizontal: the stage chunk is 17 words, so
the current row segment is known, but the adjacent left/right chunks are still
not mapped to screen x.


## Breakpoint state values

At the `$81:B3F9` hit (frame 3504):

```text
$023E = 0x8060   source offset
$0236 = 0x0002   source increment
$0339 = 0x0433   staging pointer
stage = 00 44 00 44 ... 00 86 02 86 04 86 ...  (34 bytes)
DMA   = src 00:0433, dest 0x0E88, size 34
```

The stage chunk matches BG1 map row 19, x=7..23; destination `0x0E88` is row
20, x=8 at base `0x0C00`. The uploader is therefore one row ahead of the
displayed row.

Important caveat: the raw high table at `$7F000F + 0x8060` is **not** the stage
byte sequence (`0x5800...` vs stage `0x0044...`). `$B3D0` is not a simple
read-only copy of the current table offset, or the stage buffer was built by a
second pass. Do not assume `source = table base + $023E` without instrumenting
the actual source word sequence.

## Read-watch evidence (test build)

A test-only read watch was added to `cpu_state.c`:

```text
SNESRECOMP_READ_WATCH_FILE=/path/read.log
SNESRECOMP_READ_WATCH_FRAME_MIN=3400
```

It logs bank `$7F` byte/word reads with `A/X/Y` and the current AOT function.
The log is under `logs/readwatchregs-3504.log` (6,958 lines).

At frame 3504 the staging builder reads the high table from **multiple
non-contiguous segments**, for example:

```text
7f:806b 0000  X=805C Y=0433
7f:806c 0044  X=805C Y=0433
7f:806d 0000  X=805E Y=0435
7f:806e 0044  X=805E Y=0435
7f:87c7 0000  X=87B8 Y=0437
7f:87c8 0044  X=87B8 Y=0437
7f:87e7 0006  X=87D8 Y=043F
7f:87e8 0082  X=87D8 Y=043F
7f:8807 0000  X=87F8 Y=0447
7f:8808 009C  X=87F8 Y=0447
7f:8027 0000  X=8018 Y=044F
7f:8c33 0000  X=8C24 Y=0457
7f:81b3 0000  X=81A4 Y=045F
```

The `Y` addresses walk the staging buffer (`$0433`, `$0435`, ...); the `X`
addresses jump between high-table segments. So `$B3D0` is a **multi-segment
row assembler**, not a contiguous `table + offset` copy. Adjacent margin x
segments must be found in those segment pointers, not by adding `±34` to one
base.

This is the new blocker. The next step is to decode the pointer/row table that
feeds those `X` values (or instrument the segment-selection index), then read
the neighbouring segment for x<7 and x>23.


## Pointer sequence at frame 3504

The write trace shows the pointer (`$023E`) does not simply increment across
the whole row:

```text
805C, 805E, 8060,
87B8, 87BA, 87BC, 87BE, 87C0,
87D8, 87DA, 87DC, 87DE, 87E0,
87F8, 87FA, 87FC, 87FE, 8800,
8018, 801A, 801C, 801E,
8C24, 8C2C, 8C34, 8C3C, 8C44,
81A4, 81AC, 81B4, 81BC
```

`$0236` remains `0x0002` within a group; the jumps (`805C → 87B8`, `87C0 →
87D8`, `8800 → 8018`, `8C44 → 81A4`) come from the caller selecting another
high-table segment. The source addresses read are `pointer + 0x0F`; for
example `805C → 806B`, `87B8 → 87C7`, `87D8 → 87E7`.

The caller/segment list is the next decode target: it must supply the
left/right segment for x<7 and x>23.

## All-bank read watch and caller status

The read watch was extended to all banks for frame 3504. The high-table reads
are attributed to `interp@$82D4EE`; disassembling that address shows a tiny
`STZ $57 / BIT $57 / BPL` wait loop, not a table builder. The reads therefore
happen while the AOT function label is stale (interrupt/NMI context) or in a
queued upload path.

No read in the all-bank log carries the literal values `0x805C`, `0x87B8`,
`0x87D8`, etc. Those pointers are **computed or queued**, not loaded directly
from a visible table read. A breakpoint at `$81:B3D0` entry does not hit (it is
not a block boundary), while `$81:B3F9` does.

So the next target is the interrupt/queue path that computes the segment
pointer list, not a direct table lookup. This is still the blocker for
horizontal margin chunks.

## PB/S caveat: the function label is not reliable

The read-watch now logs `PB` and `S`. At frame 3504 the high-table reads have:

```text
PB=81  S=01EB
stack at S: 7C 8A 81  -> return address $81:8A7C
```

`$81:8A7C` is inside stage 1 (`$81:8A4A`), while `g_last_recomp_func` says
`interp@$82D4EE` (a wait loop). The label is stale, and the reads may belong to
stage 1 or a routine invoked from stage 1, not necessarily `$81:B3D0`.

So the “multi-segment staging” interpretation from the read addresses is
**not yet proven as the uploader**. Do not use it as a production model. The
next step is to separate stage-1 table reads from the `$0433` staging builder by
breakpointing `$81:B3D0`/its caller and logging the actual source sequence.

## NMI check

The read-watch now logs `NMI=` from `g_snes->inNmi`. At frame 3504 the
high-table reads (`7f:806b`, `7f:87c7`, ...) have **`NMI=0`**. They are not in
the NMI handler. They run with `PB=81`, `S=01EB`, and the host stack shows only
`interp@$82D4EE`; the guest return frame at `S` points to `$81:8A7C` (stage 1).

So the stale-label problem is not simply “NMI”. The read path is either a
reentrant IRQ/queued routine or a host stack that does not reflect the guest
call. Do not attribute the reads to `$B3D0` yet.

## IRQ / vblank check

The read-watch now logs `inNmi`, `inIrq`, `inVblank` and `vPos`. For the
frame-3504 high-table reads:

```text
7f:806b  NMI=0 IRQ=0 VB=0 V=45
7f:806b  NMI=0 IRQ=0 VB=0 V=56
```

They are **not NMI and not IRQ**, and not in vblank. They run in normal CPU
context with `PB=81`, `S=01EB`; the host stack still says
`interp@$82D4EE`. So the stale label is an AOT/inlined-call artifact, not an
interrupt. The guest return frame points at `$81:8A7C`; the next step is to
breakpoint `$81:8A7C` or otherwise separate stage 1 from the staging builder.

## Breakpoint separation result

- `$81:8A7C` **does not** hit (it is mid-block, not a block boundary).
- `$81:8A4A` (stage 1 entry) **does** hit at frame 3504.
- The frame-3504 high-table reads therefore belong to **stage 1** (or code
  called from it), not to `$81:B3D0`.

This invalidates the previous “B3D0 multi-segment staging” attribution. Stage 1
is the table reader; the `$0433` uploader is a separate path. The next trace
should instrument stage-1 exit (`$0260` segment IDs) and correlate it with the
BG1 row, rather than treating the read addresses as the staging list.

## Stage-1 output correlation (negative result)

At Zoom Zoo frame 3504, stage-1 output is:

```text
$0260: 4800 4800 4800 4800 4800 4800 4800 4800 0802 0802
```

I searched all 32×32 BG1 rows, columns, and reversed windows for that 10-word
sequence, both as-is and byte-swapped. There was **no match**. Stage 1 therefore
emits intermediate segment codes, not final BG1 tile words. It cannot be used
directly for margin columns; stage 2/the uploader still has to be decoded.

This is a negative result for the “stage 1 output = map row” hypothesis.

## Stage-2 controlled inputs

Running `$81:8B98` (stage 2 after stage 1) with a preloaded `$0260` shows the
converter is real:

```text
$0260=0001 -> blank
$0260=0400 -> blank
$0260=1C00 -> 007F...
$0260=1C04 -> FFF2...
$0260=2024 -> FD06...
$0260=4800 -> blank
$0260=7FFF -> 0001... (nonblank)
```

So `$4800` is an empty segment; stage 2 can produce nonblank tile data.

A 2D world-offset scan at frame 3504 found:

```text
oy=+64  -> 0230=007F, 0260=1C00, 9 nonblank
oy=+128 -> 0230=0001, 0260=42E4, 9 nonblank
```

In both rows the result is **invariant across ox** in the sampled range. That
means the stage-1/2 path is producing a **vertical column** keyed by Y; the
horizontal margin input is not `$00A5` alone. The horizontal axis still needs a
different key (scroll/map column), not this synthetic X scan.

### 2D scan detail

At frame 3504 the only nonblank stage-2 rows were `oy=+64` and `oy=+128`.
For `oy=+64` all `ox` values gave `0230=007F`; for `oy=+128` all gave
`0230=0001`, with only `$0262` alternating `5ACC/5AE0` — a low-bit/edge
variation, not an x-column change.

So `$00A5` is not the horizontal margin key at this frame. The horizontal
input must be separate scroll/map-column state. Stage 2 itself is confirmed to
convert valid segments; the missing piece is the horizontal key and its map
placement.

## Fine vertical and horizontal scans

Fine vertical scan (`oy` step 8) found nonblank stage-2 rows at:

```text
oy=-16, -8, 16, 24, 32, 40, 64, 72,
   80, 88, 112, 120, 128, 136, 160, 168
```

At `oy=64`, `ox` step 8 (-512..512) produced the same `0230=007F`
(`0260=1C00`) for every `ox`. At `oy=128` the only variation was the low
`$0262` edge value.

Conclusion: stage 1/2 is a **vertical tile column** driven by Y; `$00A5` is not
the horizontal margin column key in this synthetic call. Horizontal margins
must come from map-column/scroll state or the high-table chunk sequence, not
from stage-2 output alone.

## Candidate horizontal keys

At frame 3504, with `oy=+64` producing a valid row, I varied:

```text
$041D  -256..+256
$041F  -256..+256
$00A5  -512..+512
```

None changed the stage-2 result: every sample stayed at `0230=007F`,
`0260=1C00`. So neither the scroll mirrors nor `$00A5` are the horizontal
margin key for the stage-1/2 path.

The generator path is a **vertical column**; the horizontal margin must come
from the BG1 map/chunk layer or another map-column state, not from stage 1/2.

## BG1 row / high-table search

At Zoom Zoo frame 3504, BG1 row 19 is:

```text
48 44 44 44 44 44 44 44 44 44 44 44 44 86 02 86
04 86 00 80 00 9c 00 9c 00 80 00 80 00 00 00 4c
```

I searched the full 32-word row and the central 17-word segment in bank `$7F`,
normal and byte-swapped. There were **zero exact matches**. The row is not a
contiguous table slice; it is assembled from multiple table/upload segments.
So the margin columns cannot be obtained as `table + row*stride + x`; the
segment assembler/stream remains the unresolved source.

## Staging word → source mapping (pre-DMA)

At the `$81:B3F9` breakpoint, staging word `k` maps to source reads:

```text
k=0  x=7  stage=4400  source $7F806B/6C
k=1  x=8  stage=4400  source $7F806D/6E
k=2  x=9  stage=4400  source $7F87C7/C8
...
k=6  x=13 stage=8600  source $7F87E7/E8
k=10 x=17 stage=9C00  source $7F8807/08
k=14 x=21 stage=0000  source $7F8027/28
```

The source bytes are the stage words with byte order reversed. But the BG1
VRAM snapshot at that same breakpoint has row 19 **zero** (pre-DMA), so the
x→map correlation cannot be closed from that instant. The earlier post-frame
row-19 match came from a different snapshot.

This is the exact limit of the current instrumentation: source addresses for
the staged words are known; a post-DMA map capture is still needed to prove the
x placement and adjacent margin columns.

## Post-DMA placement (frame 3505)

At the `$81:B3F9` breakpoint for frame 3505 (pre-DMA for 3505, after the
previous transfer), the staging buffer is:

```text
00 44 00 44 ... 06 82 08 82 ... 00 80 00 80 ...
```

and BG1 map row 19 contains the same sequence at **x=7..23**. The frame's DMA
is:

```text
dest 0x0E67 -> 0x0E67 - 0x0C00 = 0x267 = row 19, x 7
```

So the staging chunk → destination placement is confirmed: **row 19, x=7..23**.

That confirms the placement of the current 17-word chunk, but it still covers
only x=7..23. The margins (x<7 and x>23) require the **adjacent source
segments**, which are not yet available from this chunk alone.

## Same-run capture requirement

The adjacent-source test exposed an instrumentation issue: the BG1 map row
seen in the `$B3F9` snapshot is not reproducible across separate runs. In one
snapshot row 19 contains the stage sequence; in another it is zero. Cross-run
pre/post comparisons are therefore invalid for proving adjacent columns.

The next capture must stay in **one run**:

1. break at `$B3F9` for frame N;
2. save `$0433`, DMA dest and the current map;
3. `break_continue` to `$B3F9` frame N+1;
4. save the post-DMA map;
5. only then compare the stage words and adjacent source addresses.

Do not use the current cross-run row match as production evidence.

## Same-run result (pre 3504 / post 3505)

Captured in one process without killing at the breakpoint:

```text
pre stage:      00 44 00 44 ... 00 86 02 86 04 86 ...
pre DMA:        src 00:0433, dest 0x0E88 (row 20, x 8), size 34
post map:       stage appears at row 19, x=7..23
post row 20:    unchanged (00 04 ... ec 81 ...)
```

So the stage chunk is displayed **one row above** the DMA destination, and the
destination row itself is unchanged. The DMA at frame N does not install the
stage captured at the pre-DMA breakpoint; the stage and the transfer are
offset in time/row. A simple `stage -> dest` model is invalid.

This is the current blocker for adjacent margin chunks: we need the transfer
that actually installs this stage, not the next pending DMA.

## Multi-frame same-run capture (3498..3506)

At `$81:B3F9`, the DMA destination moves one map row per frame:

```text
3498 0x0F47
3499 0x0F27
3500 0x0F08
3501 0x0EE8
3502 0x0EC8
3503 0x0EA8
3504 0x0E88
3505 0x0E67
3506 0x0E47
```

The staging content is `0004...` on 3498–3502 and `0044...` on 3503–3506.
BG1 row 19 is stable throughout (`00 48 00 44...`); at frame 3505 the DMA
destination is row 19 x=7, while at 3504 it is row 20 x=8 (one row ahead).

So the uploader is a **ring/scroll writer**: it walks successive map rows; the
visible row can already contain the same stage bytes. This explains why the
same stage appears in the post map at row 19 even when the pending DMA points
to row 20.

The map row still has only 32 columns. The multi-frame capture gives the row
walk, not the horizontal columns outside 0..31; adjacency for margins is still
open.

## External full-map oracle status

The VGMaps page metadata was consulted, but downloading the full map or
thumbnail returns HTTP 403 from this environment. The external full-map image
could not be used as a pixel oracle. Without that reference, there is no
evidence that the frame's ±43 px margins contain real track rather than
background; the 32-column BG1 map remains the only runtime source.
