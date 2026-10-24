## ADDED Requirements

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
