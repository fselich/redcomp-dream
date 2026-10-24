# recomp-ws-hud Specification

## Purpose

The widescreen HUD: rigid groups anchored to the wide frame, the lower rows kept native, an independent switch, and 4:3 and menus unchanged.
## Requirements
### Requirement: HUD layout measured

The change SHALL measure the BG3 HUD scanline bands and column groups from
BG3-only captures before applying any widescreen policy, on both the first and
second tracks.

#### Scenario: HUD groups are mapped

- **WHEN** the BG3-only frames are inspected
- **THEN** the top left/right groups, bottom group and prompt rows are recorded
- **AND** each group's transparent gaps and flat runs are identified

#### Scenario: Track variants differ

- **WHEN** Dragster (first track) uses `RACE` and Zoom Zoo (second track) uses
  `0/3`
- **THEN** both top-left spans are measured
- **AND** the policy covers the wider of the two rigid groups

#### Scenario: Layout is assumed

- **WHEN** the policy is applied without measured spans
- **THEN** the change is rejected
- **AND** no glyph may be stretched or clipped

### Requirement: Rigid groups anchored

The change SHALL anchor the measured rigid left/right HUD groups at the widened
edges and SHALL copy them 1:1, without resampling glyphs.

#### Scenario: RACE and timer move out

- **WHEN** the top HUD band is active
- **THEN** `RACE` sits at the widened left edge and the timer at the right
- **AND** their glyphs are byte-identical to the native render

#### Scenario: Glyphs distort

- **WHEN** any glyph is resampled, clipped or overlapped
- **THEN** the change is reverted
- **AND** the offending span is recorded

### Requirement: Continuous HUD fallback

The change SHALL use a measured elastic band or keep the clamp when the HUD has
no transparent column between its groups.

#### Scenario: Safe split exists

- **WHEN** the groups are separated by a transparent gap
- **THEN** the anchor band is used and no lower layer shows through

#### Scenario: Continuous bar

- **WHEN** no transparent gap exists
- **THEN** the change uses measured flat runs or the clamp
- **AND** the whole-line stretch band is not used

### Requirement: Centre and prompt rows preserved

The change SHALL leave the centred lap time and prompt rows unanchored unless a
measured safe split exists.

#### Scenario: Bottom group stays centred

- **WHEN** the wide frame is compared with 4:3
- **THEN** the lap time keeps its native centre and scale
- **AND** no prompt row moves into a margin

### Requirement: Independent HUD switch

The change SHALL provide `SNESRECOMP_WS_HUD` and `[Widescreen] Hud`, independent of the cull switch. The environment variable SHALL win over the config key.

#### Scenario: HUD falls back

- **WHEN** `SNESRECOMP_WS_HUD=0`
- **THEN** the HUD returns to the authentic clamp while cull stays active
- **AND** 4:3 is unaffected

#### Scenario: HUD off from config.ini

- **WHEN** `SNESRECOMP_WS_HUD` is unset and `config.ini` has `[Widescreen] Hud = 0`
- **THEN** the HUD returns to the authentic clamp while cull stays active

#### Scenario: Single global switch

- **WHEN** the HUD shares the cull toggle
- **THEN** it is rejected
- **AND** a separate gate is required

### Requirement: 4:3 and menus preserved

The change SHALL keep 4:3 and all non-live states identical to the baseline.

#### Scenario: 4:3 is preserved

- **WHEN** widescreen or the HUD switch is off
- **THEN** the HUD and whole frame match the preserved baseline
- **AND** no guest register or tilemap changed

#### Scenario: Menu is affected

- **WHEN** a menu shows the HUD policy
- **THEN** the policy is limited to live race
- **AND** the menu remains authentic

### Requirement: HUD evidence

The change SHALL capture BG3-only and composite HUD frames with the policy on
and off, and SHALL show no hole, glyph distortion or lower-layer bleed.

#### Scenario: Wide HUD is clean

- **WHEN** the wide captures are reviewed
- **THEN** the rigid groups are at the edges, 1:1
- **AND** the gap shows the world at the correct priority

#### Scenario: Hole or bleed

- **WHEN** a lower layer shows through the HUD band or a glyph distorts
- **THEN** the first bad frame is recorded
- **AND** the change is not claimed complete

### Requirement: Decision report

The change SHALL produce `report.md` with the measured groups, policy, switch,
4:3 result, captures and whether P2c is complete, partial or reverted.

#### Scenario: P2c complete

- **WHEN** the HUD evidence passes
- **THEN** the report records the spans and policy
- **AND** names any residual limitation

#### Scenario: Clamp retained

- **WHEN** no safe split exists
- **THEN** the report records the measurement and keeps the clamp
- **AND** no glyph is stretched

### Requirement: Two-player HUD bands

In a live 2P/VS split-screen race the system SHALL replace the 1P BG3 band set with three elastic bands and no clamp band: the top half, the divider and the bottom half. The half bands SHALL use the segments `{17,64 → 4-E,51-E}`, `{64,192 → 64,192}` and `{192,256 → 192+E,256+E}`; the divider band SHALL use `{0,256 → -E,256+E}`. The BG3 widen gate SHALL start at line 10. Columns no segment covers SHALL stay empty, so the rest of BG3 is clamped by the bands themselves. The 1P band set SHALL remain unchanged.

#### Scenario: No clamp band in 2P

- **WHEN** the 2P band set is installed
- **THEN** the BG3 clamp band is off
- **AND** the three elastic bands are in slots 0, 1 and 2

#### Scenario: Top and bottom headers

- **WHEN** the wide 2P frame is drawn
- **THEN** the top header (glyph rows 11..21) and bottom header (rows 123..133) are moved by the half segments
- **AND** each glyph is byte-identical to its native render

#### Scenario: Header rows below line 17

- **WHEN** the top header's rows 11..16 are drawn
- **THEN** they are remapped like rows 17..21, because the BG3 widen gate starts at line 10

#### Scenario: Player names

- **WHEN** a player name (rows 91..101 or 203..213) is drawn
- **THEN** it is shifted by `E` with no resampling
- **AND** it stays inside the widened right edge

#### Scenario: Divider

- **WHEN** the divider (rows 112..113) is drawn
- **THEN** it spans the full widened width
- **AND** no seam is visible on the flat run

#### Scenario: Centre prompts

- **WHEN** a prompt at rows 43..53 or 155..165 is drawn
- **THEN** it remains at its native x

#### Scenario: Margins otherwise empty

- **WHEN** BG3 holds content outside the three segment sources on a band line
- **THEN** nothing of it is drawn in the margins

#### Scenario: 2P HUD switch off

- **WHEN** `[Widescreen] Hud` is `0`
- **THEN** BG3 is clamped to the native 256 px in both halves
- **AND** elastic slots 0..2 are cleared

#### Scenario: 1P unchanged

- **WHEN** `$0DDB` is zero
- **THEN** the existing 1P band, clamp band, widen gate and segments are used
- **AND** elastic slots 1 and 2 are cleared

