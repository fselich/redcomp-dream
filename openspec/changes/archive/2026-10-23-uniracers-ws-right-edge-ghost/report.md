# uniracers-ws-right-edge-ghost — report

Date: 2026-10-22
Status: **done, go**. Parked rider entries can no longer draw in either margin
at the 512 px cap. Confirmed by playtest: "ya se ve bien".

## 1. Symptom

On an ultrawide display a small fragment of a rider appears at the far right
edge, at the rider's row, looking like a reflected part of the current
character. The user's screenshot is 512 px wide (`E = 128`), the width every
aspect from 21:9 up resolves to. It is intermittent because it depends on the
game's OAM parking values, not on the player's position.

## 2. Root cause

`PpuDecodeOamX` maps the 9-bit OAM X into `[E - 256, 256 + E)`. At `E = 128`
the wrap threshold is `384`, so a high-bit X below `128` decodes to `+256..+383`
instead of wrapping to `-256..-129`. Two parked families hit this:

### 2.1 Parked 2P pair (the visible fragment)

In a 1P race slots 96/97 are the parked 2P pair. The values are stable across
every gameplay OAM dump:

| Slot | Raw X | Y | High bit | Decoded world X | Image X | Image rows |
|---:|---:|---:|---:|---:|---:|---:|
| 96 | `$65` (101) | `$20` (32) | 1 | `+357` | 485 | ~40–43 |
| 97 | `$60` (96) | `$60` (96) | 1 | `+352` | 480 | ~104–106 |

The 9-bit value is the same as a negative X of `-155`/`-160`, which 4:3 wraps
off-screen. The widened renderer instead places it in the right margin. Slot
97 (`Y=$60`) is the fragment aligned with the rider in the screenshot; slot 96
is the smaller one higher up. The pair has no legitimate 1P position.

### 2.2 Culled-rider sentinel (rarer)

Slots 98/99 are player/rival. A culled rider is parked at `X=$170`, `Y=$70`,
large, high bit set. At `E=128` that raw value is `368`, below the `384`
threshold, so it lands at `+368` (image X 496) instead of `-144`. The old
guard kept it when the rider's WRAM screen position was within 4 px of
`(-144, 112)`, exactly the parked case that produced the sliver.

## 3. Fix

`game/src/game_rtl.c`, `WsFixRiderOamXHigh`:

- D1: the `X=$170`/`Y=$70` sentinel is now **always** moved below the field
  (`Y=$E0`) and its X high bit cleared in the PPU copy. The WRAM keep
  exception is gone.
- D1b: slots 96/97 are moved below the field whenever their X high bit is set
  (the negative/parked form). A positive-X entry in those slots is not touched.
- Both paths only write `ppu->oam` / `ppu->highOam`. Guest WRAM is never
  written. The real-rider high-bit path is unchanged and still runs after the
  parked checks, so a rider at true `+368` is not hidden.

Switches:

- `SNESRECOMP_WS_OAM_XHIGH=0` disables the high-bit fix and both parked-slot
  hiding, restoring the pre-change frames byte-for-byte.
- `SNESRECOMP_WS_PARK_LOG=<path>` writes
  `frame,slot,lo,y,hi,rel,rel_y,E,action` for all 128 slots:
  `candidate`/`hide` (sentinel), `right` (any high-bit entry whose raw X falls
  in the right window), `hide-pair` (slots 96/97). It is what found 96/97 after
  the first sentinel-only attempt.

## 4. Validation

All comparisons use the same starting SRAM and the repo's own builds:
`game/build-before` = `git HEAD` (pre-change), `game/build-local` = current.

### 4.1 96/97 removal at 512 px

Dragster frame 2500, 512 px (`E=128`), pre vs post:

- exactly **9 pixels** differ;
- all are in the parked 96/97 footprint: x 480–486, rows 40–43 and 104–106;
- no other pixel changes.

This matches the predicted image positions for slot 96 (`+357`) and slot 97
(`+352`) and explains the user's fragment.

### 4.2 Sentinel forced-keep repro

The natural routes cull the rival far from the boundary, so the old keep
branch was exercised with a temporary `SNESRECOMP_WS_PARK_REPRO` build (not in
the source). Monster frame 2360, OBJ-only at 512:

- before: 50 foreground pixels in the right region, x 491–511, y 111–131
  (the parked sentinel sliver);
- after: 0 pixels in that band.

Artifacts: `work/ws-right-edge-ghost/repro-before/`, `repro-after/`.

### 4.3 Aspect parity

Dragster frame 2500, same starting SRAM, pre vs post:

| Width | Pre | Post | Result |
|---|---|---|---|
| native 256 (WS off) | `038b97c41517b646` | `038b97c41517b646` | identical |
| 16:9, 342 | `6d5c44dc74fbef23` | `6d5c44dc74fbef23` | identical |
| 21:9, 448 (`E=96`) | `3ee4b0a89313f1f2` | `3ee4b0a89313f1f2` | identical |
| 512, `E=128` | `9df2fa9d42ffda1f` | `94d264b1c5813f81` | 9 px from 96/97 |

At `E=96` both pair entries still wrap (`357 >= 352`, `352 >= 352`), so 21:9
is unchanged. At 512 they are inside the window and are now hidden.

### 4.4 Switch off

Monster frame 2360, `SNESRECOMP_WS_OAM_XHIGH=0`: pre and post are
byte-identical (`9ffa16f896246840`), including the old artifacts. This is the
fallback contract.

### 4.5 Real right-margin rider

Idle Dragster frame 1746, OBJ-only at 512:

| `SNESRECOMP_WS_OAM_XHIGH` | Right-margin foreground (x ≥ 384) |
|---|---:|
| on | 314 px, bbox x 458–487 |
| off | 0 px |

The fix leaves a real rider in the right margin untouched.

### 4.6 Guest state

State dump from the Dragster route (`dump dragster-race`, same starting SRAM):

| Dump | SHA-256 | Result |
|---|---|---|
| WRAM | `ee891fef33edfb88…` | identical |
| SRAM | `d01674e9c7a827f1…` | identical |
| VRAM | `b1ca5746b1417189…` | identical |
| CGRAM | `486f88ff0bcbebc6…` | identical |
| OAM | pre `7f3fe124…`, post `8249f2ef…` | differs only at hidden slots |

The PPU OAM copy is the only mutated state.

## 5. Reproduction recipe

```sh
# 512 px, both riders visible, frame 2500:
SNESRECOMP_WS=1 SNESRECOMP_WS_ASPECT=8:3 \
SNESRECOMP_WS_PARK_LOG=/tmp/park.csv \
./game/build-local/UniracersSNESRecomp \
  --rom rom/"Uniracers (USA).sfc" --no-launcher \
  --script tools/scripts/ws-leading-edge/route-dragster-dump.txt
```

Look for `hide-pair` rows for slots 96/97 and `hide` rows for the sentinel.
To see the old behavior, add `SNESRECOMP_WS_OAM_XHIGH=0`.

## 6. Residual risks

- Other stale high-bit entries could also land in the right margin on another
  track; the diagnostic logs them as `right`, but only the sentinel and slots
  96/97 are hidden. No such visible entry was found in the routes tested.
- The open zoomed-track rider wrap (`$03ED = 4`, 16-bit cull/sign wrap on
  tour 2 track 1) is separate and was not observed here.
- 2P split screen and widths above 512 px remain out of scope.

## 7. Files

- `game/src/game_rtl.c` (parked sentinel + 96/97 hiding, `WsParkLog`).
- `work/ws-right-edge-ghost/` (ROM-derived captures, logs, hashes; ignored).
- `tools/` active scripts now use repo-relative paths instead of the old
  `unirally` workshop; historical logs and archived changes keep their old
  absolute paths as evidence.
