## ADDED Requirements

### Requirement: Per-viewport ring and anchor

The system SHALL keep the ring observation state (BG1 map base and BG mode latch, scene generation, scroll unwrap, seen table) and the source anchor state (anchor set, scene generation, race start) separately for each viewport of the frame, and SHALL decide each margin sample with the state of its line's viewport. The track grid header and the piece-library word index SHALL be shared. A 1P race SHALL use a single viewport covering the whole frame and SHALL produce the same output as before.

#### Scenario: 1P single viewport

- **WHEN** a 1P race is drawn with `source`
- **THEN** viewport 0 starts on line 1 and covers every line
- **AND** every frame hash of the 1P validation cases equals its existing reference

#### Scenario: Independent viewports

- **WHEN** a 2P race is drawn with `source`
- **THEN** each viewport latches its BG1 map base, BG mode and scroll on its own first line
- **AND** a scene reset, seen entry or anchor change in one viewport does not change the other

#### Scenario: Anchor per viewport

- **WHEN** the two viewports' unwrapped scroll origins differ
- **THEN** each viewport searches and self-checks its own anchor from its own verified cells

#### Scenario: Radar accessors

- **WHEN** the radar queries the scroll and anchor
- **THEN** it receives viewport 0's values, as before
