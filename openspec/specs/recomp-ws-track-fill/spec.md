# recomp-ws-track-fill Specification

## Purpose

Records the first BG1 track-fill investigation (stream decode, generator probes) that ended with the clamp fallback. A finished investigation; superseded by recomp-ws-source.

## Requirements
### Requirement: BG1 stream decoded

The change SHALL characterize the BG1 tilemap writes, DMA sources/destinations,
staging buffers and world-column mapping before implementing a fill.

#### Scenario: Map writes identified

- **WHEN** a loop/slope frame is traced
- **THEN** the writes landing in the BG1 map are distinguished from CHR writes
- **AND** their world columns are recorded

#### Scenario: Mapping is assumed

- **WHEN** a fill is applied without a decoded world-column mapping
- **THEN** the change is rejected
- **AND** no duplicate track is introduced

### Requirement: True column source or documented absence

The change SHALL locate a true source for off-native columns (ROM/WRAM) or
SHALL record that none exists.

#### Scenario: Source exists

- **WHEN** the stream pointer or level strip contains the future/off-native
  columns
- **THEN** the source and world mapping are recorded
- **AND** the fill can read them read-only

#### Scenario: Source does not exist

- **WHEN** the 256-wide ring is the only data
- **THEN** the report records the absence
- **AND** clamp remains the safe mode

### Requirement: Source-backed fill

The change SHALL prefill only true columns into the host shadow and SHALL NOT
sample the wrapped map in the margins.

#### Scenario: Loop has no fragment

- **WHEN** the wide loop/slope is rendered with fill
- **THEN** no disconnected or smeared segment appears
- **AND** the track continues from real data

#### Scenario: Fill duplicates track

- **WHEN** a margin column is wrong or duplicated
- **THEN** the fill is reverted
- **AND** the frame/world cell is recorded

### Requirement: Clamp fallback

The change SHALL keep `SNESRECOMP_WS_TRACK=clamp` and `off` and SHALL fall back
to clamp when the source is unavailable.

#### Scenario: Source unavailable

- **WHEN** a frame cannot be mapped
- **THEN** clamp is used for that frame
- **AND** the limitation is recorded

#### Scenario: Mode independence

- **WHEN** HUD/cull switches change
- **THEN** track fill/clamp behavior is unaffected

### Requirement: 4:3 and simulation preserved

The change SHALL keep 4:3, camera, scroll, RNG and input unchanged.

#### Scenario: 4:3 exact

- **WHEN** widescreen is off
- **THEN** frames match the preserved baseline

#### Scenario: Simulation changes

- **WHEN** camera, scroll or simulation differs
- **THEN** the change is reverted
- **AND** the first divergent frame is recorded

### Requirement: Fill evidence

The change SHALL capture loops/slopes in both directions and both margins with
native, clamp and fill comparisons.

#### Scenario: Fill accepted

- **WHEN** fill has no fragment and 4:3 is exact
- **THEN** the report records the source/mapping and adopts fill
- **AND** clamp stays as fallback

#### Scenario: Fill rejected

- **WHEN** fragments or wrong columns remain
- **THEN** clamp is retained
- **AND** the next diagnostic is recorded

### Requirement: Decision report

The change SHALL produce `report.md` with stream evidence, source mapping,
fill/clamp result, 4:3 and residual limitations.

#### Scenario: Source-backed fill complete

- **WHEN** the fill passes
- **THEN** the report records the source address/pointer and world mapping

#### Scenario: Not decodable

- **WHEN** the stream cannot be decoded
- **THEN** the report records the evidence gap
- **AND** keeps clamp

