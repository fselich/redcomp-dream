## ADDED Requirements

### Requirement: Object path identified

The change SHALL identify the object update, OAM staging and cull path, with the
writer PCs and the OAM slots used by objects, or SHALL document why it could not
be identified.

#### Scenario: Path is found

- **WHEN** the race trace is inspected
- **THEN** the OAM emitter, encoded-X path and cull routines are recorded
- **AND** object vs HUD OAM slots are distinguished

#### Scenario: Path is not found

- **WHEN** the trace does not identify the emitter or cull code
- **THEN** the report records the watched sites and the evidence gap
- **AND** no widening is claimed

### Requirement: Symmetric cull widening

The change SHALL widen each cull window symmetrically while preserving the
routine's own base window.

#### Scenario: Cull widens

- **WHEN** the cull comparison is patched
- **THEN** the condition preserves the base and adds the margin on both sides
- **AND** the report records each routine and its base

#### Scenario: Cull changes progression

- **WHEN** widening alters spawn/progression or RNG
- **THEN** the change is reverted
- **AND** the affected site is recorded

### Requirement: Signed OAM X validation

The change SHALL validate the runner's signed 9-bit OAM X path so riders at
negative screen X are drawn in the left margin, and SHALL NOT add a redundant
reject compare where the game has none.

#### Scenario: Both margins draw

- **WHEN** an object straddles the left or right margin
- **THEN** it remains visible on both sides
- **AND** the report records whether the runner decoded a negative X

#### Scenario: No separate reject exists

- **WHEN** the game writes a signed low byte plus the OAM high bit
- **THEN** the runner's existing decode path is used
- **AND** no new game-side compare is invented

#### Scenario: Only one margin draws

- **WHEN** an object disappears on the left or right margin
- **THEN** the defect is recorded
- **AND** the change is not claimed complete

### Requirement: Independent kill switches

The change SHALL provide independent runtime switches for cull widening and for
any OAM behavior it changes.

#### Scenario: Switches are independent

- **WHEN** one change is disabled
- **THEN** the other remains active
- **AND** the disabled one falls back to authentic 4:3 behavior

#### Scenario: Only cull is widened

- **WHEN** no OAM behavior is changed
- **THEN** only the cull switch is required
- **AND** the report records why no OAM switch exists

#### Scenario: A single global switch is used

- **WHEN** more than one behavior is changed and only one toggle exists
- **THEN** it is rejected
- **AND** separate gates are required

### Requirement: 4:3 and simulation preserved

The change SHALL keep the 4:3 baseline aligned and SHALL verify that simulation,
collision, RNG and input are unchanged.

#### Scenario: 4:3 is preserved

- **WHEN** the wide flag is off or outside live race
- **THEN** frames match the baseline at the documented offset
- **AND** no simulation state differs

#### Scenario: Simulation changes

- **WHEN** camera, collision, RNG or input differs
- **THEN** the change is reverted
- **AND** the divergence is recorded

### Requirement: Wide object evidence

The change SHALL capture a second-track curve with objects and SHALL show them
drawn while straddling both margins.

#### Scenario: Objects survive the margins

- **WHEN** the wide capture is reviewed
- **THEN** objects are visible at the left and right margins
- **AND** no native-edge disappearance is observed

#### Scenario: Objects still pop

- **WHEN** an object disappears at either margin
- **THEN** the first bad frame and slot are recorded
- **AND** the report does not claim P7/P8 complete

### Requirement: Spawn and progression untouched

The change SHALL NOT implement spawn/activation/staging (P9–P13) and SHALL
record any observed pop-in for the following change.

#### Scenario: Spawn is deferred

- **WHEN** the wide capture shows spawn pop-in
- **THEN** it is recorded as P9–P13 work
- **AND** no spawn change is made in this change

#### Scenario: Spawn is accidentally changed

- **WHEN** the change alters spawn or progression
- **THEN** that work is separated into a follow-up
- **AND** not claimed here

### Requirement: Decision report

The change SHALL produce `report.md` with the object path, cull/OAM patches,
kill switches, margin evidence, 4:3 result and remaining P9–P13 gaps.

#### Scenario: P7/P8 complete

- **WHEN** cull and OAM widening pass the margin evidence
- **THEN** the report records the patches and switches
- **AND** names the spawn/staging follow-up

#### Scenario: P7/P8 incomplete

- **WHEN** either margin still clips objects
- **THEN** the report states the residual defect
- **AND** recommends the next diagnostic step
