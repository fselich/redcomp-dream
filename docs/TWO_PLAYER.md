# Two-player split screen

How the port widens the 2P and VS split-screen races. The 1P path is
described in [`WIDESCREEN.md`](WIDESCREEN.md); the track margins in
[`TRACK_MARGIN.md`](TRACK_MARGIN.md).

## Detection

`WsInLiveRace()` (`game/src/ws_frame.c`) is the single gate:

```c
if (g_ram[kRamPauseFinish] != 0 || g_ram[kRamScreenMode] > 1) return 0;
if (g_ram[kRamTwoPlayer] != 0) return 1;       /* 2P/VS split screen */
return g_ram[kRamRaceMode] != 0;               /* 1P: 1 or 0x0B */
```

The split-screen race does not set `$00B4` (it is `0` in 2P and `$0B` in VS);
`$0DDB` is the split-screen flag. `$0300 <= 1` excludes the menus and
selection screens, and `$0EE9` excludes pause and the finish. Menus, PICK
PLAYER/TOUR/TRACK, NOW PLAYING and the attract/demo stay at 256×224.

Measured state on the trace route:

| Screen | `$00B4` | `$0DDB` | `$0300` | `$0EE9` |
|---|---|---|---|---|
| main menu / PICK PLAYER / PICK ANOTHER | 0 | 0 | `$4C` | 0 |
| PICK TRACK | `$0B` | 0 | `$21EC` | 0 |
| NOW PLAYING | 0 | 0 | `$4C` | 0 |
| countdown / race | 0 (2P) or `$0B` (VS) | 1 | 0 or 1 | 0 |
| pause | 0 | 1 | 0 or 1 | nonzero |

A 2P race can briefly set `$0DDB` while a selection screen is being left; the
line and frame keep the previous width until `$0300` also enters the race,
which is the same transition the 1P path has.

## Layout

Each half is widened by the same `E` on each side; the height stays 224 and
the split stays at line 112. The measured BG1 viewports are:

```text
top       lines   1..111   BG1 map base 0x0C00
divider   lines 112..113   no viewport
bottom    lines 114..223   BG1 map base 0x1C00
```

The frame is `E+256+E × 224`: 342×224 at 16:9, 448×224 at 21:9 and 512×224 at
the cap. `PrepareFrame()` and the aspect/max-width settings are shared with
1P; the 512-px cap is the same 9-bit OAM X limit.

Each half has its own camera:

| Word | Meaning |
|---|---|
| `$0411`/`$0415` | P1 rider X/Y |
| `$0413`/`$0417` | P2 rider X/Y (CPU rival in 1P) |
| `$0419`/`$041D` | P1/top camera X/Y |
| `$041B`/`$041F` | P2/bottom camera X/Y |

The bottom rider's staged Y is `relY + $70`.

## BG1 margins: one state per viewport

The 1P ring observer kept one BG1 map base (latched on line 1), one scroll
unwrap and one seen table, and the source anchor kept one offset. In 2P the
bottom half has a different map base and a camera that can be thousands of
pixels from the top one, so all of that state is per viewport:

- `ws_ring.c`: `WsRingVp` with its own map base, scene generation, unwrap,
  first line and `seen[32][32]`; `WsRingLine(ppu, line, vp)` starts the
  viewport on line 1 or 114.
- `ws_source.c`: the anchor set, generation and race start are per viewport;
  the grid header and piece-library word index are shared track data.
- `ws_margin.c`: the line and tile hooks select the viewport by line, skip the
  divider lines, and keep `s_logged` per viewport.

`WsViewportOfLine()` in `ws_frame.c` owns the measured line constants
(`WS_VP_TOP_FIRST`, `WS_VP_DIVIDER_FIRST`, `WS_VP_BOTTOM_FIRST`,
`WS_VP_END`). The 1P path uses viewport 0 for every line and is
output-identical.

Validation (BG1 probe, `source`, 16:9, per viewport):

| Track | viewport 0 wrong | viewport 1 wrong | conflicting cells |
|---|---|---|---|
| Dragster | 0 | 0 | 0 / 0 |
| Zoom Zoo | 0 | 0 | 0 / 0 |
| Bowl | 0 | 0 | 0 / 0 |
| Switcher | 0 | 0 | 0 / 0 |
| Monster | 0 | 0 | 0 / 0 |
| Looper | 0 | 0 | 0 / 0 |

The probe scorer has a `--bottom-line 114` mode that splits the rows by
viewport, unwraps each half's scroll independently and skips the two divider
lines. The looping tracks show more unverifiable leading cells than the
straight ones, never wrong ones.

## HUD

BG3 has two copies of `RACE`, the timer, the centred prompt and the player
name. The 2P path uses three elastic bands and no clamp band:

```text
slot 0  lines  10..112   top half      T
slot 1  lines 112..114   divider       D
slot 2  lines 114..224   bottom half   T

T = { 17..64  -> 4-E .. 51-E }   RACE, 4 px inside the left edge
    { 64..192 -> 64..192   }     centre prompt, unchanged
    {192..256 -> 192+E..256+E}   timer and name, 1:1 at the right edge
D = {  0..256 -> -E .. 256+E }   divider stretched
```

The BG3 widen gate starts at line 10 (`PpuSetWidescreenBg3Widen(ppu, 10)`), so
the top header rows 11..16 are remapped too. Columns no segment covers are
empty, which is the clamp; a clamp band would disable the elastic bands (the
clamp and the widen gate win over an elastic band in the framework). The 1P
path keeps its single band (17..32, clamp band 32..224); the 1P and disabled
paths clear slots 1 and 2 so a 2P band cannot survive.

Measured BG3 glyph rows: top `RACE`/timer 11..21, name 91..101; divider
112..113; bottom `RACE`/timer 123..133, name 203..213. The centred prompt is at
native x 81..174 in both halves.

## Riders: four OAM slots

The staging routine `$82:ACA5` writes four entries and `$82:D2DC` uploads them
as slots 96..99:

| Slot | Entry | Rider X/Y | Camera X/Y | Y | Sentinel |
|---|---|---|---|---|---|
| 96 | P2 bottom | `$0413/$0417` | `$041B/$041F` | +`$70` | `($30,$30)` hi=1 |
| 97 | P1 bottom | `$0411/$0415` | `$041B/$041F` | +`$70` | `($30,$30)` hi=1 |
| 98 | P1 top | `$0411/$0415` | `$0419/$041D` | — | `($70,$70)` hi=0 |
| 99 | P2 top | `$0413/$0417` | `$0419/$041D` | — | `($70,$70)` hi=0 |

`WsFixRiderOamXHigh2P()` runs once per frame before the draw, in the same
place as the 1P fix, and only edits the PPU OAM copy/high table:

1. **Sentinel.** A `(lo, y)` equal to the half's sentinel with the measured
   high bit is moved below the field. The sentinel Y cannot be a real rider:
   the vertical window is `[-41,112)`, so a real top entry never has Y=112 and
   a real bottom entry has Y>=71, never 48.
2. **High bit already set.** Normally the game's negative-X encoding, so leave
   it. If the WRAM-derived X is positive and clearly prefers the low byte
   (`lo`) over the decoded high-bit value, the bit was set for a positive
   rider; clear it (`xhigh-clear`). This is measured on real bottom riders and
   is needed for their own half.
3. **No high bit, `lo >= E`.** Leave it; it decodes natively.
4. **Missing high bit.** For a real rider whose true X is in
   `[256-32, 256+E+32)`, set bit 8 when `lo + 256` is closer to the true X
   than `lo` (`xhigh`). This is the 1P rule with the same one-frame slack.
5. Otherwise leave the slot.

A slot is only touched when its tile/attribute match the measured rider
patterns: P1 tile `$00`, attr `$66`/`$26`; P2 tile `$88`, attr `$68`/`$28`.
Unknown contents are logged as `skip` and left alone. The 1P parked-pair and
`$170,$70` rules never run in 2P.

The action log is `SNESRECOMP_WS_2P_OAM_LOG`
(`frame,slot,half,lo,y,hi,tile,attr,rel,rel_y,E,action`). The raw staging
trace is `SNESRECOMP_WS_2P_OAM_STAGE_LOG`.

The end-of-frame high byte stages the top slots as big and the bottom slots as
small; the retail game writes the bottom high byte during the frame. Because
the framework's raster journal does not replay OAMDATA (`$2104`), the frame
level pass alone left the bottom half with no rider in playtest. A second
pass, `WsOamLineFix()`, runs from the existing beam line hook before each
line: on the bottom lines it sets slots 96/97's size bit to 1 and the X high
bit from the true WRAM X; on the top lines it does the same for 98/99. It
writes only `highOam[24]`, skips hidden sentinels, and synthesises the state
from the staged entries and WRAM; it does not replay the game's OAMDATA
writes. The pass is installed for any live 2P/VS, not only wide, so native
4:3 also shows the bottom player. The high table seen at `WsOamFix` can be one
frame ahead of the presented frame; the same slack as the 1P fix covers it.

## Cull

`WsApplyCull()` already scales the margin by `$03ED`. In 2P `$03ED = 4` and the
base is `[-784,4096]`, so `extra = E << 4` gives `[-49-E, 256+E]` screen
pixels: `[-92,299]` at 16:9. The four rider entries share the window. The
native pair is restored when widescreen or the race ends.

## Gating and limits

- `[Graphics] Widescreen`, `[Widescreen] Hud`, `Margin` and `Cull` apply
  independently; there is no 2P-specific option.
- The radar stays 1P-only (`WsRadarUpdate` requires `!ctx->two_player`).
- No new guest write: the cull window is still the only WRAM write; OAM fixes
  only the PPU copy; HUD/margins only PPU configuration.
- The two halves are not composed side by side; that remains a separate
  future change.
- The 512-px cap applies per half.
- VS uses the same layout, slots and HUD rows as 2P; `$00B4` is `$0B` in VS
  but `$0DDB` already makes it live.

## Validation

`game/tools/validate.sh` has native, 16:9, 21:9, 512 and clamp cases for 2P
(`route-validate-2p-looper.txt`) and a VS case
(`route-validate-vs-looper.txt`). The references are recorded and
deterministic; the 1P references are unchanged. The BG1 probe table above was
taken with `SNESRECOMP_BGPROBE_*` and scored with
`tools/scripts/ws-offwindow/world_key_check.py --bottom-line 114 --served`.

The 2P route reaches SHUFFLER/LOOPER through the two player screens. The
Crawler route (`route-validate-2p-zoomzoo.txt`) reaches Zoom Zoo; the probe
was also run on Dragster, Bowl, Switcher and Monster by changing the PICK
TRACK down count.
