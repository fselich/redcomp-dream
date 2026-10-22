# recomp-ws-validation Specification

## Purpose
TBD - created by archiving change uniracers-ws-validation. Update Purpose after archive.
## Requirements
### Requirement: 1P track matrix

The change SHALL cover every reachable 1P track: Dragster, Zoom Zoo, Bowl,
Switcher and Monster, or record why a track is unreachable.

#### Scenario: All tracks are reachable

- **WHEN** the PICK TRACK menu is used
- **THEN** each track is captured and named in the matrix
- **AND** the route confirms NOW PLAYING names that track

#### Scenario: A track is unreachable

- **WHEN** a track cannot be selected in 1P
- **THEN** the report records it as unreachable
- **AND** no validation is claimed for it

### Requirement: HUD variants covered

The change SHALL measure the BG3 groups on every track and SHALL keep every
glyph 1:1, unclipped and unoverlapped.

#### Scenario: A wider label appears

- **WHEN** a top-left label is wider than the measured source span
- **THEN** the segment or clamp is adjusted for that track
- **AND** the report records the new span

#### Scenario: A glyph distorts

- **WHEN** a label, timer, arrow or lap-time glyph is resampled or clipped
- **THEN** the change is reverted
- **AND** the first bad frame/track is recorded

### Requirement: Objects and cull verified

The change SHALL recheck the CPU ghost with cull on/off at both margins.

#### Scenario: Ghost survives the edge

- **WHEN** the ghost crosses a native edge
- **THEN** it remains visible in the wide margin
- **AND** cull-off matches the authentic behavior

#### Scenario: Object pops

- **WHEN** an object disappears at a native edge
- **THEN** the track/frame/slot is recorded
- **AND** the validation is not claimed complete

### Requirement: Menus and transitions

The change SHALL verify title, menus, PICK screens, NOW PLAYING, countdown,
pause and finish; widescreen and the HUD policy SHALL be inert outside live
race.

#### Scenario: Menu is authentic

- **WHEN** a non-live screen is captured
- **THEN** it matches the 4:3 baseline
- **AND** no HUD band or cull change is active

#### Scenario: HUD leaks

- **WHEN** a menu shows the wide HUD band
- **THEN** the gate is fixed
- **AND** the frame is rechecked

### Requirement: 4:3 and simulation preserved

The change SHALL keep 4:3, RNG, progression, collision, camera and input
unchanged.

#### Scenario: 4:3 is exact

- **WHEN** widescreen or the HUD switch is off
- **THEN** frames match the preserved baseline
- **AND** no guest state differs

#### Scenario: Simulation changes

- **WHEN** any simulation state diverges
- **THEN** the change is reverted
- **AND** the first divergent frame is recorded

### Requirement: Presentation fixes documented

Any per-track HUD or cull fix SHALL be recorded with its measured span and
switch; no gameplay fix is allowed.

#### Scenario: Per-track fix

- **WHEN** a track needs a wider label span
- **THEN** the new span and reason are recorded
- **AND** the fix is presentation-only

#### Scenario: Gameplay change

- **WHEN** a fix alters spawn, RNG, collision or progression
- **THEN** it is rejected
- **AND** separated from this change

### Requirement: Validation report

The change SHALL produce a per-track matrix with 4:3, HUD, objects and
transition results, and a final accept/partial/revert decision.

#### Scenario: All tracks pass

- **WHEN** the matrix passes
- **THEN** the report states the 1P 16:9 release gate is accepted
- **AND** lists any residual limitation

#### Scenario: A track fails

- **WHEN** any track fails HUD, object or transition validation
- **THEN** the report records the failure and next diagnostic
- **AND** the release gate is not accepted

