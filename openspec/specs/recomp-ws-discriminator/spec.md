# recomp-ws-discriminator Specification

## Purpose
TBD - created by archiving change uniracers-ws-discriminator. Update Purpose after archive.
## Requirements
### Requirement: State capture

The change SHALL capture WRAM state for menu, live race and pause from the same
build and route, or SHALL record the closest reachable states if pause is not
available.

#### Scenario: All states are captured

- **WHEN** the route is run with scene dumps enabled
- **THEN** menu, race and pause (or the documented substitutes) have WRAM dumps
- **AND** their frame numbers and route positions are recorded

#### Scenario: Pause is not reachable

- **WHEN** the pause input does not produce a distinct state
- **THEN** the actual behavior is recorded
- **AND** the verification uses the closest reachable states with the gap noted

### Requirement: Candidate discovery

The change SHALL search for a WRAM byte or word, or a verified combination of a
race-mode byte and a pause flag, that distinguishes live race from menu and
pause, and SHALL exclude PPU/HDMA mirrors and player/camera-state bytes from the
primary gate.

#### Scenario: Candidate is found

- **WHEN** the dumps are compared
- **THEN** the candidate address(es), values and neighborhood are recorded
- **AND** it has a plausible game-state meaning

#### Scenario: No candidate is found

- **WHEN** no stable byte distinguishes live race
- **THEN** the change records the failed candidates
- **AND** uses a documented combination with its risk

### Requirement: Cross-state verification

The change SHALL verify the candidate, or the combination, across menu, live
race, pause and jump states, and SHALL reject a candidate that changes inside
live race, toggles with player state, or matches the pause value.

#### Scenario: Candidate verifies

- **WHEN** the candidate or combination is watched across the states
- **THEN** it holds a race-only value for the race window
- **AND** differs in menu and pause and is stable during jumps

#### Scenario: Candidate fails

- **WHEN** the candidate oscillates, matches menu/pause, or toggles with player
  state
- **THEN** it is rejected
- **AND** the next candidate or combination is tested

### Requirement: Gameplay-gated wide mode

The change SHALL gate the 342×224 frame and the BG3 clamp on the verified live
race predicate, with the developer flag still required.

#### Scenario: Wide mode activates

- **WHEN** the wide flag is on and the game enters live race
- **THEN** the frame becomes 342×224 and the HUD clamp is applied
- **AND** the switch happens at a frame boundary

#### Scenario: Wide mode deactivates

- **WHEN** the game leaves live race (menu, pause or transition)
- **THEN** the frame returns to 256×224
- **AND** the BG3 clamp is removed

### Requirement: 4:3 and simulation preserved

The change SHALL keep 4:3 as the default and SHALL verify that camera,
collision, RNG, input and save-state behavior are unchanged.

#### Scenario: 4:3 is preserved

- **WHEN** the wide flag is off or the game is outside live race
- **THEN** the frame is 256×224 and matches the baseline
- **AND** no simulation state differs

#### Scenario: Simulation changes

- **WHEN** the gate changes camera, collision, RNG or input
- **THEN** the change is reverted
- **AND** the divergence is recorded

### Requirement: Decision report

The change SHALL produce `report.md` with the state captures, candidate
address(es) and values, verification evidence, gate behavior, residual risk and
remaining widescreen blockers.

#### Scenario: Gate is proven

- **WHEN** the candidate verifies and the gate works
- **THEN** the report records the address and the menu/race/pause behavior
- **AND** names the next widescreen change

#### Scenario: Gate is not proven

- **WHEN** no candidate verifies
- **THEN** the report states the fallback and risk
- **AND** does not claim a player-facing gate

### Requirement: Live-race predicate holds on every tour

The live-race predicate SHALL NOT depend on a value that identifies the tour,
the track or the race type. It SHALL be true in a live 1P race on every tour
and false in menus, pause and at boot.

#### Scenario: Later tour

- **WHEN** a live 1P race runs on a tour other than tour 1 (`$0B4 ≠ 1`)
- **THEN** wide mode activates as on tour 1

#### Scenario: Menus and boot

- **WHEN** the game shows a menu (`$0300 > 1`) or has just booted (`$0B4 = 0`)
- **THEN** wide mode stays off

