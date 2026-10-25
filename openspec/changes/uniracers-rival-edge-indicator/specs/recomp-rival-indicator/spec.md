## ADDED Requirements

### Requirement: Border position

The system SHALL draw the rival indicator at the point where the line from the
player to the CPU rival crosses the visible frame border, and SHALL support all
eight directions. The indicator SHALL be hidden while the rival is inside the
visible frame. The visible frame SHALL be the current presentation frame
(`-extraLeft .. 256+extraRight`, `0 .. 224`), not the native 256-px centre.

#### Scenario: Rival offscreen right

- **WHEN** the rival is outside the frame to the right of the player
- **THEN** the arrow appears on the right border and points right

#### Scenario: Rival offscreen diagonally

- **WHEN** the rival is outside the frame up and to the left
- **THEN** the arrow appears where the player→rival line crosses the top or left
  border and points outward along that diagonal

#### Scenario: Rival in a wide margin

- **WHEN** the rival is outside the native 256-px view but inside the 21:9
  extra margin
- **THEN** no border arrow is drawn because the rival is visible

#### Scenario: Rival on screen

- **WHEN** the rival is inside the visible frame
- **THEN** no border arrow is drawn

### Requirement: Distance cue

The indicator SHALL show one to three dots behind the arrow, with more dots for
a greater screen-space distance. The distance SHALL be computed from the
player→rival screen vector and bucketed relative to the visible frame width.
The initial buckets SHALL be: one dot below `1.5 × frameWidth`, two dots below
`4 × frameWidth`, three dots otherwise.

#### Scenario: Near rival

- **WHEN** the rival is just outside the frame
- **THEN** the arrow carries one distance dot

#### Scenario: Far rival

- **WHEN** the rival is many frame widths away
- **THEN** the arrow carries three distance dots

#### Scenario: Distance buckets change live

- **WHEN** the rival moves from one distance bucket to another during a race
- **THEN** the indicator changes its dot count on the next presented frame

### Requirement: Gating and setting

The indicator SHALL be enabled by default through
`[Widescreen] RivalIndicator = 1`, with `SNESRECOMP_WS_RIVAL` as the
environment override. It SHALL be drawn only during a live 1P race, and SHALL
NOT be drawn in 2P split screen, during netplay, outside a live race, or when
the setting is off.

#### Scenario: Default on

- **WHEN** no configuration sets `RivalIndicator`
- **THEN** the indicator is enabled

#### Scenario: Disabled

- **WHEN** `SNESRECOMP_WS_RIVAL=0` or `[Widescreen] RivalIndicator = 0`
- **THEN** no border arrow is drawn

#### Scenario: Two-player

- **WHEN** the game is in 2P split screen
- **THEN** no border arrow is drawn

#### Scenario: Netplay

- **WHEN** a netplay session is active
- **THEN** no border arrow is drawn

### Requirement: Coexistence and presentation

The existing BG3 `<<<`/`>>>` HUD arrows SHALL remain unchanged in this version.
The border indicator SHALL be presentation-only: it SHALL be drawn through the
extra-object compositor and SHALL NOT write WRAM, VRAM, OAM or CGRAM. The
radar and the indicator SHALL both be present when both are enabled.

#### Scenario: Original arrows remain

- **WHEN** the border indicator is enabled
- **THEN** the game's BG3 ahead/behind arrows are still drawn

#### Scenario: Guest state unchanged

- **WHEN** the indicator is toggled on and off at the same frame
- **THEN** the guest state trace and save state are identical

#### Scenario: Radar and indicator together

- **WHEN** both `MiniMap` and `RivalIndicator` are on and both apply
- **THEN** the radar panel and the border arrow are both presented
