## MODIFIED Requirements

### Requirement: Right-margin riders keep their true X

The system SHALL draw a rider admitted by the widened right cull at its true
screen X in the right margin. It SHALL NOT draw the rider 256 px to the left.
It SHALL NOT draw a parked/culled rider in either margin. The fix SHALL modify
only the PPU's uploaded OAM copy and high table and SHALL NOT write guest
memory.

#### Scenario: Rival enters the right margin

- **WHEN** the CPU rival's true screen X is in `[256, 256 + E)` and the game
  uploads it without the OAM X high bit
- **THEN** the rival is drawn in the right margin at that X
- **AND** no rider sprite appears in `[0, E)` of the native view for that slot

#### Scenario: Rider on screen or in the left margin

- **WHEN** a rider's screen X is in `[-E, 256)`
- **THEN** its OAM entry is drawn exactly as the game uploaded it

#### Scenario: Rider culled

- **WHEN** the game parks a culled rider with the sentinel entry
  (`X = $170`, `Y = $70`, high bit set)
- **THEN** that slot is moved below the field in the PPU copy
- **AND** no part of the parked sprite is drawn in the left or right margin at
  any `E` up to the 512 px cap, including `E = 128`
- **AND** the decision does not depend on the rider's WRAM position

#### Scenario: Real rider shares the sentinel low byte

- **WHEN** a rider's uploaded entry has the high bit clear, or its low byte or
  Y differs from the parking sentinel
- **THEN** it is treated as a real rider and is not hidden by the parked
  sentinel rule

#### Scenario: Ambiguity not resolvable

- **WHEN** the WRAM-derived X does not clearly prefer `lo + 256` over `lo`
- **THEN** the slot is left unchanged by the high-bit fix
