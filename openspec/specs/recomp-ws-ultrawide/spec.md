# recomp-ws-ultrawide Specification

## Purpose

Aspect-driven race width up to 512 px, with the margin-dependent policies (cull, HUD, OAM) following the margin and the track correct at every width.
## Requirements
### Requirement: Aspect-driven race width

The system SHALL size the widescreen race frame from the target aspect, never narrower than 16:9 and never wider than the maximum width. The target is the aspect setting (`SNESRECOMP_WS_ASPECT`, else `[Widescreen] Aspect`) when it is not `auto`, otherwise the window aspect. The maximum width is `[Widescreen] MaxWidth`, clamped to [342, 512], default 512.

#### Scenario: 16:9 screen

- **WHEN** the target aspect is 16:9 with 4:3 CRT pixels
- **THEN** the race frame is 342 px wide
- **AND** the frame equals the pre-change 16:9 output

#### Scenario: 21:9 screen

- **WHEN** the target aspect is 21:9 with 4:3 CRT pixels
- **THEN** the race frame is 448 px wide (96 px margins)

#### Scenario: Very wide target

- **WHEN** the target needs more than the maximum width
- **THEN** the frame is capped at the maximum width and letterboxed by the presenter

#### Scenario: Lower maximum width

- **WHEN** `[Widescreen] MaxWidth = 384` and the target aspect is 21:9
- **THEN** the race frame is 384 px wide

#### Scenario: Aspect from config.ini

- **WHEN** `SNESRECOMP_WS_ASPECT` is unset and `config.ini` has `[Widescreen] Aspect = 21:9`
- **THEN** the target aspect is 21:9 whatever the window aspect

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

### Requirement: Two-player ultrawide widths

The aspect-driven width SHALL apply per half in a live 2P/VS split-screen race. Each half SHALL be `E+256+E` wide and 112 high, with the same `Aspect`, `MaxWidth` and `DisplayAspect` precedence and the same 342..512 px range and 512 px cap as 1P.

#### Scenario: 2P 21:9

- **WHEN** the target aspect is 21:9 and a 2P race is live
- **THEN** the frame is 448×224 and each half is 448×112

#### Scenario: 2P 512 px

- **WHEN** the target width is 512 px and a 2P race is live
- **THEN** the frame is 512×224 and each half is 512×112
- **AND** the OAM X cap is respected

#### Scenario: 2P 4:3

- **WHEN** widescreen is off
- **THEN** the 2P frame is 256×224

#### Scenario: 2P non-live

- **WHEN** a 2P menu or selection screen is shown
- **THEN** the frame is 256×224

