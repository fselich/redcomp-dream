# Widescreen

The game draws a 256×224 picture. With widescreen on, a live race is drawn wider, from 342 px (16:9) up to 512 px, with real content in the extra columns. Menus, the title screen and NOW PLAYING stay at 256 px. Every widescreen feature is presentation-only except the cull window (below), which is the one module that writes guest WRAM.

Options are listed in [`CONFIG.md`](CONFIG.md); the module layout is in [`ARCHITECTURE.md`](ARCHITECTURE.md).

## When it applies

`WsWideEnabled()` (option `[Graphics] Widescreen`) and `WsInLiveRace()` must both hold. A live race is `$00B4 != 0` (race mode; 1 on tour 1, `$0B` on later tours), `$0EE9 == 0` (not paused or finished) and `$0300 <= 1` (not a menu screen). The predicates are in `game/src/ws_frame.c`.

## Frame width

`game/src/main.c` (`PrepareFrame`) picks the race frame width each frame:

- the target aspect is `[Widescreen] Aspect`: `W:H`, a decimal, or `auto`, which follows the window and is never narrower than 16:9;
- the width comes from the framework's `SnesDisplayAspect_ComputeAdaptiveFrame` with the pixel aspect (`[Graphics] DisplayAspect`) and is capped by `[Widescreen] MaxWidth` (342..512, default 512).

The extra width per side is `E = (width - 256) / 2` (`WsExtra()`): 43 at 16:9, 96 at 448 px, 128 at 512 px.

**Why 512 px is the cap.** Sprite X is 9 bits. The renderer decodes it as `[E - 256, 256 + E)`, so with `E > 128` a sprite on the left margin aliases into the right one.

## Track (BG1)

The track layer is filled from the game's own track table, or clamped to 256 px. See [`TRACK_MARGIN.md`](TRACK_MARGIN.md).

## HUD (BG3)

`game/src/ws_hud.c`, option `[Widescreen] Hud`:

- lines 17–31 (RACE label, lap counter, timer) use an elastic band: the left group moves out by `E` and keeps a 4 px inset, the timer group moves right by `E`, both at 1:1 scale;
- lines 32–223 (prompts, arrows, lap times) keep the native clamp, so they stay where the game put them. Line 31 is the last row of the lap/timer glyphs, which is why the band ends at 32.

With the option off, BG3 is clamped to 256 px over the whole frame.

## Riders (cull window and OAM)

**Cull window** (`game/src/ws_cull.c`, option `[Widescreen] Cull`). The game culls riders whose screen X is outside the window `$0421`..`$0423` (left and right edge words). The port widens that window by `E` on each side (scaled by the zoom shift `$03ED`, 2 on Zoom Zoo) so riders stay visible in the margins, and restores the game's values when widescreen is off or the race ends. Those words are written only by the rider setup at `$81:A385` and read only by the five cull compares at `$82:AD19/ADE7/AEA7/AF75/AFFE`, so the change affects nothing else: spawn, AI, camera, collision and RNG never see it.

**OAM fixes** (`game/src/ws_oam_fix.c`, debug switch `SNESRECOMP_WS_OAM_XHIGH`). The rules below are the 1P ones; 2P/VS uses four slots and a per-line high/size pass, see [`TWO_PLAYER.md`](TWO_PLAYER.md). They edit only the PPU's uploaded OAM copy, never guest memory:

- *X high bit.* The game sets the OAM X high bit only for negative X; natively a positive X never reaches 256. With the widened cull a rider in `[256, 256 + E)` would decode as `X - 256` and appear on the left. The port sets the missing bit from the rider's true WRAM position.
- *Parked 2P pair.* In a 1P race slots 96/97 hold the unused 2P riders at a negative X with the high bit set. At `E ≥ 97` that value falls inside the right margin and shows a sliver; the port moves them below the field.
- *Culled-rider sentinel.* A culled rider (slot 98 player, 99 rival) is parked at `X = $170, Y = $70`. At `E = 128` it lands at +368 on the right; below `E = 112` it wraps to −144 on the left. The port always moves it below the field.

Evidence: `openspec/changes/archive/2026-10-22-uniracers-ws-ghost-xhigh/report.md` and `openspec/changes/archive/2026-10-23-uniracers-ws-right-edge-ghost/report.md`.

## Radar

`game/src/ws_radar.c`, option `[Video] MiniMap`. A 64×32 panel in the bottom-right margin shows a 128×64-cell window of the track table around the player (one panel pixel per 2×2 cells), with the player in white and the rival in red. It is a framework `PpuExtraObject`, not a sprite, so it writes no guest memory.

It needs the `source` anchor to place the window, so it appears only in a live 1P race with `Margin = source`, after the anchor is found (about 100 frames into the race), and never in netplay.

## Two-player split screen

A live 2P or VS race is detected by `$0DDB` (not `$00B4`) together with
`$0EE9 == 0` and `$0300 <= 1`. Each half is widened by the same `E`; the split
stays at line 112, with the divider on 112..113 and the bottom BG1 starting on
line 114. The bottom half uses its own BG1 map base (`0x1C00` on Looper,
against `0x0C00` above) and its own camera, so the ring observation, scroll
unwrap, seen table and source anchor are per viewport. The HUD uses three
elastic bands (top half, divider, bottom half) and no clamp band, with the
BG3 widen gate at line 10; the player name and timer share the right segment.
Riders use four OAM slots (96..99) with the per-half camera and the `+$70`
bottom offset. The radar stays 1P-only. Details and measurements are in
[`TWO_PLAYER.md`](TWO_PLAYER.md).

## Limits

- The radar is 1P only. The two-player OAM fix has a frame-level pass
  (sentinels and the missing X bit) and a per-line high-table pass that makes
  the bottom rider big; the framework's draw does not replay OAMDATA, so the
  per-line state is synthesised from the staged entries and WRAM.
- The cull window is the only guest write and it feeds only the cull compares, so turning widescreen on or off does not change how a race plays out.
