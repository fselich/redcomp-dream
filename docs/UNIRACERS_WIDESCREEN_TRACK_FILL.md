# Uniracers widescreen track fill — investigation log and decision

Status: **clamp is the safe interim; full-track native fill is plausible but not implemented.** Option A (per-cell verified ring margins) is now prototyped and validated behind `SNESRECOMP_WS_OFFWINDOW=ring`; it restores the authentic trailing margin and masks the leading margin. See `docs/UNIRACERS_BG1_RING_FINDINGS.md` §8.
Date: 2026-10-20
Related changes:

- `uniracers-ws-validation` (per-track validation)
- `uniracers-ws-track-margins` (clamp)
- `uniracers-ws-track-fill` (generator investigation, archived)
- `uniracers-ws-hdma` (earlier HDMA investigation)

This document is the durable “what we tried, what we learned, and whether it is
worth going further” record for the Uniracers track-margin problem. Derived
captures, dumps and test builds live under `work/` (gitignored); do not commit
them.

> **Correction 2026-10-21 (pm) — BG1 is 16×16 in race.**
> Several statements below assume a 256×256 (8×8-tile) BG1 map. In race
> `BGMODE = 0x19`: BG1 uses 16×16 tiles, so the 32×32 ring is **512×512 px**.
> The 16:9 margins sample real ring cells: the trailing side is already correct
> world data, and only ~3 leading-edge cells per row are stale. They are not a
> "wrap of the 256-wide map" or the "opposite edge". Validated formula and
> measurements: `docs/UNIRACERS_BG1_RING_FINDINGS.md`.

> **Correction after reviewing the full-track maps.**
>
> The full maps (VGMaps: Crawler Dragster, Crawler Zoom Zoo, etc.) prove that the
> complete track data exists. The earlier conclusion “the game does not author
> track in the margins” was too strong and is superseded. The 256×256 BG1 map is
> the **runtime display window/ring**, not the whole level. The level table in
> WRAM `$7F` is much larger (one stride is `$8FA`, giving ~1,149 columns), and
> the full maps confirm long tracks with loops.
>
> The blank result from the synthetic generator scan means the **offset/world
> mapping used in that scan was wrong or incomplete**, not that the track data
> is absent. A native wide-map pipeline is therefore a real option; the clamp
> remains the safe interim until the full table/coordinate mapping is decoded.

---

## 1. TL;DR

- The 16:9 frame is 342×224 (43 px extra each side).
- BG1 is the track layer. It is a 32×32 tilemap of **16×16 tiles (512×512 px)**
  in race. *(Originally recorded here as 256×256; corrected.)*
- The old wide frame showed detached track pieces at the margins on loops and
  slopes. Those pieces are the **wrap of the 256-wide display map**; the full
  track data is elsewhere (level tables/ROM and WRAM `$7F`).
- The runtime generator (`$81:8A4A` + `$81:8B95`) produces tile entries for the
  current 256 window. Our synthetic scan returned blank at the offsets we
  tried, but that is a **coordinate/calibration artifact**, not proof that the
  track ends at 256.
- The full maps show the complete tracks: long courses, loops and slopes. The
  data exists.
- **Clamp BG1** is the safe interim: it hides the wrap and keeps the authentic
  current window. It is not the final answer for a true wide-map pipeline.
- A native/full-map fill is plausible. It needs the full level-table decoding
  and world-coordinate mapping; it is a real project, not an invention.

---

## 2. Scope and track identity

1P is full-screen against a CPU ghost. 2P/VS are split-screen and out of scope.

Track identity was corrected during the work:

- **Dragster is the first track.**
- **Zoom Zoo is the second track.**
- `CRAWLER` on the PICK TRACK screen is the league/heading, not a track.
- The five 1P tracks are Dragster, Zoom Zoo, Bowl, Switcher, Monster.

The user-provided full maps (VGMaps) confirm:

- Dragster is almost entirely flat.
- Zoom Zoo contains loops and slopes.

This explains why the margin bug was invisible in early Dragster captures and
obvious on Zoom Zoo loops. Early reports that called Dragster “second track”
were wrong and were corrected in the validation/fill changes.

---

## 3. Symptom

On Zoom Zoo loops/slopes, e.g. after holding `right`:

- Native 256×224: continuous S-curve / loop.
- Wide 342×224: the main track plus **disconnected blue/green/yellow track
  pieces** at one or both margins.
- `SNESRECOMP_LAYER_MASK=0x10` (OBJ only): no track content; only the rider.
- `SNESRECOMP_LAYER_MASK=0x01` (BG1 only): main track **and** the detached
  pieces, proving the track layer is BG1.
- PPU state at frame 5000: BG mode 1, BG1 `hScroll` 73/776 depending on run,
  `bgXsc[0]=0x0C` → 32×32 tilemap (256×256).

Artifacts (work):

- `work/ws-validation/frames/curve/` — native/wide/BG1 loop captures.
- `work/ws-validation/frames/input/` and `input-native/` — right-held loop.
- `work/ws-track-fill/frames/` — clamp/fill experiments.

---

## 4. Initial assumptions and corrections

### 4.1 “The curves are HDMA per-line scroll”

Phase E3 investigated the HDMA tables and found the enabled BG-scroll channels
had empty tables on Dragster. That led to P-U1 being marked not applicable.
This was true for the tested flat section, but it did not explain Zoom Zoo
loops: the slope there is tilemap art, not HDMA. The later `ppu_lines` at frame
5000 showed constant per-line H/V scroll.

### 4.2 “Natural wide sampling is correct”

The early background work saw the BG margins as natural tilemap samples. That
was measured on Dragster/early frames. It missed the 256-wide map wrap on loops;
the wide margins were natural samples of the wrapped map, i.e. wrong content.

### 4.3 “Zoom Zoo is the second track / Dragster is the second track”

The selection screenshots and the route were misread at different times. The
final, confirmed mapping is Dragster first, Zoom Zoo second. This matters
because the N/A spawn conclusions and HUD measurements are track-scoped.

### 4.4 “A plain tilemap copy can prefill the margins”

The copy loop at `$81:B3D0` looked like a column copy. It is generic; it copies
a 16-bit table from WRAM `$7F` using a stride, but the table is **track
geometry/segment data**, and the visible tilemap is generated from it. A raw
copy would produce wrong geometry.

---

## 5. Experiments in order

### 5.1 Clamp

The first safe fix was:

```c
PpuSetWidescreenLayerClamp(ppu, 1u << 0);   /* BG1 */
```

Result:

- Detached margin pieces disappear.
- The track remains native 256, centered; margins show background.
- HUD/cull continue independently.
- 4:3 is unchanged.

This is the accepted production behavior. `SNESRECOMP_WS_TRACK=clamp` is the
default; `off` reproduces the old wrap for comparison.

### 5.2 Shadow history/edge fill (`SNESRECOMP_WS_TRACK=fill`)

The framework has a world-keyed `ws_shadow` system:

- `WsShadowSetWorld(0, worldX, worldY)`
- `WsShadowSetScroll(0, ppu_h, ppu_v)`
- `WsShadowSetRetainHistory(0, true)`
- `WsShadowSetWestKeep/EastKeep(0, 8)`
- `WsShadowSetCaptureCols(0, 32)`
- `WsShadowSetRejectEastEcho(0, true)`
- `WsShadowFrame(ppu)` then `WsShadowExtendEdges(0, 43)`

Results:

- Frame 4000: often continuous, promising.
- Frame 5000: still disconnected pieces; edge extension can copy a track-edge
  column into a separate horizontal segment.
- The ring has no true leading-edge columns, so history/edge fill cannot
  invent them. *(Correction: the trailing edge is already authentic. Re-check
  this experiment with 16×16 keys, `tileShift = 4`.)*

This path is **experimental**, not accepted. It is useful for other games with
a wider authored map or retained strip, but not for this one.

### 5.3 Decode the source tables

At frame 5000 the game streams map columns from low WRAM to VRAM `$2118`.
A write-watch on the staging buffer found writer PC `$81:B3E2` inside
`$81:B3D0`:

```asm
LDA $7F000F,X    ; 16-bit source table
STA $0000,Y      ; low-WRAM staging
INY
INY
LDA $023E
CLC
ADC $0236        ; row stride
STA $023E
TAX
DEC $76
BMI ...
JMP $B3DE
```

At frame 5000:

| WRAM | Value |
|---|---|
| `$023E` source pointer | `0x801` |
| `$0236` row stride | `0x8FA` |
| `$0339` destination | `0x475` |
| `$00A5` world X | `0x2774` |
| `$00A7` world Y | `0x06CF` |
| `$04F1` | `0x0100` |
| `$04F3` | `0x0200` |
| `$0FED` | `0` |
| `$0FEF` | `2` |
| `$0260` segment ID | `0x4800` |
| `$0230` tile entry | `0x00A0` |

The source table is in WRAM bank `$7F`; the visible tilemap is generated from
it, not stored as a simple map.

### 5.4 Stage 1 — segment lookup (`$81:8A4A`)

`$81:8A4A` reads `$7F000F,X` and `$7F800F,X`, uses tile coordinates
(`$00A5/$00A7`), strides (`$04F1/$04F3`) and edge masks (`& 0x30`), and writes
segment IDs to `$0260..$0272`.

It computes values such as `$0232/$0234/$0236/$0238` and uses a 9-entry loop.
This is track geometry, not the final tilemap.

### 5.5 Stage 2 — tile lookup (`$81:8B95`)

`$81:8B95` calls stage 1, then for `Y = $12, $10, … 0`:

- reads `$0260,Y` (segment ID),
- indexes `$7E:C000,X` and `$7E:A000,X`,
- writes tile entries `$0230,Y/$0231,Y`,
- handles edge masks and blank tiles (`$00A0`).

So the full pipeline is:

```text
$7F000F / $7F800F
        -> $81:8A4A (segment IDs $0260..)
        -> $81:8B95 (tiles $0230/$0231 via $7E:C000/$7E:A000)
        -> BG1 column in VRAM
```

### 5.6 Synthetic generator call from C

Instead of porting every branch, a test harness calls the **actual guest
generator** and restores state:

- `SNESRECOMP_WS_TEST_GEN=1`
- save `CpuState` + all `g_ram`;
- offset `$00A5`;
- `cpu_dispatch_call_pc(&g_cpu, 0x818B95, 0x818B95)`;
- read `$0230..$0242`;
- restore `CpuState` + `g_ram`.

The call returns cleanly (`ret=0`) and produces different tile columns for
different offsets. This is a viable validation/prefill technique (P11-style
synthetic call), but it is not production code yet.

### 5.7 Margin scan — the decisive result

Offset units are relative to `$00A5`; the native half-width is about ±1024 in
these units (8 units per pixel).

At frame 5000, scanning offsets:

```text
off = -1408 .. -1024 : 00a0 ... 00a0   (blank)
off = -192  ..   64  : track entries   (native area)
off =  544  ..  896  : track entries   (native area / loop)
off =  1024 .. 1408  : 00a0 ... 00a0   (blank)
```

The true generator does **not** write track in the margin offsets. The
disconnected wide pieces are a wrapped copy of other 256-map columns.

> *Superseded:* the "8 units per pixel" calibration and the 256-map wrap
> assumed 8×8 tiles. The wide pieces are stale leading-edge ring cells. See
> `docs/UNIRACERS_BG1_RING_FINDINGS.md` §4.

This is the key evidence: a source-backed fill would write blank margin
columns, i.e. the same result as the clamp.

---

## 6. Why clamp is the safe interim

- BG1’s runtime map is a 512-px ring (16×16 tiles; originally recorded as
  256-wide); the full level table is larger.
- The wrap pieces are not the margin continuation; they are the window’s
  opposite edge.
- Clamping hides the wrap, keeps the authentic current window, and leaves the
  background wide.
- No simulation, camera, scroll, collision or RNG changes.
- It is an interim because the full level data exists and a native/wider map
  pipeline could use it.

Do not describe the clamp as “the game has no track data”; describe it as “the
runtime window is native, the full track table is still to be mapped.”

---

## 7. Decision matrix

| Option | Result | Cost | Recommendation |
|---|---|---|---|
| Clamp BG1 | authentic track, background margins, no wrap | tiny | **adopted** |
| `ws_shadow` history/edge fill | partial; artifacts on loops | medium | experimental |
| Port stage 1+2 to C | large; margins still blank | high | not useful without new data |
| Synthetic guest calls for fill | works for validation; complex per-frame save/restore; blank margins | high | only as a test harness |
| Extend/invent track data | non-authentic | high + licensing | reject |

If a future goal is **artistic** wide-track coverage rather than authenticity,
that is a different project: it needs authored/upscaled track art, not the
guest generator.

---

## 8. If someone wants to continue anyway

Minimum work:

1. Finish offset → screen-X calibration (the scan suggests 8 units/pixel; verify
   with a VRAM/native-map comparison).
2. Map the 10 generated tile rows (`$0230`) to screen Y/tilemap rows.
3. Decide where to store the margin tiles:
   - `ws_shadow` prefill, or
   - a host-side widened BG map, or
   - a synthetic guest call each frame.
4. Prove 4:3 and simulation unchanged.
5. Benchmark 12 synthetic calls/frame (or the ported generator).

Risks:

- Calling guest code during draw can corrupt state if not fully saved/restored.
- The generator may depend on state not saved by a partial snapshot.
- Performance and frame pacing.
- Any fill still writes blank margins because the source has no data.
- Inventing track beyond native is out of scope for authenticity.

Success criteria if continuing:

- Margin columns match the generator’s output at the same world offset.
- No wrap fragment and no duplicate.
- 4:3 bit-identical.
- No guest camera/DMA/RNG changes.

---

## 9. Reproduce

Build (work project):

```sh
cd work/ws-track-fill/project/UniracersSNESRecomp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSNESRECOMP_ENABLE_TRACE=ON
cmake --build build --parallel
```

Run with clamp (default):

```sh
cd build
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_NO_LAUNCHER=1 \
SNESRECOMP_WS=1 SNESRECOMP_WS_TRACK=clamp \
SNESRECOMP_RUN_FRAMES=5200 SNESRECOMP_DEBUG_PORT=9999 \
./UniracersSNESRecomp --rom "/path/Uniracers (USA).sfc" --no-launcher \
  --script ../../scripts/route-stoptrack.txt
```

`SNESRECOMP_WS_TRACK=off` reproduces the old wrap; `fill` runs the experimental
shadow path.

Synthetic generator scan:

```sh
SNESRECOMP_WS_TEST_GEN=1 ... ./UniracersSNESRecomp ...
# stderr lines: [wsgen] off=... 0260: ... 0230: ...
```

Useful debug commands (trace build):

- `oam_state`
- `ppu_lines 0 223`
- `get_ppu_state`
- `ppu_dmas count=256 frame=N`
- `dump_vram`
- `dump_frame_wram N 0 1000` / `dump_frame_wram N 10000 10000`
- `set_wram_watch 7E 0433 1`
- `wram_watch_log_get all 4900 5005 1024`
- `interp_edges to=0x818A4A n=64`

Route: `work/ws-track-fill/scripts/route-stoptrack.txt`; select Zoom Zoo with
`Down` on PICK TRACK, start the race, then hold `right` to reach loops.

---

## 10. Artifacts

Work-only (gitignored):

> These artifacts were deleted on 2026-10-21 to keep only the active work
> directories. The conclusions are preserved in this document and in the
> archived OpenSpec changes below.

- `work/ws-track-fill/stream-recon.md` — raw recon notes.
- `work/ws-track-fill/logs/` — DMA/WRAM/VRAM/scans.
- `work/ws-track-fill/frames/` — clamp/fill/native captures.
- `work/ws-validation/frames/curve/` and `input/` — loop evidence.
- `work/ws-hud/` — HUD measurements and the top-band policy.
- `work/ws-track-margins/` — clamp implementation and first fill experiment.

OpenSpec:

- `openspec/changes/archive/2026-10-20-uniracers-ws-track-margins/`
- `openspec/changes/archive/2026-10-20-uniracers-ws-track-fill/`
- `openspec/specs/recomp-ws-track-margins/spec.md`
- `openspec/specs/recomp-ws-track-fill/spec.md`

External reference maps (do not commit the images):

- `https://vgmaps.de/maps/snes/uniracers`
- “Crawler Zoom Zoo” map (`m=13404`) and “Crawler Dragster” (`m=13403`).

---

## 11. Lessons

- A flat first track can hide a margin bug; validate loops/slopes explicitly.
- “Natural wide sampling” is only correct if the layer is authored beyond 256.
- A 32×32 BG map wraps; the extra margins must not consume the wrap.
- A generic copy loop is not evidence of a tilemap source; trace what feeds it.
- The framework’s `ws_shadow` is the right tool for retained history, not for
  inventing missing leading-edge columns.
- Clamping a native-by-design layer is a correct result, not a failure.
- Track-scoped conclusions (spawn, HUD, N/A) must name the track; Dragster ≠
  Zoom Zoo.

---

## 12. Open questions (if someone revisits)

- Do any later tracks author track content beyond 256 columns? If so, the
  generator scan and clamp switch can be reused.
- Is there a hidden wider level strip in ROM that the game never displays?
  (Not found in this pass.)
- Can the 10 generated tile rows be mapped to screen Y without a full port?
- Would an artistic wide-track mode be acceptable as a separate, non-authentic
  feature?

Until those are answered with new evidence, **keep the clamp**.

---

## 13. Native-map spike correction (2026-10-21)

A follow-up spike (`uniracers-native-map-spike`) re-checked the “no data”
conclusion and found it was wrong.

### What is real

- Two 16-bit tables exist at `$7F000F` (low) and `$7F800F` (high).
- Their shape is set by `$04F1` (column count) and `$04F3` (row stride):
  - Dragster frame 2500: 32×1024 (`$0400` columns, `$0800` stride).
  - Zoom Zoo frame 3504: 32×256 (`$0100` columns, `$0200` stride).
- The tables contain thousands of nonzero cells, including loopy Zoom Zoo
  sections. The level is not limited to 256 columns.
- Calling stage 1 (`$81:8A4A`) at `$00A5/$00A7` returns real table words and
  segment/high words.

### What is still unresolved

- Stage 2 (`$81:8B95`) emitted `$00A0` for all sampled offsets. That sentinel
  is not proof of missing data; the stage-1 → stage-2 → BG1-word conversion is
  not decoded yet.
- BG1 VRAM must be decoded **big-endian**. *(Corrected: little-endian; see `docs/UNIRACERS_TRACK_FORMAT.md`.)* The visible 32×32 map is rebuilt in
  screen space as the camera moves. A world-keyed cache by `$00A5` produced
  ~93% column conflicts on Zoom Zoo, so VRAM stitching is not a valid source
  for unseen margins.

### Determinism warning

- Keep normal frame pacing. `DisableFrameDelay=1` is not valid for
  deterministic captures.
- Remove `build/saves/save.srm` and `save.srm.bak` before each deterministic
  run. Persistent SRAM makes two runs start from different game state.
- With pacing on and clean SRAM, two native Dragster runs at frame 2500 were
  byte-identical (SHA-256
  `3417f91afc8f6d47ea4ab3f7b11bbeec4cc7491a23ab71cc6c956f5fb3f5df44`).

### Current decision

The clamp remains the validated 16:9 BG1 behavior. The next work is to trace
the stage-2 conversion and the VRAM column upload, not to invent margin art.

### Follow-up: the real BG1 upload path

The stage-2 `$00A0` sentinel is not the BG1 source. At Zoom Zoo frame 3504:

- `$81:B3E2` copies a 17-word chunk from the bank `$7F` high table
  (`$7F800F + 0x5C`) into `$0433`.
- The chunk byte-swaps to BG1 map row 18, x=7..23 exactly.
- DMA `src 00:0433, dest 0x0E88, size 34` installs that chunk through `$2118`.

So the map is uploaded as **17-word horizontal chunks**, not as whole columns.
The remaining problem is chunk-to-screen mapping and the correct map-base /
scroll offset; the data and the uploader are real. The clamp remains until a
chunk-level margin prototype is validated.

See `work/ws-native-map-spike/upload-trace.md`.

Cross-frame upload trace (Zoom Zoo 3490–3504): the DMA destination steps down
one map row (`0x0FC1` → `0x0E88`), while the 17-word stage chunk matches a map
row two rows higher. That is consistent with a scroll/double-buffer offset, not
yet decoded. The stage data is real; the chunk-to-screen transform is the only
remaining blocker.

Full spike record, including determinism rules and the real DMA upload path:
`docs/UNIRACERS_NATIVE_MAP_SPIKE.md`.
