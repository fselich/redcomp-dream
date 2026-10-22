# recomp-ws-radar Specification

## Purpose
TBD - created by archiving change uniracers-ws-radar. Update Purpose after archive.
## Requirements
### Requirement: Local radar window

While enabled in a live 1P widescreen race, the system SHALL draw a small
in-frame radar in the bottom-right margin showing a local window of the decoded
`$7F` track table centred on the player, larger than the 32×32 BG1 ring.

#### Scenario: Radar visible

- **WHEN** the radar is enabled, the race is live 1P widescreen and the track
  anchor is valid
- **THEN** the radar shows a 128×64-cell window around the player
- **AND** the player is at the centre of the panel
- **AND** the track shape comes from the `$7F` table, not from a guess

#### Scenario: No anchor yet

- **WHEN** the source anchor is not live (race start or a mode without it)
- **THEN** no track shape is invented
- **AND** the radar is hidden or shows an empty panel until the anchor is valid

#### Scenario: Track bounds

- **WHEN** the 128×64-cell window extends past the table grid
- **THEN** the outside cells are background
- **AND** the radar does not wrap or mirror the map

### Requirement: Player and rival positions

The radar SHALL draw the player and CPU rival as markers at their true table
positions, using the live WRAM rider/camera values and the same track anchor as
`source`. When their positions quantize to the same 64×32 panel pixel, the
player marker is drawn last; the log still records both table cells.

#### Scenario: Both riders in the window

- **WHEN** the player and rival table cells are within the 128×64-cell window
- **THEN** both positions are represented by markers at their relative pixels
- **AND** coincident positions may share a pixel, with the player marker on top

#### Scenario: Rival outside the window

- **WHEN** the rival is outside the 128×64-cell window
- **THEN** its dot is clamped to the nearest panel edge or omitted
- **AND** no false in-window position is drawn

#### Scenario: Zoom track

- **WHEN** the track uses `$03ED` zoom (for example Zoom Zoo)
- **THEN** the dot position uses the rider's on-screen position in the same
  world coordinates as the track table
- **AND** the result is validated against the recorded frames

### Requirement: Presentation-only

The radar SHALL NOT change guest memory, VRAM, CGRAM, OAM or simulation. It
SHALL be absent at 4:3 and in non-live screens, and disabling it SHALL leave the
frame identical to the current build.

#### Scenario: Disabled

- **WHEN** the config switch or environment override disables the radar
- **THEN** the wide frame is byte-identical to the current build
- **AND** guest state is unchanged

#### Scenario: Native 4:3

- **WHEN** widescreen is off
- **THEN** the radar is not drawn
- **AND** native output is byte-identical to the native baseline

#### Scenario: Guest state

- **WHEN** the radar is enabled for 2,500 frames
- **THEN** the state trace, WRAM, VRAM, CPU and DMA state match the radar-off
  run for the same route

### Requirement: Configuration switch

The radar SHALL be controlled by the host-owned `config.ini` key
`[Video] MiniMap`, on by default. `MiniMap = 0` SHALL disable it independently
of the widescreen/source/cull switches. `config.local.ini` SHALL override
`config.ini`, and `SNESRECOMP_WS_MINIMAP=0` SHALL be a test override.

#### Scenario: Config off

- **WHEN** `config.ini` contains `[Video] MiniMap = 0`
- **THEN** no radar is drawn
- **AND** the source margins, cull and HUD still work

#### Scenario: Local override

- **WHEN** `config.ini` says `MiniMap = 1` and `config.local.ini` says
  `MiniMap = 0`
- **THEN** the radar is off

#### Scenario: Default

- **WHEN** neither file contains `MiniMap`
- **THEN** the radar is on in an eligible race

### Requirement: Placement and scope

The radar SHALL live in the lower-right widescreen corner and SHALL NOT cover
the native HUD. At 16:9 its extra width may overlap the lower-right native
band; at 21:9/512 it stays in the margin. It SHALL be limited to live 1P
widescreen races.

#### Scenario: HUD

- **WHEN** the radar is drawn at 16:9, 21:9 or 512 px
- **THEN** the native HUD, timer and lap rows are not covered

#### Scenario: Split screen or netplay

- **WHEN** `$0DDB` is not zero or netplay is active
- **THEN** the radar is not drawn

### Requirement: Validation coverage

The change SHALL validate the radar on Dragster, Zoom Zoo, Bowl, Switcher and
Monster, at 16:9, 21:9 and 512 px, and SHALL report cost and visual evidence.

#### Scenario: Five-track run

- **WHEN** the five 1P tracks are run with the radar enabled
- **THEN** the track shape and both dots are checked against the decoded map
  and the on-screen riders
- **AND** the panel does not cover the HUD

#### Scenario: Cost and invariance

- **WHEN** the radar is enabled and disabled on the same route
- **THEN** the frame-time difference is reported and is below 0.2 ms/frame
- **AND** the non-radar pixels and guest state are identical

