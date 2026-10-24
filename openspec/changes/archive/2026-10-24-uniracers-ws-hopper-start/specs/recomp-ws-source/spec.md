## ADDED Requirements

### Requirement: Header start seed

The source anchor SHALL be seedable from the `$7F` header start pair (`$7F:0002–0009`, big-endian table cells) and the viewport's live scroll and rider screen X. The seed SHALL be a candidate anchor, not an unchecked value: the Y anchor SHALL be chosen by table agreement over the first verified samples, and the existing self-check SHALL confirm or drop the seed. The general search SHALL remain the fallback.

#### Scenario: Seed reproduces the anchor

- **WHEN** the header start X and the viewport scroll/rider are valid
- **THEN** `ax = header_x - ((WsRingScrollH(vp) + rider_screen_x) >> 4)`
- **AND** the value matches the anchor the existing search finds later on the measured Dragster and Hopper runs

#### Scenario: Y from samples

- **WHEN** the first verified samples arrive
- **THEN** the best table-agreement `ay` at the seeded `ax` is used
- **AND** the anchor is installed only if the self-check agrees

#### Scenario: Invalid or ambiguous header

- **WHEN** the header pair is outside the grid, the scroll is not valid, or the Y agreement is not clear
- **THEN** no seed is installed
- **AND** the existing `WsSrcSearch` runs unchanged

#### Scenario: No wrong cells

- **WHEN** the seed is active
- **THEN** a margin cell is still served only where the live anchors agree
- **AND** the BG1 probe reports zero wrong cells

#### Scenario: 1P and 2P

- **WHEN** the race is 1P or 2P/VS
- **THEN** the seed is per viewport
- **AND** the 1P frames and the 2P tested widths remain unchanged
- **AND** the 1P source diagnostic may be re-recorded for the cull-gate timing without changing the frame or wrong-cell score
