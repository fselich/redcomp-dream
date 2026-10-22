# recomp-widescreen-spike Specification

## Purpose
TBD - created by archiving change uniracers-widescreen-spike. Update Purpose after archive.
## Requirements
### Requirement: P16 baseline preserved

The change SHALL preserve the dispatch-fix 4:3 baseline and SHALL verify that
4:3 remains aligned-equivalent at the documented offset when the wide flag is
off.

#### Scenario: 4:3 is unchanged

- **WHEN** the wide build runs with the flag off
- **THEN** its frames match the baseline at the documented offset `k`
- **AND** no simulation state differs

#### Scenario: 4:3 diverges

- **WHEN** the flag-off frames no longer align
- **THEN** the first divergent frame is recorded
- **AND** the wide change is reverted until 4:3 is restored

### Requirement: Live-race discriminator

The change SHALL identify and verify a WRAM byte or word that means "in live
race" across menu, race and pause, or SHALL document that no such discriminator
was found and the gate risk.

#### Scenario: Discriminator is proven

- **WHEN** the candidate is watched across menu, race and pause
- **THEN** the report records the address, values and frame ranges
- **AND** states how the wide mode is gated

#### Scenario: Discriminator is not proven

- **WHEN** no candidate distinguishes live race from menus or cutscenes
- **THEN** the report records the failed candidates
- **AND** the wide mode uses a documented fallback gate with its risk

### Requirement: Flag-gated 16:9

The change SHALL enable a 342×224 wide field behind a runtime flag and SHALL
keep 4:3 as the default.

#### Scenario: Wide mode is enabled

- **WHEN** the flag is on
- **THEN** the frame is 342×224 and the host reports the widened margin
- **AND** the wide mode can be disabled without rebuilding

#### Scenario: Flag is not reversible

- **WHEN** the wide mode cannot be toggled at runtime
- **THEN** the change records the limitation
- **AND** does not replace the 4:3 default

### Requirement: Wider-field evidence

The change SHALL capture 4:3 and 16:9 at the same aligned race state and SHALL
show that the wide frame adds world columns rather than stretching the 4:3
picture.

#### Scenario: The field is genuinely wider

- **WHEN** the captures are compared
- **THEN** the wide frame contains columns absent from the 4:3 frame
- **AND** the centre content is not horizontally scaled

#### Scenario: The field is stretched or blank

- **WHEN** the wide frame only scales the 4:3 picture or the margins are empty
- **THEN** the result is classified as stretched or missing content
- **AND** the report does not claim a wider field

### Requirement: HUD readability

The change SHALL keep the BG3 HUD readable in the wide frame by clamping or
anchoring it, and SHALL record its placement.

#### Scenario: HUD remains readable

- **WHEN** the wide frame is captured
- **THEN** `RACE`, the timer and other BG3 text are legible
- **AND** the placement policy is recorded

#### Scenario: HUD is unreadable

- **WHEN** the HUD is clipped, stretched or misplaced
- **THEN** the report records the defect
- **AND** recommends the P2c follow-up rather than claiming success

### Requirement: HDMA builder and P-U1

The change SHALL trace the HDMA table builder and SHALL either implement the
per-line margin extension or SHALL document it as blocked with evidence.

#### Scenario: Builder is found

- **WHEN** the writes to the HDMA source tables are traced
- **THEN** the builder site and table layout are recorded
- **AND** the report states whether the margin extension was implemented

#### Scenario: Builder is not found

- **WHEN** the trace does not identify the builder
- **THEN** P-U1 is recorded as blocked
- **AND** the wide frame is reported as renderer-only with native curves

### Requirement: Deferred object gaps

The change SHALL document the object, cull and spawn symptoms left by P7–P13 and
SHALL NOT claim that those patterns are implemented.

#### Scenario: Gaps are recorded

- **WHEN** the wide capture is reviewed
- **THEN** objects disappearing at the native edge or spawning late are recorded
- **AND** the next change is named

#### Scenario: A gap is accidentally addressed

- **WHEN** the change touches cull, OAM or spawn behavior
- **THEN** that work is recorded as out of scope
- **AND** separated into a follow-up before being claimed

### Requirement: Simulation unchanged and report

The change SHALL verify that camera, collision, RNG, input and save-state state
are unchanged, and SHALL produce `report.md` with the evidence, limits and next
steps.

#### Scenario: Simulation is unchanged

- **WHEN** 4:3 and wide runs are compared
- **THEN** simulation state matches at aligned frames
- **AND** the report states that wide mode is presentation-only

#### Scenario: Simulation changes

- **WHEN** camera, collision, RNG or input differs
- **THEN** the change is reverted
- **AND** the report records the divergence

