# recomp-ws-source Specification

## Purpose
TBD - created by archiving change uniracers-ws-source. Update Purpose after archive.
## Requirements
### Requirement: Table-sourced margin cells

With `SNESRECOMP_WS_OFFWINDOW=source` in a live 16:9 race, the system SHALL
serve each BG1 margin cell that is not ring-verified from the game's track
table in WRAM `$7F`, at the world cell plus the race anchor.

#### Scenario: Ring-verified cell

- **WHEN** the ring cell was seen natively with the same world cell and tile
  word
- **THEN** the ring tile is drawn, as in `ring` mode

#### Scenario: Leading-edge cell

- **WHEN** the ring cell is stale and the anchor is valid
- **THEN** the table word for that world cell is drawn

#### Scenario: Outside the grid

- **WHEN** the table position falls outside the piece-id grid
- **THEN** `0x0000` is drawn, which is what the game shows there

### Requirement: Validated anchor

The system SHALL establish the offset between the world key and table
coordinates from verified native cells. It SHALL accept up to eight offsets
that each reach at least 95 % agreement. Cells reading `0x0000` and words absent
from the piece library SHALL NOT count as evidence.

#### Scenario: Unique anchor

- **WHEN** exactly one offset matches at least 95 % of the samples
- **THEN** it becomes the race anchor

#### Scenario: Ambiguous anchor

- **WHEN** two to eight offsets qualify
- **THEN** all are kept
- **AND** a margin cell is drawn from the table only where every kept offset
  gives the same word, otherwise it is masked

#### Scenario: No anchor

- **WHEN** no offset qualifies, more than eight qualify, or fewer than eight
  usable samples exist
- **THEN** margins behave as in `ring` mode
- **AND** the search is retried on a later frame

#### Scenario: Invalid header

- **WHEN** the `$7F` header dimensions do not describe a 0x8000-byte grid
- **THEN** no anchor is set and margins behave as in `ring` mode

### Requirement: Continuous self-check

The system SHALL compare verified native cells against the table every frame
and SHALL drop any anchor whose agreement falls below 95 %.

#### Scenario: Disagreement

- **WHEN** fewer than 95 % of the checked cells match the table for an anchor
- **THEN** that anchor is dropped and the event is logged
- **AND** when no anchor remains, margins are masked and a re-anchor starts

#### Scenario: Scene change

- **WHEN** the BG1 map base or BGMODE changes, or the race stops being live
- **THEN** the anchor is invalidated

### Requirement: Zero wrong cells

The system SHALL NOT draw a margin cell that differs from the probe ground
truth.

#### Scenario: Five-track validation

- **WHEN** Dragster (→ and ←), Zoom Zoo, Bowl, Switcher and Monster are scored
  with the probe
- **THEN** wrong served cells are 0
- **AND** the leading-side masked count and the anchor latency are reported

### Requirement: Presentation-only and cheap

The system SHALL NOT write guest memory or VRAM, SHALL keep native 4:3
unchanged, and SHALL add negligible cost.

#### Scenario: Native output

- **WHEN** widescreen is off
- **THEN** native frame 2500 SHA-256 is `3417f91a…df44`

#### Scenario: Guest state

- **WHEN** Dragster runs to frame 2500 in `source` and in `ring`
- **THEN** WRAM, VRAM, CPU and DMA dumps are identical

#### Scenario: Cost

- **WHEN** 2,500 Dragster frames run in `source`
- **THEN** process CPU time exceeds `ring` by less than 0.2 ms per frame on
  average

