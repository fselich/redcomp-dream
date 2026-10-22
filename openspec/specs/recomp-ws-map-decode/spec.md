# recomp-ws-map-decode Specification

## Purpose
TBD - created by archiving change uniracers-ws-map-decode. Update Purpose after archive.
## Requirements
### Requirement: WRAM map decode

The spike SHALL produce a documented function that maps a world cell to its BG1
tile word using only a WRAM snapshot taken after race load.

#### Scenario: Probe agreement

- **WHEN** the function is evaluated on every world cell seen natively in the
  probe captures of Dragster, Zoom Zoo, Bowl, Switcher and Monster
- **THEN** it matches the probe word on every cell it claims to cover
- **AND** the share of covered cells is reported per track

#### Scenario: Layout documented

- **WHEN** the decode is reported
- **THEN** the piece size, row pitch, placement data and world-origin mapping
  are written down with the addresses they were found at

### Requirement: Loader and ROM source

The spike SHALL identify how bank `$7F` is filled at race load, or SHALL record
why it could not.

#### Scenario: Loader found

- **WHEN** the write watch on `$7F` during track load is analysed
- **THEN** the writer routine, its ROM source pointers and the data format are
  recorded

#### Scenario: Loader not found

- **WHEN** the source cannot be traced within the time box
- **THEN** the report records the watched ranges, the observations and the next
  step

### Requirement: Offline full-track render

The spike SHALL render the BG1 layer of each 1P track to an image from decoded
data.

#### Scenario: Render from WRAM

- **WHEN** a race-start WRAM snapshot is decoded
- **THEN** a full-track BG1 image is produced
- **AND** a native-frame crop of it matches the emulator's BG1 output for the
  same world position

#### Scenario: Topology check

- **WHEN** the image is compared with the `maps/` line-art
- **THEN** the track topology agrees, or the differences are recorded

### Requirement: Isolation from parallel work

The spike SHALL NOT modify the shared project tree or its build directory.

#### Scenario: Instrumentation

- **WHEN** a runtime hook or watch is needed
- **THEN** it is added only to `work/ws-map-decode/project/`

### Requirement: Decision report

The spike SHALL end with a report covering decode status, a runtime-source
go/no-go, ultrawide feasibility and track-editing feasibility.

#### Scenario: Go

- **WHEN** the WRAM decode reaches 100 % agreement on covered cells and at least
  95 % coverage
- **THEN** the report recommends a runtime `source` change and estimates its cost

#### Scenario: No-go

- **WHEN** coverage stays under 95 % after the time box
- **THEN** the report records what is decoded, what is not, and the cheapest next
  step

