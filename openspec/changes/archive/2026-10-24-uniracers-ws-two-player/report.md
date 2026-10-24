## Status

Implemented and validated on the local ROM. The 1P gate is unchanged and the
new 2P/VS cases are deterministic. Captures and dumps stayed outside the
repository under `/tmp/opencode/ws2p-apply`; this report keeps the measured
values.

## 1. Measurements

### 1.1 State and the live-race predicate

The 2P route is `game/tools/route-validate-2p-looper.txt`; the VS route is
`game/tools/route-validate-vs-looper.txt`. The measured states (script dumps;
the trace build uses the same words):

| Screen | `$00B4` | `$0DDB` | `$0300` | `$0EE9` |
|---|---|---|---|---|
| main menu | 0 | 0 | `$4C` | 0 |
| PICK A PLAYER | 0 | 0 | `$4C` | 0 |
| PICK ANOTHER | 0 | 0 | `$4C` | 0 |
| PICK TRACK | `$0B` | 0 | `$21EC` | 0 |
| NOW PLAYING | 0 | 0 | `$4C` | 0 |
| countdown/race (2P) | 0 | 1 | 0 or 1 | 0 |
| countdown/race (VS) | `$0B` | 1 | 0 or 1 | 0 |
| pause | 0 | 1 | 0 or 1 | nonzero |

`$0DDB` becomes 1 as the race loads (the first OAM log frame is 3108 on this
route); the race screen mode `$0300 == 1` is the route's wait condition. A
2P race can briefly set `$0DDB` while a selection screen is being left; the
frame keeps the previous width until `$0300` enters the race.

### 1.2 Words

`kRamCam2X = $041B` and `kRamCam2Y = $041F` were added to
`game/src/variables.h`; `kRamRider2X/Y` are documented as CPU rival in 1P and
player 2 in 2P.

### 1.3 BG1 per half

`SNESRECOMP_WS_2P_LINE_LOG` recorded the per-line BG1 state. Looper, Megajump
and VS agree:

```text
top       lines   1..111   map base 0x0C00
divider   lines 112..113   no viewport
bottom    lines 114..223   map base 0x1C00
```

The map base switches by HDMA on line 114 (`.ppuw.tsv` of frame 4953:
`4953 0 2107 0C hdma`, `4953 114 2107 1C hdma`). Zoom Zoo has the same line
layout. The top scroll follows the top camera and the bottom scroll the bottom
camera; at frame 4700 on Looper the top was `h=169 v=55` with camera
`3728,1463`, and the bottom `h=152 v=179` with camera `1672,643`. Each half's
scroll is unwrapped independently.

The constants are `WS_VP_TOP_FIRST=1`, `WS_VP_DIVIDER_FIRST=112`,
`WS_VP_BOTTOM_FIRST=114`, `WS_VP_END=224`, and
`WsViewportOfLine()` maps a line to 0, 1 or -1.

### 1.4 Rider OAM

`SNESRECOMP_WS_2P_OAM_STAGE_LOG` was used to confirm the slot order and the
high table. The four staged entries are:

| Slot | Entry | Camera | Y offset | Sentinel |
|---|---|---|---|---|
| 96 | P2 bottom | `$041B/$041F` | +`$70` | `($30,$30)` hi=1 |
| 97 | P1 bottom | `$041B/$041F` | +`$70` | `($30,$30)` hi=1 |
| 98 | P1 top | `$0419/$041D` | 0 | `($70,$70)` hi=0 |
| 99 | P2 top | `$0419/$041D` | 0 | `($70,$70)` hi=0 |

The staged tile/attribute patterns are P1 tile `$00`, attr `$66`/`$26`; P2
tile `$88`, attr `$68`/`$28`. During the race the high-table byte at
`highOam[24]` is `$A5` (or `$AD` when a bottom sprite is large), with a
98-frame transition `$55` before the race; the staging `$1599` follows it.
Slots 96/97 carry the high bit even when their staged X is positive; the host
therefore applies `xhigh-clear` there. The 1P body is not used in 2P.

`SNESRECOMP_WS_2P_OAM_LOG` actions seen over the validation runs:
`xhigh-clear` for the real bottom riders, `keep` for native/top entries,
`hide` for the sentinels, `skip` for non-rider staging and `hi-positive` for
a positive entry whose high bit is the better decode. On the P2-only route the
first `xhigh` is slot 99 at frame 4175 with `rel=273`, and the OBJ-only capture
shows P2 in the top right margin; the left-margin P1 bottom entry at
`rel=-86` is also visible. The 1P parked-pair and `$170,$70` rules are never
applied in 2P.

**Playtest finding: bottom rider missing.** The first playback showed the
bottom half with no rider (Switcher, 2P). The end-of-frame high byte stages
the top slots as big (`size=1`) and the bottom slots as small (`size=0`); the
retail game writes the bottom high byte during the frame, and the framework's
draw does not replay OAMDATA. The frame-level X fix alone cannot restore the
missing bottom rider. The existing beam line hook now runs `WsOamLineFix()`:
on the bottom lines it sets slots 96/97's size bit to 1 and the X high bit
from the true WRAM X; on the top lines it does the same for 98/99. Only
`highOam[24]` is written; hidden sentinels are skipped. After this, the
Switcher capture shows MARTIN's green rider in the bottom half. The same
per-line pass is installed for any live 2P/VS, not only wide, so native 4:3
also shows the bottom player now. The five wide 2P/VS frames and `2p-native`
changed and were re-recorded; every 1P reference stayed byte-identical.

### 1.5 VS

The VS route (`main menu -> VS -> PICK PLAYER ONE -> PICK PLAYER TWO ->
PICK TOUR -> PICK TRACK -> NOW PLAYING`) reaches the same split-screen race.
`$00B4` is `$0B`, `$0DDB` is 1, the cull is `$FCF0/$1000` with `$03ED=4`, and
the line layout, slots and HUD rows match 2P.

## 2. BG1 probe per viewport

`world_key_check.py --bottom-line 114 --served` splits the probe at the bottom
viewport, unwraps each half's scroll and skips the two divider lines. At 16:9
with `source`:

| Track | vp0 wrong | vp1 wrong | vp0/vp1 conflicting |
|---|---|---|---|
| Dragster | 0 | 0 | 0 / 0 |
| Zoom Zoo | 0 | 0 | 0 / 0 |
| Bowl | 0 | 0 | 0 / 0 |
| Switcher | 0 | 0 | 0 / 0 |
| Monster | 0 | 0 | 0 / 0 |
| Looper | 0 | 0 | 0 / 0 |

The unverifiable leading cells are reported per viewport; none are served
with a wrong word. The scorer change is the divider skip: before it, the two
divider rows were counted as top-half cells and produced 28/48 false wrong
cells on Zoom Zoo.

## 3. HUD and cull

- 16:9, 21:9 and 512 px frames were captured at 342×224, 448×224 and
  512×224. `RACE`, the timer and the player name sit at the widened edges in
  both halves, the divider spans the full width and the centred prompts stay
  put.
- `SNESRECOMP_WS_HUD=0` keeps the frame wide but puts BG3 back in the native
  centre in both halves.
- `SNESRECOMP_WS_CULL=0` keeps the frame wide and removes the margin riders.
- The `2p-clamp` case passes; `PpuSetWidescreenLayerClamp(ppu, BG1)` keeps the
  track native-width in both halves.

## 4. Guest state

The cross-run WRAM/VRAM/CGRAM diff was first misleading: two separate runs of
the same route differed by 333 / 460 / 22 bytes, and a WS=0 run compared with
a WS=1 run differed by the same amounts. A second WS=0 run and the WS=1 run
with the same timing were **identical** in WRAM, VRAM and CGRAM (0 bytes
different); the one-byte SRAM difference also appears between two WS=0 runs,
so it is run-to-run save noise, not the feature. The only guest write in the
code is the existing cull window; the OAM fixes write the PPU copy and the
HUD/margins write PPU configuration.

## 5. Validation gate

Before the change: `game/tools/validate.sh` 10/10 PASS.
After the per-viewport refactor and the HUD/OAM changes: 10/10 PASS (1P hashes
unchanged). The final gate has six new cases:

```text
2p-native, 2p-16x9, 2p-21x9, 2p-512, 2p-clamp, vs-16x9
```

`validate.sh --record` produced no `NONDET` entries and left every 1P
reference byte-identical; the final `validate.sh` passes 16/16. After the
per-line OAM size fix, the five wide 2P/VS references and `2p-native` changed
and were re-recorded; every 1P reference stayed byte-identical, and the final
gate still passes 16/16.

## 6. Residual limits

- The 2P OAM frame-level pass hides sentinels and fixes the missing X bit in
  wide mode; the per-line pass synthesises the high-table size/X for the
  line's half and also hides sentinels in native 4:3, because the framework's
  draw does not replay OAMDATA. It is installed for any live 2P/VS, wide or
  native. The high table seen at `WsOamFix` can be one frame ahead; the same
  one-frame slack as the 1P fix covers it.
- The radar remains 1P-only.
- The 2P validation route uses SHUFFLER/LOOPER; the Crawler route
  (`route-validate-2p-zoomzoo.txt`) reaches Zoom Zoo. The probe was also run
  on Dragster, Bowl, Switcher and Monster by changing the PICK TRACK down
  count; those routes are not part of the default 16-case gate.
- Side-by-side composition of the two halves remains out of scope.

## 7. Decision

**Accept.** The 1P output is unchanged, both split-screen halves widen with
their own BG1 state, HUD and rider OAM, the 2P/VS frames are deterministic,
and the BG1 probe reports zero wrong cells in both halves on the six measured
tracks. The remaining limits are documented and do not affect the acceptance
of the requested feature.
