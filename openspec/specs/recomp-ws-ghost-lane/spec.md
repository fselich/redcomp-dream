# recomp-ws-ghost-lane Specification

## Purpose
TBD - created by archiving change uniracers-ws-ghost-lane. Update Purpose after archive.
## Requirements
### Requirement: Ghost lane harvest

With `SNESRECOMP_WS_OFFWINDOW=ghost` in a live 16:9 race, the system SHALL keep
a speculative copy of the machine ahead of the real one, and SHALL record the
BG1 ring cells that copy displays, keyed by world cell.

#### Scenario: Lead maintained

- **WHEN** the input is held and no resync is due
- **THEN** the ghost stays `SNESRECOMP_WS_GHOST_AHEAD` frames (default 4) ahead
- **AND** advances at most `SNESRECOMP_WS_GHOST_MAXSTEPS` frames per real frame

#### Scenario: Resync

- **WHEN** the packed input changes, the race scene resets, eligibility toggles
  or `SNESRECOMP_WS_GHOST_RESYNC` frames have passed
- **THEN** the ghost restarts from the current real state

#### Scenario: Not eligible

- **WHEN** widescreen is off, the race is not live, netplay is active or the
  framework run-ahead is enabled
- **THEN** no ghost frame runs and the mode behaves as `ring`

### Requirement: Leading cells served by exact world match

The margin hook SHALL serve a harvested cell only when its world cell equals the
world cell the margin pixel needs. Cells verified by Option A take precedence,
and everything else stays masked.

#### Scenario: Leading cell harvested

- **WHEN** a leading margin cell fails the Option A check and the ghost table
  holds the same world cell
- **THEN** the harvested map word is drawn

#### Scenario: No harvest

- **WHEN** neither the ring nor the ghost table has the world cell
- **THEN** the cell is masked to background

### Requirement: Real lane unchanged

Running the ghost lane SHALL NOT change the real machine's state or its native
output.

#### Scenario: Per-frame state trace

- **WHEN** Dragster runs 2,500 frames in `ring` and in `ghost`
- **THEN** the `SNESRECOMP_STATE_TRACE` files are identical

#### Scenario: Frame dumps and native pixels

- **WHEN** frame 2500 is dumped in both modes
- **THEN** WRAM, VRAM, CPU and DMA dumps are identical
- **AND** the native 256 px window pixels are identical

#### Scenario: Native 4:3

- **WHEN** widescreen is off
- **THEN** frame 2500 SHA-256 is `3417f91a…df44`

#### Scenario: Host-side state

- **WHEN** a ghost frame runs
- **THEN** the beam driver execution state, `snes_frame_counter`, audio producer
  cursor, speculative flag and game host statics are restored before the real
  frame is drawn

### Requirement: Zero wrong cells and measured coverage

The system SHALL serve no margin cell that differs from the probe ground truth,
and the report SHALL record the coverage gained over `ring`.

#### Scenario: Five tracks

- **WHEN** Dragster (→ and ←), Zoom Zoo, Bowl, Switcher and Monster are scored
  with `world_key_check.py --served --mask-log`
- **THEN** wrong served cells are 0 on every run
- **AND** leading-side served/masked counts are reported against `ring`

#### Scenario: Rendered pixels

- **WHEN** a ghost-served cell later becomes natively visible
- **THEN** its rendered 16×16 block in the margin equals the native block

### Requirement: Cost budget

The ghost lane SHALL stay within a measured per-frame budget.

#### Scenario: Average cost

- **WHEN** 2,500 Dragster frames run in `ghost`
- **THEN** process CPU time exceeds `ring` by at most 5 ms per frame on average
- **AND** the p99 and worst `hook_ms` are reported

