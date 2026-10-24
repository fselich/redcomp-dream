# recomp-ws-source-default Specification

## Purpose

Which BG1 margin policy applies: source by default, clamp as the only alternative, and how values of removed policies are mapped.

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

The system SHALL offer exactly two BG1 margin policies: `source` and `clamp`. The policy SHALL come from `SNESRECOMP_WS_OFFWINDOW`, else `SNESRECOMP_WS_TRACK_CLAMP` (which selects `clamp`), else `[Widescreen] Margin`, else `source`. Values of removed policies SHALL be mapped, not rejected: `ring` and `ghost` SHALL resolve to `source`; `wrap`, `0` and `off` SHALL resolve to `clamp`; any other value SHALL resolve to `source`.

#### Scenario: Explicit clamp

- **WHEN** `SNESRECOMP_WS_OFFWINDOW=clamp`
- **THEN** BG1 is clamped to the native 256 px and the margins show the backdrop

#### Scenario: Legacy clamp switch

- **WHEN** only `SNESRECOMP_WS_TRACK_CLAMP` is set
- **THEN** BG1 is clamped

#### Scenario: Policy from config.ini

- **WHEN** no margin variable is set and `config.ini` has `[Widescreen] Margin = clamp`
- **THEN** BG1 is clamped

#### Scenario: Removed fill policy

- **WHEN** `config.ini` has `[Widescreen] Margin = ring`, or `SNESRECOMP_WS_OFFWINDOW=ghost`
- **THEN** the policy is `source`
- **AND** the frame hash equals the `source` case

#### Scenario: Removed raw wrap

- **WHEN** `SNESRECOMP_WS_OFFWINDOW=wrap`, `0` or `off`
- **THEN** the policy is `clamp`
- **AND** the frame hash equals the `clamp` case

#### Scenario: Unknown value

- **WHEN** `config.ini` has `[Widescreen] Margin = banana`
- **THEN** the policy is `source`

### Requirement: Quiet default

The system SHALL NOT print per-scene widescreen diagnostics to stderr unless a
widescreen log switch is set.

#### Scenario: Default run

- **WHEN** no `SNESRECOMP_WS_*LOG` variable is set
- **THEN** no `[ws-ow]` lines are printed

