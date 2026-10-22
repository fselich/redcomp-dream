# Uniracers widescreen spawn/staging (Phase E5) — report

Date: 2026-10-20
Change: `uniracers-ws-spawn`
Outcome: **P9–P13 not applicable in 1P.** The 1P Dragster engine has no
camera-spawned object records and no discrete X-axis stage/CHR trigger to
widen. Track furniture is BG; the only OAM objects are the player and CPU
ghost. No spawn or staging code was changed.

## Scope

1P is full-screen against a CPU ghost. 2P/VS are split-screen and out of scope.
The route selects **Dragster**, the second entry in PICK TRACK; its road runs
left.

## Evidence

### OAM has no spawn system

- 128 render snapshots (frames 5287–5414) always have exactly five non-parked
  slots: 0, 96, 97, 98, 99.
- Slots 98/99 are player/CPU ghost; 96/97 are the parked 2P pair; 0 is parked.
- OAM write events are only slots 96–99 from `$82:AD5D`/`$82:AE1C`.
- No new slot, allocation or record appears during the race.
- The CPU ghost is present from the first race frame; it is not spawned by the
  camera.

### Track objects are BG

Frame 3500 with `SNESRECOMP_LAYER_MASK`:

- **OBJ only (`0x10`)**: one rider sprite at screen centre; no road, arrows or
  track furniture (`frames/layers/obj3500.png`).
- **BG only (`0x0f`)**: road, checkered kerb, blue arrows, `PULL A STUNT` text
  and purple trail; no rider (`frames/layers/bg3500.png`).

The arrows and track content are BG tilemap, not spawned object records.

### No stage/CHR swap

At frame 3500 the PPU DMA list has 26 transfers, all 32 bytes, all to `$2118`
VRAM tilemap destinations (`0x6030`…`0x6cc0`). There is no large CHR block
transfer and no discrete section-swap trigger. The background is column-
streamed, which is the path already covered by the margin work.

### Rider routine is rider-only

`$82:ACA5` updates player/ghost X/Y and cull state. It has no record walk,
spawn-anchor compare or activation-distance check. The five horizontal
`CMP $0421/$0423` sites are rider culls, not spawn gates.

## Decision

| Doctrine | Result |
|---|---|
| P9 ordinary spawn anchors | **N/A** — no camera-spawned records |
| P10 dual pass / progression | **N/A** — no second record walk |
| P11 balanced synthetic call | **N/A** — no native pass to synthesize |
| P12 large-object activation | **N/A** — no large spawned objects |
| P13 X-axis stage lead | **N/A** — no discrete CHR/tilemap stage trigger |

This is a positive result: the correct action is **not** to invent spawn logic.
The wide margins already sample the column-streamed BG; P7/P8 cover the only
OAM objects.

## 4:3 and simulation

No source change was made for this phase. Native 256×224 frames at 1740 and
3500 were captured and hashed (`metrics/native-baseline-hashes.txt`); the
existing Phase E4 4:3 baseline remains the reference. RNG, progression,
collision, camera and input are untouched.

## Remaining gaps

- 2P/VS split-screen is out of scope.
- A future mode or track with real spawned enemies would need a new change;
  this N/A is for the 1P engine observed here.

## Artifacts

- `work/ws-spawn/spawn-path.md` — recon path and decision.
- `work/ws-spawn/frames/native/` — native 1740/3500 captures.
- `work/ws-spawn/frames/layers/` — OBJ-only and BG-only frame 3500.
- `work/ws-spawn/metrics/` — hashes and OAM summary.
- `work/ws-spawn/logs/` — WRAM dumps, OAM trace, DMA list.
- `work/ws-spawn/patches/` — empty; no spawn/stage patch was made.

All derived output is under the gitignored `work/ws-spawn/`; no generated C,
frames or ROM data are in the repository.
