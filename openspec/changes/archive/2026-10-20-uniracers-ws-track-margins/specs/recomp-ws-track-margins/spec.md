## ADDED Requirements

### Requirement: Track margin failure characterized

The change SHALL reproduce and characterize the non-horizontal track-margin
fragments before changing the renderer.

#### Scenario: Native is continuous

- **WHEN** a loop/slope frame is captured in 4:3
- **THEN** the track is continuous
- **AND** the wide frame can be compared at the same landmark

#### Scenario: Wide shows a fragment

- **WHEN** the wide frame is captured
- **THEN** the detached segment is isolated to the track BG layer
- **AND** the map size/scroll/wrap is recorded

### Requirement: True margin data identified

The change SHALL determine whether the game retains true off-native track
columns and SHALL NOT synthesize track geometry that does not exist.

#### Scenario: Retained data exists

- **WHEN** history/streaming/level data contains the off-native columns
- **THEN** the fill source and scroll key are recorded
- **AND** the fill is read-only with respect to simulation

#### Scenario: No retained data

- **WHEN** the 256-wide map is the whole authored world
- **THEN** the change records that proper fill is impossible
- **AND** uses the safe clamp instead

### Requirement: Proper fill or safe clamp

The change SHALL implement either a retained-data margin fill or a native clamp
for the track layer, with no wrapped-map fragments.

#### Scenario: Fill works

- **WHEN** the wide loop/slope is rendered with fill
- **THEN** the margins continue the real track
- **AND** no wrap fragment appears

#### Scenario: Clamp works

- **WHEN** the track is clamped
- **THEN** BG1 stays native and the margins show background
- **AND** the limitation is recorded

### Requirement: Independent track switch

The change SHALL provide `SNESRECOMP_WS_TRACK` with `fill`, `clamp` and `off`
modes.

#### Scenario: Fallback is available

- **WHEN** fill is wrong or unavailable
- **THEN** `clamp` removes the fragments while HUD/cull stay active

#### Scenario: Off mode

- **WHEN** `off` is selected
- **THEN** the pre-change sampling is reproduced for comparison
- **AND** it is not the default

### Requirement: 4:3 and simulation preserved

The change SHALL keep 4:3, RNG, progression, camera and input unchanged.

#### Scenario: 4:3 exact

- **WHEN** widescreen is off
- **THEN** frames match the preserved baseline

#### Scenario: Simulation changes

- **WHEN** camera, scroll or simulation differs
- **THEN** the change is reverted
- **AND** the first divergent frame is recorded

### Requirement: Track margin evidence

The change SHALL capture loops/slopes in both directions and both margins, with
native and wide comparisons.

#### Scenario: No broken track

- **WHEN** the wide captures are reviewed
- **THEN** no disconnected track fragment appears in the margins
- **AND** the final mode is stated

#### Scenario: Fragment remains

- **WHEN** a fragment remains
- **THEN** the frame/slot/layer is recorded
- **AND** the change is not claimed complete

### Requirement: Decision report

The change SHALL produce `report.md` with the map/scroll evidence, fill or clamp
decision, switch, 4:3 result and residual limitation.

#### Scenario: Fill adopted

- **WHEN** retained-data fill passes
- **THEN** the report records the source and scroll key

#### Scenario: Clamp adopted

- **WHEN** no retained data exists
- **THEN** the report records the clamp and the lost margin coverage
