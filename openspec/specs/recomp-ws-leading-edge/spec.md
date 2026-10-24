# recomp-ws-leading-edge Specification

## Purpose

Ring-verified BG1 margin cells: a margin cell is drawn from the ring only when the native view confirmed its world cell and tile word; the first stage of the source policy.

## Requirements
### Requirement: Verified ring cells

With the `source` margin policy, the system SHALL serve a BG1 margin cell from the ring when its world key and tile word were confirmed in the native view. A cell that is not verified SHALL be served from the track table as `recomp-ws-source` specifies, and otherwise rendered as background.

#### Scenario: Trailing cell verified

- **WHEN** a margin cell's ring entry was seen in the native view with the same world cell and the VRAM word is unchanged
- **THEN** the cell is drawn from the ring

#### Scenario: Stale leading cell

- **WHEN** a margin cell's ring entry holds a different world cell, or was never seen natively, and the track table cannot serve it
- **THEN** the cell is drawn as background
- **AND** it is counted as masked, not as wrong

#### Scenario: Direction change

- **WHEN** the camera reverses, stops or moves vertically
- **THEN** the same per-cell rule applies without a direction heuristic

### Requirement: Source-backed leading cells

With `SNESRECOMP_WS_OFFWINDOW=source`, the system SHALL fill masked leading
cells only from data produced by the game's own map path (row builder or
resolver), and SHALL keep them masked when no such data is available.

#### Scenario: Source available

- **WHEN** the leading-edge source yields a cell for the needed world cell
- **THEN** the cell is drawn from that source
- **AND** the probe ground truth confirms it on the validation captures

#### Scenario: Source unavailable

- **WHEN** no sourced value exists for a needed cell
- **THEN** the cell stays masked to background

#### Scenario: Guest state preserved

- **WHEN** the source hook runs
- **THEN** guest CPU, WRAM and VRAM hashes are identical before and after it

### Requirement: Zero wrong margin cells

The system SHALL NOT serve a margin cell whose content differs from the
probe ground truth for that world cell.

#### Scenario: Five-track validation

- **WHEN** Dragster, Zoom Zoo, Bowl, Switcher and Monster are run with the
  probe on both modes
- **THEN** the scorer reports 0 wrong served cells
- **AND** reports coverage and masked-track counts per side and track

### Requirement: Presentation-only and 4:3 exact

The system SHALL NOT change native 4:3 output or guest simulation.

#### Scenario: Native output

- **WHEN** widescreen is off, with the switch set or unset
- **THEN** native frames are bit-identical to the baseline

#### Scenario: Margin substitution

- **WHEN** a margin sample is not verified
- **THEN** the renderer substitutes a transparent tile for that sample only
- **AND** guest VRAM is not modified

#### Scenario: Simulation unchanged

- **WHEN** a mode is enabled in 16:9
- **THEN** camera, scroll, RNG and input traces match the clamp run frame by
  frame

### Requirement: Dedicated switch

The margin policy SHALL be chosen as `recomp-ws-source-default` specifies. With `source`, the global BG1 clamp SHALL NOT be applied to BG1; with `clamp`, the margin hooks SHALL NOT be installed. HUD and cull policies SHALL be independent of the margin policy.

#### Scenario: Source

- **WHEN** the policy is `source`
- **THEN** the global BG1 clamp is not applied
- **AND** HUD and cull policies are unchanged

#### Scenario: Clamp

- **WHEN** the policy is `clamp`
- **THEN** no margin line or tile hook is installed for the frame

### Requirement: Per-option decision

The change SHALL report a separate go/no-go for Option A and Option C, with
coverage, wrong-cell counts and per-frame cost.

#### Scenario: Option A only

- **WHEN** Option A passes and Option C fails
- **THEN** the report may recommend Option A as the new default over the clamp
- **AND** records the Option C blocker

#### Scenario: Both fail

- **WHEN** Option A shows any wrong cell or a 4:3 difference
- **THEN** the clamp remains the default
- **AND** the first failing frame and cell are recorded

