## Why

The game's existing rival indicator is a BG3 HUD element at two fixed
left/right positions (rows 112–126, columns 40–63 or 208–231). It only says
"ahead/behind" in the native 256-px view and never reaches the widescreen
border. On a wide frame, or on a track with vertical/looping sections, the
player cannot tell where the rival is without looking at the radar. The rival's
2D screen position is already available in WRAM, so a small presentation-only
border indicator can show direction and distance at any edge.

## What Changes

- **New border indicator.** When the CPU rival is outside the visible frame, a
  small arrow appears at the frame edge where the player→rival line crosses it,
  pointing outward. It covers all eight directions, not just left/right.
- **Distance cue.** 1–3 dots trail inward from the arrow, based on the rival's
  screen-space distance in frame widths. The thresholds are provisional and
  tuned during validation.
- **Widescreen-aware bounds.** The visible rectangle is the current frame
  (`-extraLeft .. 256+extraRight`, `0 .. 224`), so the indicator works in 4:3,
  16:9, 21:9 and 512 px. If the rival is visible in a wide margin, no arrow is
  drawn.
- **Coexists with the original indicator.** The game's BG3 `<<<`/`>>>` arrows
  stay untouched in this first version. The new arrow adds *position*; the
  original keeps its *ahead/behind* reading.
- **New option, on by default.** `[Widescreen] RivalIndicator = 1` (env
  `SNESRECOMP_WS_RIVAL`) and a menu row `RIVAL INDICATOR <ON>`.
- **Hidden where it does not apply.** No indicator outside a live 1P race, in
  2P split screen, or during netplay.
- **Presentation only.** Drawn as a `PpuExtraObject`, like the radar: no WRAM,
  VRAM, OAM or CGRAM writes.

## Capabilities

### New Capabilities

- `recomp-rival-indicator`: border position and distance arrow for the CPU
  rival, its viewport gating and its live setting.

### Modified Capabilities

- `recomp-ws-settings`: adds the `[Widescreen] RivalIndicator` option and its
  default.
- `recomp-ingame-options-menu`: adds the indicator row and the menu layout
  needed for one more option.

## Impact

- `game/src/ws_settings.c`/`.h`: new option descriptor.
- New `game/src/ws_rival.c`/`.h`: geometry, distance buckets and RGB15 arrow
  bitmaps.
- `game/src/game_rtl.c`: build and present the indicator object each frame.
- `game/src/ws_radar.c`: share the extra-object list with the new indicator;
  the radar must not overwrite it.
- `game/src/ws_menu.c`: new row; row height reduced so 12 settings plus
  Save/Close fit.
- `game/CMakeLists.txt`, `README.md`.
- No framework patch and no guest-state writes.
