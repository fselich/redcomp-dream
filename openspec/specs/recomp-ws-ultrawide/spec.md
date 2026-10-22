# recomp-ws-ultrawide Specification

## Purpose
TBD - created by archiving change uniracers-ws-ultrawide. Update Purpose after archive.
## Requirements
### Requirement: Aspect-driven race width

The system SHALL size the widescreen race frame from the target aspect, never
narrower than 16:9 and never wider than 512 px. The target is
`SNESRECOMP_WS_ASPECT` when set, otherwise the window aspect.

#### Scenario: 16:9 screen

- **WHEN** the target aspect is 16:9 with 4:3 CRT pixels
- **THEN** the race frame is 342 px wide
- **AND** the frame equals the pre-change 16:9 output

#### Scenario: 21:9 screen

- **WHEN** the target aspect is 21:9 with 4:3 CRT pixels
- **THEN** the race frame is 448 px wide (96 px margins)

#### Scenario: Very wide target

- **WHEN** the target needs more than 512 px
- **THEN** the frame is capped at 512 px and letterboxed by the presenter

#### Scenario: Outside the race

- **WHEN** the race is not live or widescreen is off
- **THEN** the frame is 256 px wide

### Requirement: Margin-dependent policies follow the margin

The system SHALL derive the object cull margin and the HUD elastic segments
from the current margin width instead of a fixed 43 px.

#### Scenario: Cull

- **WHEN** the margin is E px
- **THEN** the game's horizontal cull window is widened by E on each side

#### Scenario: HUD

- **WHEN** the margin is E px
- **THEN** the left HUD group ends 4 px inside the left frame edge and the timer
  ends at the right frame edge, both unscaled

### Requirement: Track and layers correct at every width

The system SHALL show the authentic BG1 track in all margins at every
supported width, and SHALL keep native 4:3 unchanged.

#### Scenario: BG1 probe

- **WHEN** Dragster, Zoom Zoo and Monster are probed at 21:9 and at 512 px
- **THEN** wrong served BG1 cells are 0

#### Scenario: Native

- **WHEN** widescreen is off
- **THEN** native frame 2500 SHA-256 is `3417f91a…df44`

