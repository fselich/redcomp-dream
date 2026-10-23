## MODIFIED Requirements

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
