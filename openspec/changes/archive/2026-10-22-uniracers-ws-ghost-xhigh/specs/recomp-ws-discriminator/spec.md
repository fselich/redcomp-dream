## ADDED Requirements

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
