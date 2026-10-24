## Context

The port widens only live 1P races. The gate is `WsInLiveRace()` in `game/src/ws_frame.c`:

```c
return g_ram[kRamRaceMode] != 0 && g_ram[kRamPauseFinish] == 0 &&
       g_ram[kRamScreenMode] <= 1;
```

`kRamRaceMode` is `$00B4`; it is `1` on tour 1 and `$0B` on later tours. In a 2P/VS split-screen race it is **`0`**. The split-screen flag is `kRamTwoPlayer = $0DDB`, which is `1` only once the split-screen race starts. `WsFrameCtx` already carries `two_player`, but the only consumers are the radar and the 1P OAM fix, both of which disable themselves when it is set. Consequently nothing widens in 2P: `PrepareFrame()` returns 256 px, and the cull, BG1 margins, HUD and OAM fixes are gated off.

Read these before implementing:

- `docs/ARCHITECTURE.md` (module map, frame sequence, the guest-state rule);
- `docs/WIDESCREEN.md` (1P frame, HUD bands, rider OAM fixes, limits);
- `docs/TRACK_MARGIN.md` and `docs/formats/BG1_RING.md` (ring, seen table, anchor);
- `openspec/specs/recomp-ws-hud`, `recomp-ws-objects`, `recomp-ws-source`, `recomp-ws-ultrawide`;
- archived changes `2026-10-20-uniracers-ws-objects`, `2026-10-22-uniracers-ws-ghost-xhigh`, `2026-10-23-uniracers-ws-right-edge-ghost` (OAM path, sentinels, parked pair) and `2026-10-22-uniracers-ws-source` (anchor);
- `docs/ROADMAP.md` W4.

### Frame sequence that matters here

`game/src/game_rtl.c`:

```text
GameRunOneFrame:   WsApplyCull → snes_beam_frame_driver_run_frame → WsApplyCull
GameDrawPpuFrame:  WsFrameCtxBuild → WsOamFix → WsRadarUpdate → WsMarginFrameBegin
                   → WsApplyHud → snes_beam_frame_driver_draw_ppu_frame → WsMarginFrameEnd
```

The game runs the whole frame first; the draw then replays the frame's **raster journal** line by line. The journal (`raster_reg_journaled()` in `game/snesrecomp/runner/src/snes/ppu.c`) records every PPU register `$2100-$2133` and `$420C` **except** OAMDATA `$2104`, the VRAM port and the CGRAM port. Two consequences drive this design:

- BG1 scroll, BG1 map base (`$2107`) and BG mode **are** replayed per line, so the margin line hook (`g_snes_beam_line_hook`) sees each half's own BG1 state on its lines.
- OAM is **not** replayed per line: the draw uses one OAM/high-table state per frame (`ppu->oam`, `ppu->highOam` as `WsOamFix` sees them). A per-line fix cannot replay the game's OAMDATA writes, but the existing beam line hook **can** write the high table for the line from the staged entries and WRAM. That is how the bottom half gets its own big size bit: the single end-of-frame high table has the bottom entries staged small, which is correct for the top half and wrong for the bottom, and the game's mid-frame high-table writes are not journaled.

The sprite pass evaluates OAM per line (`ppu_evaluateSprites`), but from that single state.

### Measured 2P state

A 2P race is reached with the main menu `2P` entry: `PICK A PLAYER` (P1), then a second player screen, `PICK TOUR`, `PICK TRACK`, `NOW PLAYING`, race. P1 uses pad 1, P2 pad 2; both move their rider during the race. `VS` uses the same split-screen presentation and must be measured separately.

| Word | Address | 1P race | 2P race | Notes |
|---|---|---|---|---|
| race mode | `$00B4` | `1` / `$0B` | `0` | not a 2P race signal |
| two player | `$0DDB` | `0` | `1` | set with the split-screen race |
| pause/finish | `$0EE9` | `0` | `0` | `1` in pause/finish |
| screen mode | `$0300` | `0`/`1` | `0`/`1` | large (`$4C`, `$21EC`, ...) in menus |
| cull left | `$0421` | varies | `$FCF0` (`-784`) | 2P uses zoom shift 4 |
| cull right | `$0423` | varies | `$1000` (`4096`) | divided by 16 = `[-49,256]` |
| cull zoom | `$03ED` | `0`/`2` | `4` | the cull margin is `WsExtra() << $03ED` |
| P1 rider X/Y | `$0411`/`$0415` | player | player | |
| P2 rider X/Y | `$0413`/`$0417` | CPU ghost | player 2 | |
| P1 camera X/Y | `$0419`/`$041D` | race camera | top half | |
| P2 camera X/Y | `$041B`/`$041F` | not used | bottom half | |

`variables.h` only names `$0419`/`$041D` (`kRamCamX`/`kRamCamY`). Add `kRamCam2X = 0x041B` and `kRamCam2Y = 0x041F` (bottom-half camera) and keep the comments of `kRamRider2X/Y` accurate ("CPU rival in 1P, player 2 in 2P").

Example after moving P1 only (P2 idle):

```text
P1: R1X=3867 R1Y=1498  Cam1X=3775 Cam1Y=1465  -> top    relX=92  relY=33
P2: R2X=1776 R2Y=681   Cam2X=1672 Cam2Y=643   -> bottom relX=104 relY=38 (+112 = 150)
```

The split is horizontal at line 112. Each half has its own BG1 scroll and its own HUD copy. **Measured on Looper (2P):** the PPU write log (`.ppuw.tsv`) of one frame shows `4953 0 2107 0C hdma` and `4953 114 2107 1C hdma`. `$2107` is BG1SC, so HDMA sets the BG1 map base to `0x0C00` on line 0 (top half) and to `0x1C00` on line 114 (bottom half). The bottom half is BG1 with its own map base, not another layer, and its BG1 state takes effect on line 114; the divider is lines 112..113. Task 1.3 re-verifies this on other tracks and on VS and records the BG1 scroll of each half against its camera.

### Measured BG3 HUD layout

The HUD is entirely BG3. Isolating BG3 (`SNESRECOMP_LAYER_MASK=4`) on a 2P race gives the glyph boxes:

```text
            0 ───────────────────────────── 256
            │  RACE (17..46)       timer (193..238) │  y 11..21
            │       centre prompt (81..174)         │  y 43..53
            │                              MIKE (208..238) │  y 91..101
            ├───────────────────────────────────────┤  y 112..113 (divider, full width)
            │  RACE (17..46)       timer (193..238) │  y 123..133
            │       centre prompt (81..174)         │  y 155..165
            │                            MARTIN (192..238) │  y 203..213
        224 ─────────────────────────────
```

The two halves are identical except for the +112 Y offset and the name text (the name is the chosen rider, so its width varies; `MARTIN` is the widest seen, starting at x=192). The centred prompt is at x≈128 in both; a symmetric widening keeps it centred. No BG3 content was seen in columns 0..16 or 239..255.

### Rider OAM path

The rider staging routine is `$82:ACA5` (disassemble with `tools/scripts/ws-leading-edge/dis.py`). It computes **four** entries in 2P, one per player per half:

```text
entry 0 ($1501-$1504) = P2 bottom: (R2X-Cam2X, R2Y-Cam2Y+$70)
entry 1 ($1505-$1508) = P1 bottom: (R1X-Cam2X, R1Y-Cam2Y+$70)
entry 2 ($1509-$150C) = P1 top:    (R1X-Cam1X, R1Y-Cam1Y)
entry 3 ($150D-$1510) = P2 top:    (R2X-Cam1X, R2Y-Cam1Y)
```

They map to OAM slots 96..99 in the same order. The uploader at `$82:D2DC` copies 16 bytes from `$1501-$1510` to OAM and writes the high-table byte from `$1599` (slots 96..99 share `highOam[24]`; per slot, bit `2*(slot&3)` is X bit 8 and the next bit is the size). The cull uses the shared `$0421`/`$0423` window, scaled by `$03ED`, for all four entries. The vertical window is `[-41,112)` relative to the entry's camera; bottom entries add `$70` to Y when staged.

The game sets the X high bit only when the scaled X is negative (the `BPL` branches at `$AD42`/`$AF9E`/`$B025`). A real rider with positive screen X in `[256, 256+E)` is uploaded with only the low byte, so the widened renderer (`PpuDecodeOamX`: raw 9-bit `v` → `v` if `v < 256+E`, else `v-512`) draws it at `X - 256`. This is the 1P bug fixed by `WsFixRiderOamXHigh`; in 2P it must be fixed per slot with the camera of that slot's half.

Observed at one frame where P1 is close to its camera and P2 far from P1's camera (native 4:3):

```text
slot 98  x=117 y=42   P1 top real
slot 99  x=112 y=112  P2 top culled -> sentinel ($70,$70)
slot 96  x=105 y=168  P2 bottom real
slot 97  x=48  y=48   P1 bottom culled -> sentinel ($30,$30)
```

**The high-table capture is unreliable.** The same capture read `$1599 = $A5` (staging), `$20A2 = $A5`, `$20A3 = $5A`, and `0x55` in the end-of-frame OAM high table. Neither value is consistent with what was on screen: `$A5` gives slot 96 (a visible real rider at x=$68) X bit 8 = 1, which would put it at -152; `0x55` gives all four slots X bit 8 = 1. `$A5`/`$5A` being bit-swapped copies suggests the game alternates the byte between frames, and staging (`$1501..`, `$1599`) is the next frame's data compared with the current frame's upload (the same one-frame lag the 1P fix tolerates). It is **not** evidence of a mid-frame OAM rewrite: the port's draw cannot replay one (see "Frame sequence"), and native 2P draws correctly in the port. Task 1.4 reads `ppu->highOam[24]` exactly where `WsOamFix` runs, over consecutive frames, and records the sentinel high bits. Until then the design does not assume any particular sentinel high bit.

Why the 1P fix cannot be reused unchanged:

- it hides slots 96/97 as the "parked 1P 2P pair"; in 2P they are the bottom-half entries;
- it matches slots 98/99 to `R1/R2` with the single 1P camera;
- the 2P sentinels are `($70,$70)` top and `($30,$30)` bottom, not `($170,$70)`;
- bottom-half Y is `relY + $70`.

### BG1 margins: why the current code is single-viewport

`ws_ring.c`, `ws_source.c` and `ws_margin.c` hold one global state:

- `WsRingLine()` latches the BG1 map base and BG mode **once**, at `WS_RING_LINE_FIRST` (line 1). A change of either resets the scene (seen table and scroll unwrap) and bumps the scene generation, which drops the anchor.
- `WsRingLineScroll()` computes a line's absolute scroll as the line-1 unwrapped base plus `WsWrapDelta10(line raw, line-1 raw)`. `WsWrapDelta10` folds the delta into `[-512, 512)`. That handles small per-line scroll changes, not a second camera: the bottom camera can be thousands of pixels from the top one, so its cells get wrong world coordinates (aliased modulo 1024 px).
- The seen table `s_seen[32][32]` is keyed by ring slot. Bottom-half observations with wrong world coordinates overwrite top-half entries and pollute both the margin decision and the anchor evidence.
- The tile hook's map check (`(sc & 0x7FFF) != WsRingMapAddr(cx, cy)` → leave the tile alone) uses the line-1 map base. If the bottom half uses another map base, every bottom margin sample fails the check and is drawn as the raw ring: exactly the wrong leading-edge cells `source` exists to prevent.
- The "world" coordinate is the unwrapped scroll, whose origin is the raw scroll at the scene start, not an absolute world position. The anchor (`table cell = world cell + anchor`) absorbs that origin. Two viewports have two unrelated origins, so **one anchor cannot serve both halves**.

A 32×32 ring of 16×16 cells is 512×512 px. Two independent cameras cannot share it without overwriting each other's cells, so the game gives the bottom half its own map: BG1 base `0x1C00` from line 114, against `0x0C00` above (measured on Looper, see above). With the current code, line 1 latches `0x0C00`, every bottom-half margin sample fails the map check against it, and the bottom half observes the wrong map. The fix is per-viewport state.

## Goals / Non-Goals

**Goals:**

- Recognize 2P and VS split-screen races as live races without changing 1P.
- Widen each half symmetrically by the same `E` used by 1P; keep height 224 and the split at 112.
- Apply the same aspect/max-width settings: 16:9, 21:9 and 512 px.
- Make both halves' BG1 margins, HUD, names, divider and the four rider entries correct.
- Keep the only guest write as the existing cull window; OAM edits stay in the PPU copy; HUD and margins stay in PPU configuration and renderer hooks.
- Keep 1P output byte-identical to the current references, and 4:3 and all menus identical to the native baseline.
- Validate both halves at multiple widths and on multiple tracks.
- Leave a durable `docs/TWO_PLAYER.md`.

**Non-Goals:**

- Placing the two halves side by side (W4's "more ambitious" option).
- A 2P radar, two radars, or sharing the radar extra-object slot.
- Online/netplay 2P.
- Changing the game camera, simulation, RNG, input or collision.
- A new user option: the existing `Widescreen`, `Hud`, `Margin` and `Cull` switches apply.
- Wider than 512 px; the 9-bit OAM X cap is unchanged.
- A framework patch.

## Decisions

### D1 — Live-race predicate: `$0DDB`, not `$00B4`

```c
int WsInLiveRace(void)
{
    if (g_ram[kRamPauseFinish] != 0 || g_ram[kRamScreenMode] > 1) return 0;
    if (g_ram[kRamTwoPlayer] != 0) return 1;       /* 2P/VS split screen */
    return g_ram[kRamRaceMode] != 0;               /* 1P: 1 or 0x0B */
}
```

This is behaviour-identical for 1P (`$0DDB == 0`). `WsInLiveRace()` stays the single gate; `PrepareFrame()`, `WsApplyCull()`, `WsFrameCtxBuild()` and the margin/HUD/OAM modules pick it up unchanged.

Task 1.1 must confirm on the trace: `$0DDB` is 0 on every menu, PICK, tour/track screen and in the attract/demo; `$0DDB` returns to 0, or `$0300` leaves `<= 1`, when a 2P race ends. Record whether 2P NOW PLAYING satisfies the predicate. 1P NOW PLAYING can already satisfy it (see the comment in `WsRingLine`: the ring's scene reset keeps its cells out of the race); 2P must behave exactly like 1P there, with no extra guard unless the capture shows a visible difference. If VS uses another flag, add it to the same branch.

*Alternative rejected:* widening whenever `$0300 <= 1` regardless of mode. It would widen the attract/demo and any other split screen.

### D2 — Frame geometry: widen each half, keep the split

The frame becomes `E + 256 + E` wide and stays 224 high; `E = WsExtra()` (16:9 = 43, 21:9 = 96, 512 px = 128). The split stays at 112.

```text
              -E          0          256       256+E
             ┌────────────┬───────────┬───────────┐
  P1 top     │   margin   │  native   │   margin  │  y 0..111
             ├────────────┼───────────┼───────────┤  y 112..113 divider
  P2 bottom  │   margin   │  native   │   margin  │  y 114..223
             └────────────┴───────────┴───────────┘
```

`main.c::PrepareFrame()` needs no change. The 512-px cap stays because the OAM X decode window is `[E-256, 256+E)`; with `E > 128` a left-margin sprite aliases into the right margin.

*Alternative rejected:* stretching each 256-px half horizontally. It distorts glyphs and the track.

### D3 — BG1 margins: per-viewport ring, unwrap, seen table and anchor

No new margin policy; the `source` cascade (map check → ring-verified → table → mask) and `clamp` stay as they are. What changes is that ring/source state exists **per viewport**.

**Viewports.** Viewport 0 is the full frame in 1P and the top half in 2P. Viewport 1 exists only in 2P and is the bottom half. Add to `ws_frame.c`:

```c
enum { WS_VP_MAX = 2 };
/* Viewport of a scanline: 0 in 1P; in 2P 0 above the split and 1 below it,
 * -1 on the divider lines that belong to neither. */
int WsViewportOfLine(int two_player, int line);
```

The line constants are named in `ws_frame.h`: top `[1, 112)`, divider `[112, 114)`, bottom `[114, 224)`. The bottom value 114 is measured (the HDMA write of BG1SC on line 114 on Looper); task 1.3 re-verifies it on other tracks and on VS. The bottom viewport starts on the first line with the bottom half's BG1 state (114), not on the nominal split (112). `two_player` comes from `WsFrameCtx`; the margin module stores it in `WsMarginFrameBegin` for its hooks.

**`ws_ring.c`.** Move every static into

```c
typedef struct {
    unsigned scene_generation;
    int scene_valid, scroll_valid, frame_active;
    uint16_t prev_map_base, map_base;
    uint8_t prev_bgmode;
    uint16_t prev_h, prev_v, frame_raw_h, frame_raw_v;
    int32_t unwrapped_h, unwrapped_v, base_h, base_v;
    WsRingSeen seen[WS_RING_CELLS][WS_RING_CELLS];
} WsRingVp;
static WsRingVp s_vp[WS_VP_MAX];
```

and make the per-frame start happen on each viewport's **own first line**: `WsRingLine(ppu, line, vp)` runs the scene check, scroll unwrap and map-base latch when `line` is that viewport's first line, and observes the line otherwise. Accessors take the viewport: `WsRingLineScroll(ppu, vp, &h, &v)`, `WsRingMapAddr(vp, cx, cy)`, `WsRingSeenCell(vp, cy, cx)`, `WsRingSceneGeneration(vp)`, `WsRingFrameActive(vp)`. `WsRingObserve` already handles one line; it now writes into the viewport's table. Keep `WsRingScrollValid/H/V()` as viewport-0 wrappers (the radar uses them).

**`ws_source.c`.** Move `s_src_valid`, `s_src_ax/ay`, `s_src_generation` and `s_src_race_frame0` into a per-viewport struct. The grid header and the piece-library word index (`s_src_word_present`, `s_src_present_end`) are track data and stay shared. `WsSrcSamples`, `WsSrcSearch` and the self-check read the viewport's seen table. `WsSourceFrame()` runs for each viewport that was active this frame; `WsSourceAgree(vp, wx, wy, &w)` uses the viewport's anchors. Keep `WsSourceValid()`, `WsSourceAnchorX/Y(i)` as viewport-0 wrappers so `ws_radar.c` is unchanged. Log lines gain a `vp=` field.

**`ws_margin.c`.**

- `WsMarginLine`: `vp = WsViewportOfLine(two_player, line)`; skip `vp < 0`; the "line 1" block (blank-character search, `s_logged` reset, announcement) stays at line 1 for the whole frame; then `WsRingLine(ppu, line, vp)`.
- `WsMarginTile`: `vp = WsViewportOfLine(two_player, line)`; return `tile` if `vp < 0` or the viewport is not active; otherwise the existing cascade with the viewport's scroll, map address, seen cell and anchors. Key `s_logged` by viewport too.
- `WsMarginFrameEnd`: `WsSourceFrame()` per active viewport, then end both ring frames.
- Add `SNESRECOMP_WS_2P_LINE_LOG=<path>`: per drawn line in 2P, `frame,line,vp,map_base,bgmode,h,v,main_screen_layers`. It is the evidence for task 1.3 and stays as a diagnostic.

**1P invariance.** In 1P `WsViewportOfLine` always returns 0, viewport 0's first line is `WS_RING_LINE_FIRST`, and every computation is the current one. The 1P validation references must not change.

**Clamp.** `PpuSetWidescreenLayerClamp(ppu, 1u << 0)` clamps BG1 for all lines, which is already correct for both halves.

**Contingencies from task 1.3:**

- *Bottom half on BG1 with its own map base (measured on Looper: `0x1C00` from line 114):* the design above as written.
- *Bottom half on the same map base (not expected after the Looper measurement):* still per-viewport unwrap, seen table and anchor; the two viewports will overwrite each other's ring slots, which the "slot still holds this world cell and word" check already detects. Record how often cells are masked.
- *Bottom half on another layer (low priority: ruled out on Looper):* the margin tile hook and the clamp must also cover that layer on the bottom lines. Stop and record the finding in `report.md` before writing that code; it changes the hook's `layer != 0` filter and the clamp mask.

*Alternative rejected:* one shared anchor keyed by the WRAM cameras (`world = camera + screen`). It could work if the scroll registers equal the cameras modulo 1024, but that is unmeasured and would change the 1P coordinate system the validated anchor depends on. Per-viewport state reuses the validated code as is.

### D4 — HUD: three elastic bands, no clamp band

Two facts of the framework (`game/snesrecomp/runner/src/snes/ppu.h`) constrain the HUD:

- **Precedence.** `PpuWidescreenLayerElasticBandAt` states that the clamp band, layer mask, mirror/repeat/stretch and the BG3-widen gate all **win over** an elastic band. A clamp band covering the elastic lines disables them. 1P works only because its elastic band (17..32) and clamp band (32..224) do not overlap.
- **Uncovered columns.** `PpuMergeElasticBackground` draws only the destination columns its segments cover ("a band can express a clamp"). An elastic band whose segments cover the native content and nothing else leaves the margins empty.
- **BG3 widen gate.** `PpuWidescreenLayerExtra` widens BG3 only on lines `>= wsBg3WidenY` (0 disables BG3 widening). With the 1P value 17, lines 11..16 of the top header would not widen.

So the 2P path in `WsApplyHud` is:

```text
PpuSetWidescreenLayerClampBand(ppu, 2, 0, 0)      clamp band off
PpuSetWidescreenBg3Widen(ppu, 10)                 lines 0..9 stay native (clamped)
slot 0: BG3 lines  10..112  top half      segs T
slot 1: BG3 lines 112..114  divider       segs D
slot 2: BG3 lines 114..224  bottom half   segs T
```

with `E = ctx->extra` and

```text
T = { { 17,  64,   4-E,  51-E },     RACE group 1:1, 4 px inside the left edge
      { 64, 192,   64,   192  },     centre prompts in place (identity)
      {192, 256, 192+E, 256+E } }    timer and player name 1:1 at the right edge
D = { {  0, 256,   -E,  256+E } }    divider stretched; nearest-neighbour is invisible on a flat run
```

The top/bottom line ranges are confirmed in task 4.1 against the divider rows measured in task 1.3 (`PpuSetWidescreenLayerElasticBandSlot` takes `y0` inclusive, `y1` exclusive). Columns 0..16 of BG3 are dropped by `T`; the BG3-only capture shows nothing there, and task 4.3 checks it on other tracks and names.

The timer segment starts at 192 instead of the 1P 193 so that `MARTIN` (192..238) and the timer share one segment; one pixel column of empty BG3 moves with it.

The 1P path is unchanged except that it now also clears slots 1 and 2 (`PpuSetWidescreenLayerElasticBandSlot(ppu, s, 2, 0, 0, NULL, 0)`), and the disabled path clears slots 0..2. Otherwise a 2P band would survive into a 1P race.

*Alternative rejected:* the first draft's clamp band `BG3[10..224]` plus five elastic bands. The clamp band wins, so nothing would move, and `Bg3Widen(17)` would block lines 10..16.

*Alternative rejected:* anchor bands (`kPpuWsAnchorBands`). They cannot express a divider stretch.

### D5 — OAM: four rider slots, per-half cameras, frame plus per-line high table

`WsFixRiderOamXHigh` keeps its 1P body. Its `two_player` early return becomes a call to a new `WsFixRiderOamXHigh2P(ppu)` with the same gates (wide, live, cull on, `E > 0`, `SNESRECOMP_WS_OAM_XHIGH` on). The 1P parked-pair and `$170,$70` rules never run in 2P.

Slot table (verified in task 1.4, including on VS and a second track):

| Slot | Entry | Rider X/Y | Camera X/Y | Y offset | Sentinel `(lo, y)` |
|---|---|---|---|---|---|
| 96 | P2 bottom | `$0413/$0417` | `$041B/$041F` | `+$70` | `($30,$30)` (seen on 97; confirm) |
| 97 | P1 bottom | `$0411/$0415` | `$041B/$041F` | `+$70` | `($30,$30)` (seen) |
| 98 | P1 top | `$0411/$0415` | `$0419/$041D` | 0 | `($70,$70)` (seen on 99; confirm) |
| 99 | P2 top | `$0413/$0417` | `$0419/$041D` | 0 | `($70,$70)` (seen) |

For each slot, in order:

1. **Sentinel.** If the staged `(lo, y)` equals the half's sentinel (and the high bit equals the value measured in task 1.4), move the slot below the field (`WsOamSlotHide`) and log `hide`. This does not consult WRAM. It is safe because the sentinel Y cannot belong to a staged real rider: the vertical window is `[-41, 112)` relative to the camera, so a real top entry never has Y = 112 (`$70`), and a real bottom entry has `Y = relY + 112 >= 71`, never 48 (`$30`). Task 1.4 confirms the window. Hiding is required at some widths: a sentinel with the high bit set decodes as `256 + lo` when that is `< 256 + E` (`$130` = 304 is inside the right margin from `E >= 49`, `$170` = 368 from `E >= 113`), and as `lo - 256` (-208 / -144) otherwise, which overlaps the left margin at large `E`.
2. **High bit already set.** The game sets it for a negative X, so normally leave the slot (left margin or off-screen). Exception, only if task 1.4 shows it happens: a real rider whose WRAM-derived `rel` is positive and clearly prefers the decoded value `lo` over the high-bit value (`lo + 256` when `lo < E`, `lo - 256` otherwise) was uploaded with the bit set by mistake; then clear the bit and log `xhigh-clear`. Without that evidence, do not implement the exception; just log such slots as `hi-positive`.
3. **No high bit, `lo >= E`.** Leave the slot: it decodes to `lo`, the native position.
4. **Missing high bit.** `rel = (int16_t)(riderX - camX)` with the slot's rider and camera. If `rel` is in `[256 - 32, 256 + E + 32)` and `abs(lo + 256 - rel) < abs(lo - rel)`, set the high bit (`WsOamSlotSetHigh`). This is the 1P rule with the same `WS_OAM_XHIGH_SLACK` one-frame tolerance.
5. Otherwise leave the slot unchanged.

A slot is only touched if it looks like a rider entry: tile and attribute bytes match the patterns measured in task 1.4 (seen: P1 tile `$00`, attributes `$26`/`$66`; P2 tile `$88`, attributes `$28`). Unknown contents are left alone and logged as `skip`.

**Per-line high table.** The end-of-frame high byte has the top entries big
(`size=1`) and the bottom entries small (`size=0`); the retail game writes the
bottom high byte during the frame, and the framework's draw does not replay
OAMDATA. Playtest on Switcher showed the bottom half with no rider for that
reason. The existing beam line hook now calls `WsOamLineFix(ppu, line)` before
each line:

- top lines (1..113): slots 98/99, size 1, X high from the true WRAM X;
- bottom lines (114..223): slots 96/97, size 1, X high from the true WRAM X.

It is installed for any live 2P/VS, wide or native 4:3; in native it also
hides the half's sentinel. Only `highOam[24]` is written, and only for slots
that still look like rider entries (a hidden sentinel is skipped). This is a synthesis from the staged
entries and WRAM, not a replay of the game's OAMDATA writes. The frame-level
`WsFixRiderOamXHigh2P` still hides the sentinels and fixes the missing X bit;
the per-line fix overrides the high/size bits for the line's half.

A rider visible in the *other* half is not a ghost: if P2 is near P1 in the world, slot 99 is P2's real sprite in the top half and gets the same treatment as any real rider.

Logging: extend `SNESRECOMP_WS_PARK_LOG` with a `vp` column, or add `SNESRECOMP_WS_2P_OAM_LOG` with `frame,slot,half,lo,y,hi,tile,attr,rel,rel_y,E,action` (`hide`, `xhigh`, `xhigh-clear`, `hi-positive`, `keep`, `skip`). The per-line high/size write is not logged per action; the stage log remains the raw evidence.

*Rejected from the first draft:* replaying the game's OAM writes per line. The
draw has one OAM state per frame, so a replay would see the same data. The
implemented per-line hook is not a replay: it synthesises the high/size bits
for the line's half from the staged entries and WRAM.

*Alternative rejected:* keeping the cull native in 2P. Riders would pop at the 256-px edge.

*Alternative rejected:* the framework's strict right-hint mode (`PpuWsSetOamRightHints`) alone. It does not add the missing bit for a real rider whose positive X was truncated to 8 bits.

### D6 — Cull: no new formula

`WsApplyCull()` already scales by `$03ED` and writes `$0421`/`$0423`. In 2P, `$03ED = 4` and the base is `[-784, 4096]`, so `extra = E << 4` gives `[-49-E, 256+E]` in screen pixels (E = 43: `[-92, 299]`) for all four entries. Task 6.1 checks that the game does not install a different cull pair for the bottom half within a frame; the existing "base reuse / new pair" logic already handles reloads.

### D7 — Gating, settings and radar

- `[Graphics] Widescreen`, `[Widescreen] Hud`, `[Widescreen] Margin` and `[Widescreen] Cull` apply unchanged; no new menu row or key.
- `[Video] MiniMap` stays 1P-only: `WsRadarUpdate` already requires `!ctx->two_player`.
- 4:3 and every non-live screen stay on the 256-px path because `WsInLiveRace()` or `WsWideEnabled()` is false.
- The in-progress change `uniracers-rival-edge-indicator` also hides in 2P; if it lands first, keep its 2P gate.

### D8 — Validation matrix

New routes under `game/tools/`, one per mode, that reach a live race and then drive both pads (the script syntax supports `p1:`/`p2:` prefixes; see the investigation log). They must wait long enough for slow menus; prefer the existing route style with measured waits, and record the frame at which `$0DDB` becomes 1.

New `validate.sh` cases (tags):

```text
2p-native   widescreen off                       must equal the native 2P reference
2p-16x9     SNESRECOMP_WS=1 ASPECT=16:9           342×224
2p-21x9     ASPECT=21:9                           448×224
2p-512      ASPECT=32:9                           512×224
2p-clamp    ASPECT=16:9 OFFWINDOW=clamp
vs-16x9     VS route, ASPECT=16:9
```

The existing 1P cases must keep their current hashes.

For each wide case, in `report.md`:

- BG1 probe score per viewport: zero wrong served cells in the left and right margin of each half; masked counts and anchor latency per viewport.
- BG3-only and composite captures: `RACE`/timer at the widened edges, names at the right edge, divider full width, prompts centred, glyphs byte-identical.
- OAM log: real riders in both margins of both halves, no sentinel or ghost visible, `xhigh` actions only for real riders.
- Guest state: WRAM/VRAM/CGRAM identical with the wide path on/off except the cull words; SRAM unchanged.
- Menus, NOW PLAYING, countdown, pause and finish native.
- Tracks: the tour-1 tracks used by 1P validation, plus Zoom Zoo (non-zero cull zoom in 1P; record `$03ED` in 2P) and a looping track (Switcher or Monster).

### D9 — Documentation

`docs/TWO_PLAYER.md` is written last and is the durable explanation: detection, layout, per-viewport BG1, HUD bands, the four-slot OAM policy, cull, what was measured and how, decisions and rejected alternatives (including the first-draft errors listed below), and limits. `docs/WIDESCREEN.md` and `docs/TRACK_MARGIN.md` link to it; `docs/README.md` lists it and drops "Two-player split screen is not adapted to widescreen". It must not contain ROM-derived data and must pass `tools/scripts/docs/check_links.py`.

## Corrections to the first draft

The first version of this design was reviewed against the code. These claims were wrong and are fixed above; the implementer should not reintroduce them:

1. "The ring model already supports two vertical viewports with different scrolls; BG1 is verify-only." False: one map base per frame, a ±512 px per-line scroll delta, one seen table and one anchor (D3).
2. "One anchor per race serves both halves because the track table is world-based." False: the port's world coordinate is the unwrapped scroll, whose origin is per viewport (D3).
3. "The elastic band wins over the clamp band." False: the clamp band wins (D4).
4. "Lines 0..9 stay native through `wsBg3WidenY = 17`." False: 17 blocks lines 0..16, including the top header (D4).
5. "The OAM high table may change between the halves; if so, fix OAM per line." Only half-true: the game's mid-frame OAM writes are not journaled, so they cannot be replayed. The port does need a per-line high/size synthesis for the bottom half, because the single high table stages the bottom entries small; playtest showed the bottom rider missing until it was added (Frame sequence, D5).
6. Sentinels were described without their high bit; whether and where they show depends on it (D5).

## Risks / Trade-offs

- **BG1 layout differs on another track or in VS** → Looper shows BG1 base `0x0C00`/`0x1C00` switched by HDMA on line 114; task 1.3 re-verifies other tracks and VS, and D3's contingencies cover a difference.
- **Ring slots shared by both viewports** → the existing "same world cell and word" check prevents wrong cells; the cost is more masked cells. Record the masked count per viewport.
- **Anchor search cost doubles** → `WsSrcSearch` runs per viewport only while that viewport has no anchor; record `search_ms` per viewport.
- **Slot roles vary by mode/track** → rider-pattern check before editing a slot; verify on a second track and on VS.
- **HUD labels vary** → `T` covers columns 17..255; re-measure if a wider name or label appears.
- **1P regression** → every 2P branch is behind `two_player`; 1P references must be byte-identical.
- **Very wide halves at 21:9/512** → requested behaviour; side-by-side composition is a separate future change.

## Migration Plan

No data migration and no new persistent setting. Reverting the predicate returns 2P to the 256-px path; the per-viewport ring/source refactor is inert in 1P.

## Open Questions

Each is answered by a task in section 1 and recorded in `report.md`:

- Is the Looper layout (BG1 base `0x0C00` on lines 0..113, `0x1C00` from line 114, set by HDMA) the same on other tracks and in VS? How does each half's BG1 scroll relate to its camera? (1.3)
- What are the high bits of the 2P sentinels at upload, and is the vertical window `[-41,112)`? Does the game ever upload a real rider with a positive X and the high bit set? This is the main open point. (1.4)
- Does VS set `$0DDB` and use the same staging, slots and HUD rows? (1.5)
- Are the HUD rows stable across tours, tracks and rider names? (4.3)

## Investigation log

How the first measurements were taken, so they can be reproduced. Captures contain ROM-derived pixels and stay outside the repository (`docs/VALIDATION.md`).

### Tooling

- Release binary: `game/build-local/UniracersSNESRecomp`.
- Trace build: `game/build-trace/UniracersSNESRecomp` (`SNESRECOMP_ENABLE_TRACE=ON`); debug server (`SNESRECOMP_DEBUG_PORT`) commands `oam_write_get`, `oam_render_get`, `oam_state`, `ppu_lines [first last]` (per-line scroll, BG mode, windows and screen enables, but not the BG map base; use the new `SNESRECOMP_WS_2P_LINE_LOG` for that).
- Screenshots: `SNESRECOMP_SCREENSHOT`, `SNESRECOMP_SCREENSHOT_FRAME`, or `SNESRECOMP_SCREENSHOT_DIR` with `_FROM`/`_TO`.
- Layer isolation: `SNESRECOMP_LAYER_MASK=4` is BG3 and `16` is OBJ.
- State dump: script command `dump <tag>` with `SNESRECOMP_DUMP_DIR=<dir>` writes `.wram.bin`, `.vram.bin`, `.oam.bin`, `.cgram.bin`, `.sram.bin`, `.fb.bmp` and register journals.
- Disassembly: `python3 tools/scripts/ws-leading-edge/dis.py "rom/Uniracers (USA).sfc" 82:ACA5 200 m1x1`.
- BG1 probe: `SNESRECOMP_BGPROBE_FRAME`, `SNESRECOMP_BGPROBE_LAYERS`, `SNESRECOMP_BGPROBE_FILE`, scored by `tools/scripts/ws-offwindow/world_key_check.py`. The scorer assumes one viewport; it needs a per-half mode (split the probe rows at the bottom viewport's first line and score each half against its own camera).
- Existing logs: `SNESRECOMP_WS_OFFWINDOW_LOG`, `SNESRECOMP_WS_SOURCE_LOG`, `SNESRECOMP_WS_PARK_LOG`, `SNESRECOMP_WS_OAM_XHIGH_LOG`.

### Reaching 2P

The menu order is `1P / 2P / VS / LEAGUE / OPTIONS`. From the title screen press `down` once, then `b`; P1 picks on `PICK A PLAYER`; the second player screen needs P2 input (`p2:down`/`p2:b`); then the tour and track screens use `b`. The exploratory route that reached a race:

```text
wait 640
press down 2
wait 60
press b 2
wait 400
press b 2
wait 500
press p2:down 2
wait 60
press p2:b 2
wait 400
press p1:down 2
wait 60
press p1:b 2
wait 400
press p1:b 2
wait 400
press p1:b 2
wait 400
press p1:b 2
wait 400
press p1:b 2
wait 600
press right+p2:right 3000
```

It selects `SHUFFLER`/`LOOPER` and is not yet a per-track route.

### Forcing the wide path for inspection

Before the predicate existed, a 2P race was forced wide with the script command `forcepoke 00b4 01` after the race started, with `SNESRECOMP_WS=1` and `SNESRECOMP_WS_ASPECT=16:9`. Debug only; it must not appear in the implementation. It produced the 342×224 frame and the broken HUD/OAM captures described above.

### OAM/staging capture at one frame

```text
P1  R1X=3520 R1Y=1628 Cam1X=3402 Cam1Y=1586 -> top    relX=118 relY=42
P2  R2X=934  R2Y=532  Cam2X=830  Cam2Y=476  -> bottom relX=104 relY=56 (+112=168)

staging $1501-$1510:
  68 A8 88 28   P2 bottom  (slot 96)
  30 30 00 26   P1 bottom  sentinel $30,$30 (slot 97)
  76 2A 00 66   P1 top     (slot 98)
  70 70 88 28   P2 top     sentinel $70,$70 (slot 99)

$1599 = A5   $20A2 = A5   $20A3 = 5A
end-of-frame OAM high table for slots 96..99 = 0x55
```

See "Rider OAM path" for why these high-table values are not usable as they stand.

### BG3-only measurements

`SNESRECOMP_LAYER_MASK=4` at a 2P race gave the glyph boxes in "Measured BG3 HUD layout", from connected components of non-backdrop pixels. The top and bottom copies differ by exactly 112 lines.

### Temporary workspace

The exploratory captures were written under `/tmp/opencode/2p-explore` and are not part of the repository; this appendix, `report.md` and `docs/TWO_PLAYER.md` are the durable record.
