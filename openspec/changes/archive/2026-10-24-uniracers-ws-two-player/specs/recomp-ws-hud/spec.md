## ADDED Requirements

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
