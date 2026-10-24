# Widescreen parked sprites (right-edge sliver)

Status: fixed by `openspec/changes/uniracers-ws-right-edge-ghost` (archived
report in that change). Applies to the 512 px cap (`E = 128`).

## Symptom

On an ultrawide display a small fragment of a rider appears at the far right
edge, aligned with the rider's row. It looks like a reflected piece of the
current character. The size depends on the display scale: at 512 px the
fragment is roughly 7 px wide, so a 3–4× presentation makes it look larger.
It is intermittent because it follows the game's OAM parking values, not the
player's position.

## The 9-bit OAM X window

The PPU stores 9-bit sprite X. The runner decodes it with
`PpuDecodeOamX`:

```
x = (oam_lo | (high_bit << 8))
if (x >= 256 + E) x -= 512
```

`E` is the extra width per side: `(frame_width - 256) / 2`. At 4:3 `E = 0`;
at 16:9 `E = 43`; at 448 px `E = 96`; at the 512 px cap `E = 128`.

The game sets the high bit when its screen X is negative (left margin). In
4:3 a negative X outside the 256-px window wraps off-screen. At large `E` the
wrap threshold (`256 + E`) moves right, so some negative positions are no
longer wrapped and instead land inside the right margin.

## Parked entries

### Slots 96/97 — the parked 1P 2P pair

In a 1P race these slots are the unused 2P pair. Their gameplay OAM values are
stable across every capture:

| Slot | Raw X | Y | High bit | Decoded world X | Image X (E=128) | Image rows |
|---:|---:|---:|---:|---:|---:|---:|
| 96 | `$65` (101) | `$20` (32) | 1 | `+357` | 485 | ~40–43 |
| 97 | `$60` (96) | `$60` (96) | 1 | `+352` | 480 | ~104–106 |

The same 9-bit value is a negative X of `-155`/`-160`; 4:3 wraps them
off-screen. At `E = 128` the threshold is `384`, so `357` and `352` stay
positive and draw in the right margin.

Observed thresholds:

- slot 96 (`x9 = 357`) is inside the window when `256 + E > 357`, i.e.
  `E >= 102`; at 21:9 (`E = 96`) it still wraps;
- slot 97 (`x9 = 352`) is inside when `E >= 97`; at 21:9 (`E = 96`) it still
  wraps.

So explicit 21:9 is normally unchanged; 512 px and the `auto` ultrawide path
(which caps 21:9 monitors at 512) show the pair.

### Slots 98/99 — the culled-rider sentinel

Player = slot 98, rival = slot 99. A culled rider is parked at
`X = $170` (368), `Y = $70` (112), large, with the high bit set.

At `E = 128` the raw `368` is below `384`, so it decodes as `+368` (image X
496). The old guard kept the sentinel when the rider's WRAM position was
within 4 px of `(-144, 112)` — the parked case — so the sliver survived. At
smaller `E` the same value wraps to `-144` and can show on the left.

## Fix

`game/src/game_rtl.c`, `WsFixRiderOamXHigh`, inside the existing 1P
widescreen gates:

1. Sentinel (`X=$170`, `Y=$70`, high bit): always move below the field
   (`Y=$E0`), clear the high bit in the PPU copy. No WRAM keep exception.
2. Slots 96/97: when their X high bit is set, move them below the field and
   clear the bit in the PPU copy. A high-bit-clear (positive X) entry is left
   alone.
3. Real right-margin riders: unchanged. A rider at true `+368` uploads
   `lo=112` without the high bit; the fix adds the bit after the parked
   checks.

Only `ppu->oam` and `ppu->highOam` are written. Guest WRAM, VRAM, CGRAM,
SRAM and simulation are untouched.

## Switches and diagnostics

| Variable | Effect |
|---|---|
| `SNESRECOMP_WS_OAM_XHIGH=0` | Disables the high-bit fix and both parked-slot hiding. Restores pre-change frames byte-for-byte. |
| `SNESRECOMP_WS_PARK_LOG=<path>` | CSV `frame,slot,lo,y,hi,rel,rel_y,E,action`. Actions: `candidate`/`hide` (sentinel), `right` (any high-bit entry whose raw X lands in the right window), `hide-pair` (slots 96/97). `rel`/`rel_y` are meaningful for 98/99 only. |
| `SNESRECOMP_WS_OAM_XHIGH_LOG=<path>` | Existing log for high-bit fixes; `lo=-1` means a parked slot was hidden. |

The 1P gate is `$0DDB == 0`; 2P split screen is untouched.

## Validation summary

Same starting SRAM, pre-change (`git HEAD`) vs fixed:

| Case | Result |
|---|---|
| native 4:3, Dragster 2500 | hash `038b97c4…` identical |
| 16:9 (342), Dragster 2500 | hash `6d5c44dc…` identical |
| 21:9 (448, E=96), Dragster 2500 | hash `3ee4b0a8…` identical |
| 512 (E=128), Dragster 2500 | 9 px differ, all in the 96/97 footprint (x 480–486, rows 40–43 and 104–106) |
| `SNESRECOMP_WS_OAM_XHIGH=0`, Monster 2360 | hash `9ffa16f8…` identical |
| WRAM / SRAM / VRAM / CGRAM | identical |
| OAM dump | differs only at the hidden slots |
| real rival in right margin, Dragster 1746 | still drawn (314 px, x 458–487) |

The user's playtest after the 96/97 fix: "ya se ve bien".

## Reproduce / diagnose

```sh
SNESRECOMP_WS=1 SNESRECOMP_WS_ASPECT=8:3 \
SNESRECOMP_WS_PARK_LOG=/tmp/park.csv \
./game/build-local/UniracersSNESRecomp \
  --rom rom/"Uniracers (USA).sfc" --no-launcher \
  --script tools/scripts/ws-leading-edge/route-dragster-dump.txt
```

Look for `hide-pair` rows on slots 96/97 and `hide` rows for the sentinel.
If a new artifact appears, use the `right` rows to find every high-bit slot
whose raw X is in the right window; then decide whether it has a legitimate
1P position before hiding it.

## Related

- `openspec/changes/uniracers-ws-right-edge-ghost/` — the change and report.
- `openspec/changes/archive/2026-10-22-uniracers-ws-ghost-xhigh/` — original
  rider high-bit fix and parked-sentinel follow-up.
- `openspec/changes/archive/2026-10-22-uniracers-ws-ultrawide/` — the 512 px
  cap and the `E`-derived cull/HUD policy.
- Open, separate: zoomed-track rider wrap (`$03ED = 4`, 16-bit cull/sign
  wrap); not observed in this work.
