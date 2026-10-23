# recomp-ws-source-default Specification

## Purpose
TBD - created by archiving change uniracers-ws-source-default. Update Purpose after archive.
## Requirements
### Requirement: Source is the default 16:9 BG1 policy

The system SHALL use the `source` margin policy when widescreen is on in a live
race and neither `SNESRECOMP_WS_OFFWINDOW` nor `SNESRECOMP_WS_TRACK_CLAMP` is
set.

#### Scenario: Normal play

- **WHEN** the game runs with `Widescreen = 1` and no `SNESRECOMP_WS_*`
  environment
- **THEN** BG1 margin cells are served as in `SNESRECOMP_WS_OFFWINDOW=source`

#### Scenario: Widescreen off

- **WHEN** widescreen is off
- **THEN** native frame 2500 SHA-256 is `3417f91a…df44`

### Requirement: Alternative policies stay selectable

The system SHALL keep `ring`, `ghost`, `clamp` and `wrap` selectable through `SNESRECOMP_WS_OFFWINDOW`. It SHALL honour `SNESRECOMP_WS_TRACK_CLAMP` when `SNESRECOMP_WS_OFFWINDOW` is unset. When neither variable is set, the policy SHALL come from `[Widescreen] Margin` (`source`, `ring`, `clamp` or `wrap`), and `source` when that key is missing. `ghost` SHALL stay selectable only through the environment.

#### Scenario: Explicit clamp

- **WHEN** `SNESRECOMP_WS_OFFWINDOW=clamp`
- **THEN** BG1 is clamped to the native 256 px as before

#### Scenario: Legacy clamp switch

- **WHEN** only `SNESRECOMP_WS_TRACK_CLAMP` is set
- **THEN** BG1 is clamped

#### Scenario: Raw wrap

- **WHEN** `SNESRECOMP_WS_OFFWINDOW=wrap`
- **THEN** the margins show the raw ring, as before this change

#### Scenario: Policy from config.ini

- **WHEN** no margin variable is set and `config.ini` has `[Widescreen] Margin = ring`
- **THEN** the BG1 margins use the `ring` policy

#### Scenario: Ghost not in config.ini

- **WHEN** `config.ini` has `[Widescreen] Margin = ghost` and no margin variable is set
- **THEN** the policy is `source`

### Requirement: Quiet default

The system SHALL NOT print per-scene widescreen diagnostics to stderr unless a
widescreen log switch is set.

#### Scenario: Default run

- **WHEN** no `SNESRECOMP_WS_*LOG` variable is set
- **THEN** no `[ws-ow]` lines are printed

