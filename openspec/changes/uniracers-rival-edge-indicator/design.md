## Context

The port already has:

- the rival's live position in WRAM (`$0413/$0417`), the player's
  (`$0411/$0415`) and the camera (`$0419/$041D`);
- the radar's world-cell formula and `PpuExtraObject` compositor;
- the settings model and in-game menu from `uniracers-ingame-options`;
- an existing BG3 rival arrow in the HUD: three glyphs in rows 112–126 at
  columns 40–63 (left) or 208–231 (right). It is clamped to the native 256-px
  centre and only expresses ahead/behind.

The radar already consumes `PpuSetExtraObjects`; the new indicator must share
that call rather than overwrite it.

## Goals / Non-Goals

**Goals:**

- Show the rival's direction on any edge of the visible frame, including
  diagonals.
- Show a coarse distance cue (1–3 inward dots) on the same arrow.
- Work in 4:3, 16:9, 21:9 and 512 px, using the current wide frame bounds.
- Draw only when the rival is outside the visible frame.
- Be a presentation-only `PpuExtraObject`; no guest writes.
- Be on by default and switchable from the menu/config.
- Coexist with the existing BG3 arrows in this first version.

**Non-Goals:**

- Replacing or suppressing the game's `<<<`/`>>>` HUD arrows.
- A numeric time gap (`+3.2 s`), turn-by-turn navigation or a full-course map.
- 2P split screen, netplay, non-live screens or the finish/pause overlays.
- Alpha-blended UI or a `draw_frame` presenter rewrite.
- A framework patch.

## Decisions

### D1. Screen coordinates and visible rectangle

Use the same screen conventions as the radar:

```text
player_screen = ((int16)(playerX - camX), (int16)(playerY - camY))
rival_screen  = ((int16)(rivalX  - camX), (int16)(rivalY  - camY))
```

The visible rectangle is the frame currently being presented:

```text
left   = -ppu->extraLeftCur
right  = 256 + ppu->extraRightCur
top    = 0
bottom = 224
```

Using the frame bounds rather than native `0..256` means:
- in 4:3 the border is the authentic edge;
- at 16:9/21:9 a rival visible in the extra margin produces no arrow;
- a future asymmetric margin (`C1`) keeps working.

If the rival's screen point is inside the rectangle, no arrow is drawn.

### D2. Border point and direction

Trace the segment from `player_screen` to `rival_screen` and choose the first
intersection with the rectangle inset by the arrow radius (4 px). If the player
is not inside the rectangle (should not happen in a live race), use the frame
centre as the ray origin.

Classify the resulting vector into eight directions. The arrow is drawn at the
intersection, rotated to point outward:

```text
        ▲        ↗        →        ↘
        ↑                          ↓
        ↖        ←        ↙        ▼
```

For an axis-aligned ray the intersection is the corresponding edge; for a
diagonal ray use the first edge hit, so the arrow can land anywhere along a
border, not only at the corners.

*Alternative rejected:* clamp `rivalX` and `rivalY` independently. That snaps
diagonal offscreen rivals to a corner and does not follow the player→rival
line.

### D3. Distance cue

Distance is the screen-space length of the player→rival vector:

```text
d = max(|rivalX - playerX|, |rivalY - playerY|)
```

`max` (Chebyshev) keeps the arithmetic integer and gives the same bucket on
all diagonals without a square root. The initial thresholds are relative to
the current frame width:

| Dots | Distance |
|---|---|
| 1 | `d < 1.5 × frameWidth` |
| 2 | `d < 4 × frameWidth` |
| 3 | otherwise |

The thresholds are provisional; they are tuned against captures on Dragster
and Zoom Zoo. The existing BG3 chevrons are not used as a distance source:
observation shows their visible count animates/resets while the rival keeps
moving away, so they are not a monotonic distance counter.

### D4. Arrow bitmaps

Pre-render eight directions × three distance levels into 16×16 RGB15 buffers
(bit 15 = opaque, like the radar). Each bitmap contains:

- a solid arrow head at the outer edge, red with a one-pixel dark outline so it
  reads over track, sky and HUD;
- 0–2 additional dots behind the head, inward along the ray, to complete the
  1–3 distance cue.

A single reusable 16×16 buffer redrawn each frame is also acceptable; the
pre-rendered set is chosen for simplicity at 60 Hz. The arrow uses the radar's
`PpuExtraObject` conventions: `priority = 3`, `math = 1`, `oamSlot = 126`
(127 stays the radar's), `order = 1`.

*Alternative rejected:* window-space drawing through `draw_frame`. It would be
crisper at arbitrary window scales but inconsistent with SNES pixels and would
require touching every presenter path.

### D5. Extra-object coordination

`WsRadarUpdate` currently calls `PpuSetExtraObjects(ppu, &s_radar_obj, 1)` or
`NULL`. Add a small shared list in the presentation layer:

```text
WsExtraObjectsBegin()
  WsRadarUpdate(...)      -> append radar object, or nothing
  WsRivalUpdate(...)      -> append indicator object, or nothing
WsExtraObjectsCommit(ppu)
  -> PpuSetExtraObjects(ppu, list, count)   // NULL/0 when empty
```

The order is stable: radar first, indicator second. Neither module calls
`PpuSetExtraObjects` directly after the refactor.

### D6. Gating and setting

The indicator is drawn only when all hold:

- `WsInLiveRace()`;
- the wide frame is active or the native frame is in a race;
- `!kRamTwoPlayer`;
- `!snes_netplay_active()`;
- the rival is outside the visible rectangle;
- the new `RivalIndicator` setting is on.

New setting:

```ini
[Widescreen]
RivalIndicator = 1     ; default on
```

- environment override: `SNESRECOMP_WS_RIVAL`;
- game-owned key, written by `WsSettingsSave`;
- menu row `RIVAL INDICATOR <ON>`; the menu row height is reduced so the 12
  settings plus Save/Close still fit above the footer.

### D7. Coexistence with the BG3 arrows

This version does not touch BG3. The original arrows remain the
ahead/behind/distance-ish cue; the new arrow answers "where". The two can
disagree on looping tracks: the old arrow follows the game's race logic, the
new arrow is spatial. Document this, then decide from play-testing whether to
suppress the original.

A future replacement can use the existing BG3 elastic-band machinery: shrink
the BG3 clamp band and add elastic slots that copy the prompt/lap-time rows
1:1 while omitting the arrow columns. That is deliberately out of scope here
because it changes the HUD policy and needs its own validation.

## Risks / Trade-offs

- **[Spatial arrow and "ahead/behind" disagree on loops]** → The original BG3
  arrow stays visible; label the new cue as position, not race order, and
  validate on Zoom Zoo.
- **[Duplicate horizontal indicators]** → Accept in v1; if it is noisy, the
  elastic-band suppression is the next iteration.
- **[Top-edge arrow covers `0/3` or the timer]** → Use a dark outline and a
  4 px inset; if needed, shift the top landing zone below the 32-px HUD band.
- **[Distance thresholds feel wrong]** → Buckets are a single table in the new
  module; tune with captures without changing geometry.
- **[The rival's screen position is a parked/sentinel value]** → Gate on live
  race and reuse the radar's existing position assumptions; if a sentinel
  appears, hide the arrow for that frame.
- **[Extra-object z-order or overlap with the radar]** → Fixed slots 126/127
  and a single shared object array; radar keeps its existing priority.
- **[Indicator draws over the radar in a corner]** → Radar occupies the
  bottom-right 64×32; the inset arrow can be placed outside that rectangle or
  drawn after it with the same opaque priority; validate visually.

## Migration Plan

No data or save migration. The setting is new and defaults on; setting it to
`0` restores the previous presentation exactly. Removing the module and the
setting returns the build to the current state.

## Open Questions

- Final distance thresholds and whether three levels are enough.
- Whether the top-edge arrow should be allowed to overlap the HUD or be
  lowered below it.
- Whether the original BG3 arrow should be suppressed in a later change.
- Should the indicator also appear while the game is paused, or only while the
  guest is running? Current proposal: only live race.
