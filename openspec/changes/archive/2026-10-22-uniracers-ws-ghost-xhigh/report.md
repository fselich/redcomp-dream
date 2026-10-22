# uniracers-ws-ghost-xhigh — report

Date: 2026-10-21
Status: **go** for `SNESRECOMP_WS_OAM_XHIGH` (default on).

## 1. Cause

The rider staging code (`$82:ADB7…AE57`) sets the OAM X high bit only when the
screen X is **negative**. The native right cull is 256, so a positive X could
never reach the bit. The widened cull (`uniracers-ws-objects`, `256 + E`) now
admits X in `[256, 256 + E)`; those values are uploaded with the high bit clear,
and the PPU decodes `X - 256`, drawing the rival as a phantom on the left.

Reproduced with `work/ws-ghost-offscreen/scripts/run.sh` (Dragster idle,
`route-dragster.txt`), frames 1725–1750:

| frame | true X | OAM X low / high | drawn before |
|---:|---:|---|---|
| 1739 | 244 | 244 / 0 | right, correct |
| 1740 | 258 | 2 / 0 | **left phantom** |
| 1741 | 272 | 16 / 0 | **left phantom** |
| 1742 | 286 | 30 / 0 | **left phantom** |
| 1743 | 300 | parked (`X=112|hi`, `Y=112`) | hidden |

`frames/before-1742.png` captures the phantom. The left margin was already
correct because the game sets the high bit for negative X.

## 2. Fix

`WsFixRiderOamXHigh(Ppu *)` in `src/game_rtl.c`, called from
`GameDrawPpuFrame` immediately after `WsInjectTestOam`, before rasterization.

For each rider slot (98 player `$0411`, 99 rival `$0413`):

```text
rel  = (int16)(riderX - cameraX)      # $0419
want = rel << $03ED                   # the cull X (0 on Dragster)
lo   = ppu->oam[slot*2] & 0xFF
hi   = highOam X bit for the slot
if !hi and lo < E and want in [256-SLACK, 256+E+SLACK):
    if |lo+256 - want| < |lo - want|:
        set the high bit in ppu->highOam
```

`SLACK = 32` absorbs the up-to-one-frame lag between the WRAM position and the
uploaded OAM. The comparison is what picks `lo + 256` over `lo`; the fix does
not require an exact match. Only the PPU's uploaded `highOam` copy is changed;
guest WRAM (`$1599`, `$150x`) is never written. The uploader rebuilds the copy
each frame, so the patch does not accumulate.

The high-table bit is `highOam[slot >> 2]`, bit `(slot & 3) * 2` (X high; the
next bit is size). The gate is:

```text
WideEnabled() && InLiveRace()
&& $0DDB == 0            # 1P full screen
&& WsCullEnabled()
&& WsExtra() > 0
&& SNESRECOMP_WS_OAM_XHIGH != 0
```

`SNESRECOMP_WS_OAM_XHIGH=0` is an independent kill switch (the cull stays on).
`SNESRECOMP_WS_OAM_XHIGH_LOG=<path>` writes `frame,slot,lo,want` for every fixed
slot.

## 3. Verification

### 3.1 Dragster idle: phantom gone, rival in the right margin

After the fix, the log shows:

```text
1742,99,2,272
1743,99,16,286
1744,99,30,300
```

The draw frame trails the WRAM recording by one frame, so the first fixed draw
is 1742 for the WRAM 1740 entry. `frames/after-1742.png` shows the rival at wide
x ≈ 301 (true X 258 + 43), no rider pixels left of the player. The before/after
diff at 1742 is at x 48–95 (the old phantom) and x 304–341 (the corrected rival);
**x < 43 (the left margin) is unchanged**.

### 3.2 Dragster moving and Zoom Zoo

- Dragster moving (`route-dragster-move.txt`, 1700–2500): 65 fixed slots; no
  left phantom in the log/visual; `$03ED = 0`.
- Zoom Zoo idle (`route-zoomzoo.txt`) and moving
  (`route-zoomzoo-move.txt`), 1400–3500: the fix log has **no fixed slots**;
  `$03ED = 2` in the race. The zoom-shifted cull is active, but the rival's
  uploaded OAM X does not land in the aliased `[0, E)` range while admitted in
  these windows, so there is nothing to fix. `frames/zoomzoo-1800.png` shows no
  left phantom.
- The design's open question is answered: **Zoom Zoo uses `$03ED = 2`**, and no
  aliased slot occurred in the sampled routes.

### 3.3 Lag

For every logged fix, `|lo + 256 - want|`:

| Route | Fixes | Max |
|---|---:|---:|
| Dragster idle 1742–1744 | 3 | 14 |
| Dragster moving 1745–… | 65 | 6 |
| 21:9 / 512 idle 1742–1750 | 9 | 14 |

The maximum is 14 < `SLACK` (32). The first idle run's constant 14 is the
one-frame upload lag; the moving route's max is 6.

### 3.4 Left margin unchanged

Zoom Zoo moving frame 1817 (slot 99 true X < 0) with the fix on and off:
byte-identical (`cd282d97…`). The Dragster 1742 before/after diff has no pixels
in x < 43.

### 3.5 Native and switch-off hashes

| Run | SHA-256 |
|---|---|
| Wide default, `SNESRECOMP_WS_OAM_XHIGH=0`, Dragster 2500 | `4b138431bdc04382a04695d683bf36ba2a1eeb284e1ef1c600ad10c798a8e357` |
| Native WS-off (`SNESRECOMP_WS=0`), Dragster 2500 | `3417f91afc8f6d47ea4ab3f7b11bbeec4cc7491a23ab71cc6c956f5fb3f5df44` |

The switch-off wide hash is the pre-change 16:9 default. (With `SNESRECOMP_WS`
unset the config `Widescreen = 1` still selects wide; the native check uses
`SNESRECOMP_WS=0`.)

### 3.6 Presentation-only

`SNESRECOMP_STATE_TRACE`, 2,500 Dragster frames, fix on vs off: **identical**.
The WRAM/CPU trace does not see the `highOam` patch.

### 3.7 21:9 and 512 px

| Target | Frame | E | Fixes | Result |
|---|---:|---:|---:|---|
| `SNESRECOMP_WS_ASPECT=21:9` | 448×224 | 96 | 5 (1742–1746) | rival visible at the right |
| `SNESRECOMP_WS_ASPECT=8:3` | 512×224 | 128 | 9 (1742–1750) | rival visible at the right up to true X 384 |

`frames/wide-219b-1746.png` shows the rival in the right margin;
`frames/wide-512-1756.png` (after the game parks the rival) shows no parked
sprite. The parked X=112|hi is hidden by the renderer's right-margin hint
handling at E=128.

## 4. Files and artifacts

- Code: `src/game_rtl.c` only.
- Patch: `work/ws-ghost-xhigh/patches/ws-ghost-xhigh.diff` (against the saved
  `game_rtl.c.before`; it contains only this change).
- Logs: `logs/xhigh-*.csv`, `logs/*.trace`, `logs/*.log`.
- Frames: `frames/before-1742.png`, `after-1742.png`, `after-1744.png`,
  `zoomzoo-1800.png`, `wide-219b-1746.png`, `wide-512-1756.png`, state/wide
  PPMs.
- Repro evidence: `work/ws-ghost-offscreen/scripts/xhigh-repro.json` and the
  copied `run-xhigh-repro.log`/`rec-xhigh-repro.log`.

## 5. Decision

Go for the fix, default on. It removes the phantom and draws the rival at its
true X in the right margin on Dragster, at 21:9 and at 512 px, while leaving
native 4:3, the switch-off wide output and the guest state bit-identical.
Out of scope: 2P split screen (`$0DDB ≠ 0`), the parking position, and any
generic OAM hint work.

## Addendum — zoom regression (2026-10-21)

Playtest at 21:9 showed the player vanishing on Zoom Zoo. On zoomed tracks
(`$03ED = 2`) the OAM X is the unshifted `rel`; only the cull compare uses
`rel << $03ED`. The fix shifted `want`, so it set the high bit on the player.
Corrected (`want = rel`), and the cull widening now scales by `E << $03ED`
(before, Zoom Zoo got only E/4 px of margin). See tasks §5 and
`work/ws-ghost-xhigh/regress-zoom/`.

## Addendum — parked sprite on ultrawide (2026-10-21)

On wide screens (E above ~96, e.g. 2.39:1) the game's parking slot for a
culled rider (`X=$170` = −144, `Y=112`, large) reached the left margin and
showed a sliver of the rival at the far left. The fix now moves a parked rider
below the field (small, `Y=$E0`) in the PPU copy, unless WRAM says the rider
really is at (−144, 112). The output at 16:9 and 21:9 is unchanged (the parked
sprite was never visible there). See tasks §6 and
`work/ws-ghost-xhigh/regress-zoom/park-*`.

## Addendum — race gate on later tours (2026-10-22)

Widescreen was active only on tour 1. The live-race gate required
`$0B4 == 1`, a value measured only on tour 1. A RAM monitor during play showed
`$0B4 = 0x0B` in a later-tour race, with `$0EE9 = 0` and `$0300 ∈ {0,1}` as on
tour 1. The gate is now `$0B4 != 0 && $0EE9 == 0 && $0300 <= 1` (in both
`main.c` and `game_rtl.c`). That track also uses `$03ED = 4`; the cull scale
clamp is now ≤ 6. Tour 1 output is byte-identical. See tasks §7.

## Addendum — BG1 margins cut on a tour-2 track (2026-10-22)

Out of this change's original scope (BG1 `source` mode), recorded here
because it came from the same playtest. On tour 2 track 1 the left margin was
sometimes cut. The track table is circular in X; the source lookup treated
cells beyond the grid's X edge as empty, so the anchor failed its self-check
whenever the view crossed that edge. The lookup now wraps X modulo the grid
width. Tour-1 BG1 probe scores are unchanged (0 wrong). Pending: a user
replay on the tour-2 track (tasks 8.4). Evidence: `work/ws-source/xwrap/`.

## Open — rival flashing in wrong places (2026-10-22, parked)

The user saw the rival vanish and reappear briefly in wrong places twice on
tour 2 track 1, and could not reproduce it. Unconfirmed hypothesis: on
`$03ED = 4` the cull compares `rel << 4` in 16 bits, which wraps when
`|rel| ≥ 2048` px. A rival about 4096 px away can then pass the (now wider)
window. A possible presentation-only guard: do not draw a rider whose WRAM
`rel` is outside `[-E-64, 256+E)`. **Rejected:** on this track the table is
circular with a 4096 px period, the same period as the 16-bit wrap. So an
aliased rival lands exactly where it would be in a circular world. The guard
would hide a rival that is truly adjacent across the edge. Capture data first. To capture it: launch the
game normally (not via the chat `!`, which blocks the chat), pause when it
happens, and run `work/ws-ghost-xhigh/gate/grab.py <out.json>`. It pulls the
last ~6000 frames over the debug port 4377.
