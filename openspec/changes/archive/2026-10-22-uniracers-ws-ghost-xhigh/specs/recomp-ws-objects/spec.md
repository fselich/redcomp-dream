## ADDED Requirements

### Requirement: Right-margin riders keep their true X

The system SHALL draw a rider admitted by the widened right cull at its true
screen X in the right margin. It SHALL NOT draw the rider 256 px to the left.
The fix SHALL modify only the PPU's uploaded OAM high table and SHALL NOT write
guest memory.

#### Scenario: Rival enters the right margin

- **WHEN** the CPU rival's true screen X is in `[256, 256 + E)` and the game
  uploads it without the OAM X high bit
- **THEN** the rival is drawn in the right margin at that X
- **AND** no rider sprite appears in `[0, E)` of the native view for that slot

#### Scenario: Rider on screen or in the left margin

- **WHEN** a rider's screen X is in `[-E, 256)`
- **THEN** its OAM entry is drawn exactly as the game uploaded it

#### Scenario: Rider culled

- **WHEN** the game parks a culled rider (high bit already set)
- **THEN** the fix leaves that slot unchanged

#### Scenario: Ambiguity not resolvable

- **WHEN** the WRAM-derived X does not clearly prefer `lo + 256` over `lo`
- **THEN** the slot is left unchanged

### Requirement: High-bit fix has its own gate

The system SHALL apply the rider high-bit fix only in a live 1P widescreen race
with the cull widening active. It SHALL provide the independent switch
`SNESRECOMP_WS_OAM_XHIGH=0`.

#### Scenario: Switch off

- **WHEN** `SNESRECOMP_WS_OAM_XHIGH=0`
- **THEN** frames equal the pre-change widescreen output
- **AND** the cull widening stays active

#### Scenario: Widescreen off or 4:3

- **WHEN** widescreen is off or the race is not live
- **THEN** output and simulation equal the native baseline

#### Scenario: Split screen

- **WHEN** `$0DDB ≠ 0`
- **THEN** the fix is inactive
