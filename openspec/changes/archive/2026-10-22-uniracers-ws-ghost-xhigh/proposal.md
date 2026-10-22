## Why

In widescreen races the CPU rival sometimes "goes crazy" once it leaves the
4:3 view. Playtesting showed this on Dragster and Zoom Zoo. A recording
(`work/ws-ghost-offscreen/`) found the cause.

The rider staging code (`$82:ADB7…AE57`) sets the OAM X high bit only when the
screen X is **negative**. The game's native right cull (256) meant a positive X
never reached 256. Our widened cull (`uniracers-ws-objects`, `256 + E`) now
admits X in `[256, 256 + E)`. Those values are uploaded without the high bit,
so the hardware reads them as `X − 256` and draws a phantom rival on the left
side of the screen.

Dragster, player idle, rival leaving to the right:

| frame | true X | OAM X / high bit | drawn at |
|---:|---:|---|---|
| 1739 | 244 | 244 / 0 | right, correct |
| 1740 | 258 | 2 / 0 | **left (phantom)** |
| 1742 | 286 | 30 / 0 | **left (phantom)**, next to the player |
| 1743 | 300 | parked (`X=112\|hi`, `Y=112`) | hidden |

This single defect explains all three reported symptoms:

- the rival vanishes before the right wide edge;
- a phantom appears near the player;
- a "frozen" sprite sits on the left while the rival rides just ahead.

The left margin is correct, because the game sets the high bit for negative X.

## What Changes

- A presentation-only fix in `GameDrawPpuFrame`. For each rider OAM slot
  (98 player, 99 CPU rival), resolve the 256-px ambiguity of the uploaded X
  using the rider's true screen X from WRAM. When the true X is in
  `[256, 256 + E)`, set that slot's X high bit in the PPU's `highOam` copy.
- The fix never writes guest WRAM (`$1599`, `$150x`). Simulation, RNG, AI and
  collision are untouched.
- It is active only with widescreen on, in a live 1P race, with the cull
  widening active.
- Independent kill switch `SNESRECOMP_WS_OAM_XHIGH=0`.
- Validation:
  - the rival is drawn in the right margin on Dragster and Zoom Zoo;
  - no phantom appears on the left;
  - native 4:3 and the WS-off output are unchanged;
  - the left margin is unchanged.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `recomp-ws-objects`: adds a requirement that riders admitted by the widened
  right cull are drawn at their true position in the right margin, never
  aliased by 256 px. The fix is presentation-only and has its own switch.

## Impact

- `src/game_rtl.c` only (new helper called from `GameDrawPpuFrame`). No
  framework (`snesrecomp`) change.
- Works at every width up to 512 px (`E ≤ 128`). The renderer's
  `PpuDecodeOamX` already maps a high-bit X in `[256, 256 + E)` to the right
  margin.
- Artifacts under `work/ws-ghost-xhigh/`. The diagnosis evidence stays in
  `work/ws-ghost-offscreen/`.
- Out of scope:
  - 2P split screen (`$0DDB ≠ 0`);
  - the culled parking position (`X=−144`, `Y=112`), which was not visible at
    16:9, 21:9 or 512 px;
  - any simulation change.
