## MODIFIED Requirements

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
