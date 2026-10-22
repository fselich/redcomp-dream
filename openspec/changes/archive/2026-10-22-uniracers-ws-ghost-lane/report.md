# uniracers-ws-ghost-lane — report

Date: 2026-10-21
Status: **go** for the test switch `SNESRECOMP_WS_OFFWINDOW=ghost`.

## 1. Summary

The ghost lane is implemented and validated:

- **0 wrong served cells** on Dragster →, Dragster ←, Zoom Zoo, Bowl, Switcher
  and Monster, and on a frequent-direction-change route.
- **Real lane unchanged:** the 2,500-frame `SNESRECOMP_STATE_TRACE` is
  byte-identical to `ring`; frame-2500 WRAM/VRAM/CPU/DMA dumps are identical;
  native 4:3 is `3417f91a…df44`; the wide capture differs from `ring` only in
  the **leading margin** (560 pixels at Dragster 2500, 0 of them in the native
  window).
- **Leading coverage:** Dragster → served share goes from 16.0 % (`ring`) to
  **99.84 %** (`ghost`); Dragster ← from 44.3 % to **100 %**. Bowl/Switcher/
  Monster improve substantially; Zoom Zoo barely changes, as the design
  predicted for a vertical loop.
- **Cost:** ≈+2.3 ms/frame mean (end-to-end +1.9–2.9 ms) over `ring` on 2,500 Dragster frames (process CPU
  seconds). `hook_ms` mean 2.33 ms, p99 9.6 ms, one 60.9 ms first-activation
  spike. Within the 5 ms CPU budget; the headless wall rate is ~42 fps (see
  the caveat in §6).
- Actual rollback snapshot size is **324,175 bytes** (two blobs ≈ 648 KB plus
  a ~50 KB raster-state copy), not the 1,067,148 bytes measured with the
  generic run-ahead.

## 2. Method

Mode `SNESRECOMP_WS_OFFWINDOW=ghost` (value 3) implies all Option A behaviour
plus a speculative machine ahead of the real one.

At each real frame, after `RtlRunFrame` and before the real draw:

1. Save the real rollback blob and the host render state.
2. If the ghost is valid, load its blob; otherwise start from the real state
   (`resync`).
3. Run `SNESRECOMP_WS_GHOST_AHEAD` (default 4) frames ahead, at most
   `SNESRECOMP_WS_GHOST_MAXSTEPS` (default 2) per real frame, with the real
   packed input.
4. Rasterize each ghost field with a ghost-only line hook that fills
   `s_gh[32][32]` with `{world_x, world_y, map_word}` for its native view.
5. Save the ghost blob; load the real blob; restore host state; then the real
   field is drawn.

The real margin hook serves, in order:

1. an Option A-verified ring cell;
2. a ghost cell only when `s_gh[cy][cx].world ==` the needed world cell;
3. otherwise the transparent mask, as in `ring`.

Resync on packed-input change, Option A scene reset (map base / BGMODE), lane
eligibility change, and every `SNESRECOMP_WS_GHOST_RESYNC` frames. The periodic
default was raised from 60 to **600** after measurement: 60 produced a 25 ms
p99 from resync snapshots; 600 gives a 9.6 ms p99 with equal or better
coverage.

### 2.1 Character-data gate

BG1 CHR is **not** static from boot. The hash of the `PPU_bgTileAdr(ppu,0)`
region changes at race entry:

| Run | Hash changes | Stable through scored window |
|---|---|---|
| Dragster | frames 1381, 1384–1387, 1430 | yes (2300–2500) |
| Zoom Zoo | frames 1404, 1407–1410, 1503, 1510 | yes (3300–3505) |

A ghost map word can point at a character the real VRAM has not received yet.
The lane therefore tracks the real CHR hash every eligible frame and only sets
`s_gh_valid` after **30 consecutive stable frames**. The hash trace is optional
via `SNESRECOMP_WS_GHOST_CHR_LOG=<path>`. This is a correction to the design's
"check first" precondition: the check is required, not just advisory.

## 3. Framework corrections found during implementation

Three pieces of host state outside the guest blob were missing from the design:

| State | Why it matters | Fix |
|---|---|---|
| Raster journal (`s_field[2]`, cursors, OAM port) | the real draw replays it; a ghost run overwrites it | `ppu_rasterStateSize/Save/Load`, saved before and restored after the lane |
| Whole `Ppu` struct host fields (`mosaicModulo`, brightness caches, `frameOverscan`, `frameInterlace`, render flags/pointers, ports, ...) | the real draw uses them; the guest PPU saveload does not restore them | save/restore the whole `Ppu` struct around the lane |
| Beam exec `s_resume_pc` / `s_wai_halted` | not in the rollback snapshot | `snes_beam_frame_driver_get_exec/set_exec` |

Without the raster-journal and `Ppu` host-state save/restore, the real native
image was badly wrong (a large misplaced/missing background region), while the
CPU/WRAM state trace was still identical. With K=0 (no ghost steps) the image
matched `ring` exactly, which isolated the corruption to the lane.

Correction to the design: `snes_frame_counter` **is** in the current rollback
residue (v7), so the real blob already restores it. The lane still sets the
probe sentinel around ghost runs/draws to suppress probe rows, and the real
blob restores the real counter.

## 4. Invariants

| Check | Result |
|---|---|
| `SNESRECOMP_STATE_TRACE`, ring vs ghost, 2,500 Dragster frames | **identical** |
| Frame 2500 WRAM/VRAM/CPU/DMA dumps | **identical** |
| Native 4:3 with the switch set, widescreen off | `3417f91afc8f6d47ea4ab3f7b11bbeec4cc7491a23ab71cc6c956f5fb3f5df44` |
| Native 256 window, ghost vs ring at 2500 | **0 differing pixels** |
| Wide ghost vs ring at 2500 | 560 differing pixels, **all in the leading (east) margin** |
| Probe native conflicts, all runs | 0 |

The PPU struct copy is deliberately broad: it restores every host rendering
field, not just the guest-visible ones. It is cheap relative to the snapshot.

## 5. Coverage

Leading side (the side the camera moves toward). "Served" counts verifiable
spans that were drawn (correct); "masked" stays background. Wrong is 0 in all
runs.

| Track / window | Ring served / masked | Ghost served / masked | Ghost served share |
|---|---:|---:|---:|
| Dragster → 2300–2500 | 22,624 / 118,720 | **139,776 / 224** | **99.84 %** |
| Zoom Zoo 3300–3505 | 70,860 / 46,518 | 72,364 / 44,871 | 61.7 % |
| Bowl 2000–2300 | 150,915 / 62,369 | 203,406 / 9,639 | 95.5 % |
| Switcher 2000–2300 | 41,714 / 185,248 | 187,344 / 37,693 | 83.2 % |
| Monster 2000–2300 | 49,382 / 190,848 | 152,853 / 85,377 | 64.2 % |
| Dragster ← 3300–3600 (west leading) | 91,392 / 114,912 | **205,184 / 0** | **100 %** |

The ghost also fills some *trailing* cells that Option A had not verified
(e.g., Bowl west masked falls from 109,275 to 47,001; Dragster left east from
34,048 to 9,408). Those are still exact-world-match cells and scored 0 wrong.

Frequent input changes (`route-dragster-bounce.txt`, 12 left/right reversals in
the probe window) give 0 wrong; the leading served share drops to 86.5 % as the
lane resyncs on each change.

## 6. Cost

Reference: Dragster, 2,500 frames, headless, clean SRAM.

| Run | CPU seconds | Extra vs ring |
|---|---:|---:|
| ring | 15.224 | — |
| ghost (final) | 20.026 | **+1.92 ms/frame** |
| ghost (trace + timing run) | 21.683 | +2.58 ms/frame |

`SNESRECOMP_FRAME_TIMING` over 2,500 frames:

```text
hook_ms: mean 2.326   p99 9.611   max 60.9
```

The maximum is a single first-activation/resync frame; steady-state p99 is
below one frame. `SNESRECOMP_WS_GHOST_MAXSTEPS=1` lowers CPU to +1.65 ms/frame
but does not remove the resync p99; the default stays at 2 with a 600-frame
resync period.

**Wall-clock caveat.** Headless, 2,500 Dragster frames took 59.9 s in `ghost`
(41.7 fps) against 41.7 s in `ring` (60 fps); Zoom Zoo was 38.2 fps. The CPU
budget passes, but the test build is **not a 60 fps default** on this machine.
The lane is correct; the remaining speed work is **not** the ghost raster.
With `SNESRECOMP_WS_GHOST_AHEAD=0` the lane still runs its save/restore
machinery and is still ~42 fps: `guest_ms` 10.97 vs `ring` 4.04, while
`hook_ms` is only 0.069. The overhead is the lane's snapshot/copy traffic and
its effect on the real guest (cache/APU), not `ppu_runLine`. The design's D7.3
journal-walk optimisation would not recover 60 fps; a lighter snapshot/restore
strategy is the real next step. Until then use `ring` for smooth play and
`ghost` for validation. This is why the recommendation below is "promote after
the speed work", not "flip the default now".

## 7. Pixel/tile check

`scripts/pixel_check.py` compared a ghost-served margin 16×16 block at Dragster
2500 (tile word `0x0400`) with the same world cell's native block at 2506:
15 of 256 RGB pixels differ, all palette shades of the same background pattern
(e.g. `84a5a5` vs `4a6b6b`); geometry and transparency match. The tile word is
identical and the BG1 CHR region is static after warm-up, so the rendered tile
content is the same. Exact RGB across frames is not a valid invariant when the
palette animates; the map-word + CHR check is.

The visual capture `frames/ring-vs-ghost-final-2500.png` shows the intended
result: the leading margin continues the track/background, the native region
is unchanged.

## 8. Decision

**Ghost: go** for the test switch.

- `ghost` is correct and is the better picture, but it is ~42 fps in this test
  build (see the wall-clock caveat). Recommend it for validation now; promote
  it as the default only after the D7.3 speed work, or keep `ring` for smooth
  play. Every served cell is exact-world-match and the leading margin gains
  real track on horizontal sections.
- `ring` remains the safe fallback; clamp remains the default in this change.
- Not tested: real-time audio glitch/duplication (task 5.4). The lane uses the
  same speculative/audio-rewind pattern as the framework run-ahead; no ghost
  samples should reach the consumer. A real-time listen is the open item.
- Netplay, 2P and framework run-ahead are refused by eligibility.

## 9. Artifacts and reproduce

Work artifacts (gitignored, ROM-derived): `work/ws-ghost-lane/`.

- `logs/final-<track>-<window>.csv` — probe
- `logs/final-<track>-mask.csv` — visit log
- `logs/final-ghost.trace`, `logs/ring-dragster.trace`
- `logs/chr-dragster.csv`, `logs/chr-zoomzoo.csv`
- `logs/final-timing.csv`
- `frames/ring-vs-ghost-final-2500.png`, `frames/final-*.ppm`
- `dumps-ghost/` (frame-2500 state dumps)
- `patches/ws-ghost-lane.diff`, `patches/beam-exec.diff`
- `scripts/pixel_check.py`, `scripts/route-dragster-bounce.txt`

Reproduce:

```sh
P="$HOME/Seafile/Personal Projects/unirally"
cd "$P/work/ws-native-map-spike/project/UniracersSNESRecomp/build-local"
rm -f saves/save.srm saves/save.srm.bak
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_NO_LAUNCHER=1 \
SNESRECOMP_RUN_FRAMES=2500 SNESRECOMP_WS=1 \
SNESRECOMP_WS_OFFWINDOW=ghost \
SNESRECOMP_STATE_TRACE="$P/work/ws-ghost-lane/logs/final-ghost.trace" \
./UniracersSNESRecomp --rom "$P/Uniracers (USA).sfc" --no-launcher \
  --script "$P/work/ws-native-map-spike/scripts/route-dragster-move.txt"
```

## Closure (2026-10-22)

Archived as an experimental reference, not as a promoted feature. The
real-time audio check (task 5.4) was waived by decision and was not tested;
the lane rewinds the audio producer cursor and no ghost samples should reach
the consumer. `ghost` remains selectable with
`SNESRECOMP_WS_OFFWINDOW=ghost`, but `source` is the production default and the
lane is not promoted because of its cost.
