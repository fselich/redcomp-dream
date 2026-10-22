## ADDED Requirements

### Requirement: Full level table characterized

The spike SHALL characterize the full level table that backs BG1 before any
wide renderer is attempted.

#### Scenario: Table layout recorded

- **WHEN** a loop frame is captured
- **THEN** the table base(s), stride, row count, column count and entry format
  are recorded
- **AND** blank (`$00A0`) versus track entries are distinguishable

#### Scenario: Ring confused with level

- **WHEN** the 32×32 VRAM map is treated as the whole level
- **THEN** the spike is rejected
- **AND** the table mapping is re-derived

### Requirement: World-to-screen mapping proven

The spike SHALL prove the mapping between table column/row, world position and
native screen coordinates.

#### Scenario: Native columns reproduced

- **WHEN** the mapping is applied to the visible 4:3 columns
- **THEN** the decoded cells match the native BG1 tile entries
- **AND** the error is recorded in tiles/pixels

#### Scenario: Mapping guessed

- **WHEN** margin columns are generated from unverified offsets
- **THEN** no wide result is accepted
- **AND** clamp remains the default

### Requirement: External full-map oracle

The spike SHALL use the published full-track maps only as an external
validation reference.

#### Scenario: Whole-track shape compared

- **WHEN** a decoded track is assembled
- **THEN** its loops, slopes and connections are compared with the full map
- **AND** mismatches are recorded

#### Scenario: External image leakage

- **WHEN** full-map images are copied into the repository or build artifacts
- **THEN** the change is rejected
- **AND** the images are removed

### Requirement: Margin columns sourced from real data

The spike SHALL generate margin columns from the real level table and generator,
not from wrap, mirroring or stretching.

#### Scenario: True margin columns

- **WHEN** the ±43 px margins are rendered
- **THEN** each margin cell comes from a decoded table/genuine generator cell
- **AND** no wrapped or duplicated track appears

#### Scenario: Generator unavailable

- **WHEN** stage 1/2 cannot produce a margin cell
- **THEN** the spike records the gap and falls back to clamp
- **AND** does not synthesize false track

### Requirement: Test-only wide prototype

The spike SHALL keep the wide-map prototype test-only and independent of the
accepted clamp default.

#### Scenario: Prototype isolated

- **WHEN** the prototype is enabled
- **THEN** it is selected by a dedicated test switch
- **AND** HUD, cull and clamp switches remain independent

#### Scenario: Prototype fails

- **WHEN** the mapping or rendering is wrong
- **THEN** clamp remains the shipping default
- **AND** no production path depends on the prototype

### Requirement: 4:3 and simulation preserved

The spike SHALL keep 4:3, camera, scroll, RNG, input and gameplay unchanged.

#### Scenario: 4:3 exact

- **WHEN** widescreen and the prototype are off
- **THEN** frames match the preserved baseline

#### Scenario: Simulation divergence

- **WHEN** camera, scroll or simulation differs
- **THEN** the spike is rejected
- **AND** the first divergent frame is recorded

### Requirement: Go/no-go report

The spike SHALL produce a report that decides whether a production native-map
pipeline is justified.

#### Scenario: Spike passes

- **WHEN** native cells match, true margins have no fragments and 4:3 is exact
- **THEN** the report recommends a production change with the decoded mapping

#### Scenario: Spike fails

- **WHEN** the table or mapping cannot be proven
- **THEN** the report records the blocker
- **AND** clamp remains the final accepted behavior
