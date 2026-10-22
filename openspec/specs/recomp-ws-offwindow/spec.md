# recomp-ws-offwindow Specification

## Purpose
TBD - created by archiving change uniracers-ws-offwindow. Update Purpose after archive.
## Requirements
### Requirement: Ring formula proven

The change SHALL derive and validate the BG1 ring formula that maps screen
columns to map columns.

#### Scenario: Native columns reproduced

- **WHEN** the formula is applied to native 4:3
- **THEN** the sampled BG1 cells match the rendered native frame
- **AND** the tile phase and modulus are recorded

#### Scenario: Tile size taken from the renderer

- **WHEN** the formula assumes a BG1 tile size
- **THEN** the size is the one the renderer used on the probed lines
  (`BGMODE` bit 4), not a register dump taken outside the race

#### Scenario: Formula guessed

- **WHEN** off-window columns are sampled without a validated formula
- **THEN** the change is rejected
- **AND** clamp remains the fallback

### Requirement: Same-run evidence

The change SHALL capture pre/post data in the same process.

#### Scenario: Cross-run comparison

- **WHEN** pre-DMA and post-DMA snapshots come from different runs
- **THEN** they are not accepted as mapping evidence

#### Scenario: Same-run anchor

- **WHEN** stage, DMA and VRAM are captured in one run
- **THEN** the row/x anchor and transfer timing are recorded

### Requirement: Real off-window source

The change SHALL source margin cells from real level data, not wrap,
mirroring, edge-repeat or stretching. Ring cells written by the game are real
level data when their world key matches the native ground truth.

#### Scenario: True columns

- **WHEN** a margin column is rendered
- **THEN** it comes from decoded table/segment data or from a ring cell whose
  world key is verified
- **AND** no duplicated or wrapped track appears

#### Scenario: Trailing ring cells

- **WHEN** a trailing-side margin cell is served from the BG1 ring
- **THEN** the world-key check reports 0 wrong cells for that side on the
  validated captures

#### Scenario: Source unavailable

- **WHEN** the adjacent segment cannot be sourced
- **THEN** the change falls back to clamp
- **AND** records the gap

### Requirement: Topology oracle

The change SHALL use the local `maps/` line-art only for track topology and
visibility decisions.

#### Scenario: Section classification

- **WHEN** a frame is classified horizontal or vertical
- **THEN** the expected margin content is recorded from the line-art

#### Scenario: Tile validation

- **WHEN** tile-level accuracy is asserted
- **THEN** it is not claimed from the line-art
- **AND** runtime evidence is required

### Requirement: Test-only prototype

The change SHALL keep the off-window prototype isolated behind a dedicated
switch.

#### Scenario: Switch independence

- **WHEN** HUD, cull, clamp or repeat switches change
- **THEN** the off-window switch remains independent

#### Scenario: Prototype failure

- **WHEN** the prototype is wrong
- **THEN** clamp remains the default
- **AND** no production path depends on it

### Requirement: 4:3 and simulation preserved

The change SHALL keep native 4:3, camera, scroll, RNG, input and gameplay
unchanged.

#### Scenario: 4:3 exact

- **WHEN** widescreen and the prototype are off
- **THEN** frames match the preserved baseline

#### Scenario: Simulation divergence

- **WHEN** camera, scroll or simulation differs
- **THEN** the change is rejected
- **AND** the first divergent frame is recorded

### Requirement: Go/no-go report

The change SHALL produce a report that decides whether true off-window track is
implementable.

#### Scenario: Pass

- **WHEN** the five tracks pass and 4:3 is exact
- **THEN** the report recommends a production implementation

#### Scenario: Fail

- **WHEN** the ring formula or source cannot be proven
- **THEN** the report records the blocker
- **AND** clamp remains the documented fallback

### Requirement: Pixel-source probe

The change SHALL measure ring and margin claims with a renderer pixel-source
probe that logs the VRAM map word each drawn span came from.

#### Scenario: Probe off

- **WHEN** the probe switch is unset
- **THEN** native 4:3 output is bit-identical to the baseline

#### Scenario: Probe evidence

- **WHEN** a ring, phase or margin claim is recorded
- **THEN** it cites probe captures from the same run
- **AND** the capture frames and switches are recorded

