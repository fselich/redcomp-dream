## Context

The port already has the pieces a local radar needs:

- the decoded track table in WRAM `$7F` (`docs/UNIRACERS_TRACK_FORMAT.md`);
- the live `source` anchor (`s_src_valid`, `s_src_ax/ay`, `s_src_cols/rows`,
  `WsSrcCell`) that maps a world cell to a table cell;
- rider positions `$0411/$0413` (X) and `$0415/$0417` (Y), camera
  `$0419/$041D`, zoom `$03ED`;
- a host-object compositor (`PpuExtraObject`) that can draw RGB15 pixels in
  the wide field without writing VRAM, OAM, WRAM or CGRAM.

The BG1 ring is only 32×32 cells (512×512 px), so a radar of the ring would
repeat the screen. This design uses a **128×64-cell window (2048×1024 px)**
sampled from the full `$7F` table, centred on the player, and rasterized into a
**64×32-pixel** RGB15 panel: 2×2 table cells per radar pixel.

```text
player table cell (px,py)
        │
        ▼
┌───────────────────────────────────────────────┐  128 cells = 2048 px
│                                               │
│                    · rival                    │
│                                               │
│                    ■ player (centre)          │
│                                               │
└───────────────────────────────────────────────┘
        ↓ 2×2 cells / pixel
   64×32 RGB15 panel  →  PpuExtraObject  →  wide frame
```

## Goals / Non-Goals

**Goals:**

- A readable local track shape around the player, covering about 2× the BG1
  ring / 3× the 16:9 width.
- Player and CPU rival dots at their true positions.
- Presentation-only: no guest memory writes; 4:3, WS-off and `MiniMap=0`
  byte-identical to the current build.
- A user-facing `[Video] MiniMap` switch in `config.ini`, default on.
- Fits in the bottom-right margin at 16:9 and scales to 21:9/512.

**Non-Goals:**

- A full-course map. Dragster/Switcher are 1024×16 cells; a 32-px box would be
  a line. A full map/progress strip is a separate change.
- 2P split screen, netplay, non-live screens.
- Alpha-blended UI, arbitrary host overlays, or a `draw_frame` presenter
  rewrite.
- Changing the track table, `source`, `ultrawide`, cull, OAM or HUD.
- A radar in modes where no table anchor exists (clamp/wrap); the window is
  only drawn when the anchor is live.

## Decisions

### D1 — Window and scale

- Radar panel: **64×32 pixels**.
- World window: **128×64 table cells** (2048×1024 px), centred on the player's
  table cell, clamped to the grid bounds (outside = background).
- Each radar pixel covers a **2×2 cell block**; it is track if any of the four
  cells has a non-zero BG1 character (`word & 0x3FF`). Palette-only fill cells
  are background.
- The player dot is at the centre (32,16). The rival dot is
  `(rival_table - player_table) / 2 + (32,16)`; if it falls outside the 64×32
  panel, clamp it to the nearest edge (a small edge marker is optional, but not
  required by this change).
- At 2×2 cells per pixel a one-cell separation can quantize to the same panel
  pixel. When the rival's pixel equals the player's, the player marker is
  drawn last and covers it; the radar log still records both table cells. The
  panel resolution, not the position formula, sets this limit.

Why 128 cells wide: 32 would be the current ring; 64 is the 16:9-scale window;
128 doubles the horizontal context to about 6× the 16:9 screen width and 4.5×
its height at the same 2×2-cell scale. The panel grew horizontally rather than
shrinking the cells.

### D2 — Positions and anchor

The radar is a view of the **table**, not of VRAM. For a rider:

```text
screen_x = (int16)(riderX - $0419)
screen_y = (int16)(riderY - $041D)
world_x  = (rendered_unwrapped_h + screen_x) >> 4
world_y  = (rendered_unwrapped_v + screen_y) >> 4
table_x  = world_x + s_src_ax[0]
table_y  = world_y + s_src_ay[0]
```

`rendered_unwrapped_h/v` are the last rendered field's unwrapped BG1 scroll
(`s_ow_unwrapped_h/v` / `s_ow_prev_h/v`), not the live register that is one
frame ahead. The rider/camera WRAM values are live; a one-frame position lag is
accepted, the same lag the game's own uploader has.

The radar is drawn only while `s_src_valid > 0` (the source anchor is live).
Before that, and in modes that do not maintain the anchor, the panel is hidden
(or the previous panel is cleared); no guessed track is drawn.

`$03ED` is the game's zoom shift (2 on Zoom Zoo). The rider's **on-screen
position** is the unzoomed `rider - camera`; the zoom only scales the cull
window in the staging code. The radar uses the unzoomed screen position; the
first validation step checks this against the recorded Zoom Zoo frames. If the
recorded position disagrees, the design must be corrected before continuing.

### D3 — Rendering: one `PpuExtraObject`

- A static `uint16_t s_radar_px[64*32]` in RGB15, bit 15 = opaque.
- One `PpuExtraObject` with `x/y` in **screen coordinates**,
  `width=64`, `height=32`, `stride=64`, priority 3, `math=1`, `oamSlot=127`,
  `order=1`. `PpuComposeExtraObjects` adds `kPpuExtraLeftRight` internally
  when it maps into the 800-wide priority buffer.
- The visible frame's right edge in screen coordinates is
  `256 + ppu->extraRightCur`; place the panel at
  `x = 256 + ppu->extraRightCur - 64 - 2`, `y = 224 - 32 - 2`. At 16:9 (E=43)
  that is screen x 233..296 (framebuffer 276..339): the lower-right 22 px are
  inside the native field but outside the HUD (Bowl's `0/68` ends at screen
  x 230); at 21:9/512 the panel stays in the margin.
- The panel is a dark background + 1-pixel border, the track in a bright
  colour, the player white and the rival red. No alpha: the panel is opaque.
  At 16:9 it uses the lower-right native band plus the margin but stays clear
  of the HUD; at 21:9/512 it stays in the margin.
- `PpuSetExtraObjects` is called in `GameDrawPpuFrame` before the rasterizer;
  when the radar is off or ineligible, set `NULL`.
- The object is host presentation data: no VRAM/CGRAM/OAM/WRAM writes.

Alternatives rejected:

- `draw_frame` presenter: more control (true screen-space, no brightness/color
  math), but must reproduce wide presentation/interpolation and is a host
  change. Keep it as a fallback if `PpuExtraObject` cannot be made legible.
- OSD/SDL overlay: easier UI, but not part of the captured frame and not
  scalable with the game.
- Drawing into VRAM/OAM: would touch guest state.

### D4 — config.ini switch

`config.ini` gains a **host-owned** key, read by the game in
`SnesDesktopHostGame.after_config`:

```ini
[Video]
MiniMap = 1
```

- Default when absent: **1 (on)**.
- `MiniMap = 0` (or false/off) disables it.
- `config.local.ini` is read after `config.ini`; last value wins.
- `SNESRECOMP_WS_MINIMAP=0` is an environment override for tests.
- The framework parser already accepts unknown keys in `[Video]` silently (it
  is a host-owned section), so no `snesrecomp` parser change is required.
- The game-side reader is deliberately small: find `[Video]`, match the key
  case-insensitively, parse a boolean. No dependency on framework internals.

### D5 — Gating

Draw the radar only when all hold:

- `s_radar_enabled` (config/env);
- `WideEnabled() && InLiveRace()`;
- `g_ram[0x0DDB] == 0` (1P full screen);
- `WsExtra() > 0`;
- `s_src_valid > 0` (table anchor live);
- `!snes_netplay_active()`.

When any fails, `PpuSetExtraObjects(ppu, NULL, 0)`. Native 4:3 and WS-off
therefore never see the panel.

### D6 — Cost

The 64×32 panel samples 128×64 cells = 8192 16-bit WRAM reads per frame (2×2
cells/pixel), plus 2048 pixel stores. That is negligible next to the renderer.
The `PpuExtraObject` adds one 32-row rectangle to the OBJ compositor. Validate
with `SNESRECOMP_FRAME_TIMING`; target < 0.2 ms/frame.

## Risks / Trade-offs

- **Anchor not live in clamp/wrap modes** → radar hidden; document that it is a
  `source`/default feature. A later change can maintain a radar-only anchor.
- **Player/rival table mapping with `$03ED`** → validate Zoom Zoo before
  trusting; if wrong, correct the formula or hide the dots on zoom tracks.
- **The panel covers the margin track** → intended; it is in the margin, not
  the native HUD. Check all five tracks at 16:9/21:9/512.
- **`PpuExtraObject` is affected by brightness/colour math** → validate during
  fades/finish; if the panel disappears, move to a host presenter or set the
  object math/priority accordingly.
- **No alpha** → a dark panel is opaque; use a small border and bright track
  colours. Dither/alpha is out of scope.
- **Rival far ahead** → at 64 cells the rival at ±280 px is inside; at the
  extreme edge clamp the dot; do not invent a position.
- **Table shape classification** → `word & 0x3FF` is the decoded shape
  criterion used by `track_shape.py`; validate against the map renders.

## Migration Plan

Test switch only, default on. Rollback: `[Video] MiniMap = 0` in config.ini, or
`SNESRECOMP_WS_MINIMAP=0` for a run. No guest state or save format changes.

## Open Questions

- Does the unzoomed rider screen position match the table cell on Zoom Zoo
  (`$03ED=2`)?
- Is `[Video]` acceptable as the user-facing section, or should this be a
  dedicated `[Uniracers]` section (which needs a one-line framework parser
  acceptance)?
- Is the 2×2-cells-per-pixel track shape readable enough? The panel is now
  64×32 (wide context); a 64×64 would add vertical context at the cost of
  covering more of the native frame at 16:9.
- Should the radar be on by default or opt-in while it is new?
