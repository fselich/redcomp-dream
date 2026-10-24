## ADDED Requirements

### Requirement: Two-player rider OAM

In a live 2P/VS split-screen race the system SHALL handle OAM slots 96..99 as the four staged rider entries: 96 P2 bottom, 97 P1 bottom, 98 P1 top, 99 P2 top, with the bottom camera (`$041B/$041F`) and a +112 Y offset for slots 96/97 and the top camera (`$0419/$041D`) for slots 98/99. For each slot that matches a rider entry it SHALL, in order: hide the half's parking sentinel; leave an entry with the high bit set (unless the measurement shows the game sets it for a real positive X, in which case it SHALL clear the bit when the WRAM-derived X clearly prefers `lo`); leave an entry with `lo >= E`; set the missing X high bit when the WRAM-derived X clearly prefers `lo + 256` within the one-frame slack; otherwise leave it. It SHALL modify only the PPU's OAM copy and high table, and SHALL NOT write guest memory. A frame-level pass SHALL do the above; a per-line high-table pass SHALL set the line's half rider slots to the big size bit and the true X high bit before each line is drawn, because the single end-of-frame high table stages the bottom entries small.

#### Scenario: P1 top right margin

- **WHEN** P1's true top-half screen X is in `[256, 256+E)` and slot 98 was uploaded without the high bit
- **THEN** slot 98's high bit is set in the PPU copy
- **AND** the rider is drawn at the true X in the top right margin

#### Scenario: P2 bottom right margin

- **WHEN** P2's true bottom-half screen X is in `[256, 256+E)` and slot 96 was uploaded without the high bit
- **THEN** slot 96's high bit is set in the PPU copy
- **AND** the rider is drawn at the true X in the bottom right margin

#### Scenario: Per-line bottom size

- **WHEN** a bottom line is drawn and slot 96/97 is a real rider
- **THEN** the PPU high table's size bit for that slot is set before the line is drawn
- **AND** the bottom rider is drawn big, not missing or as a small fragment

#### Scenario: Left margin

- **WHEN** a real rider's true screen X is in `[-E, 0)`
- **THEN** the game's high bit is preserved
- **AND** the rider is drawn at the true X

#### Scenario: Parking sentinel

- **WHEN** a top slot carries `($70,$70)` or a bottom slot carries `($30,$30)` with the measured sentinel high bit
- **THEN** that slot is moved below the field in the PPU copy
- **AND** the decision does not depend on WRAM
- **AND** no part of it is drawn in either margin at any `E` up to 128

#### Scenario: Sentinel cannot be a real rider

- **WHEN** a real rider is staged inside the vertical window `[-41, 112)` of its camera
- **THEN** its staged Y is never 112 in a top slot and never 48 in a bottom slot, so the sentinel rule never hides it

#### Scenario: High bit on a positive rider

- **WHEN** a rider entry has the high bit set and the WRAM-derived X is positive and clearly prefers `lo` over the decoded high-bit value
- **THEN** the slot is logged as `hi-positive`
- **AND** the bit is cleared only if the change report shows that the game uploads real riders this way

#### Scenario: No clear match

- **WHEN** the WRAM-derived X does not clearly prefer `lo + 256` over `lo`
- **THEN** the slot is left unchanged

#### Scenario: Unrecognised slot

- **WHEN** the slot's tile and attribute do not match a rider entry
- **THEN** the slot is left unchanged and logged

#### Scenario: 1P untouched

- **WHEN** `$0DDB` is zero
- **THEN** the existing 1P X-high, parked-pair and sentinel fixes run
- **AND** the 2P policy is inactive

## MODIFIED Requirements

### Requirement: High-bit fix has its own gate

The system SHALL apply the rider high-bit fix only in a live widescreen race with the cull widening active and `E > 0`: with the 1P policy when `$0DDB` is zero and with the two-player rider OAM policy when it is not. It SHALL provide the independent switch `SNESRECOMP_WS_OAM_XHIGH=0`, which disables both policies.

#### Scenario: Switch off

- **WHEN** `SNESRECOMP_WS_OAM_XHIGH=0`
- **THEN** frames equal the widescreen output without the OAM fix, in 1P and in 2P
- **AND** the cull widening stays active

#### Scenario: Widescreen off or 4:3

- **WHEN** widescreen is off or the race is not live
- **THEN** output and simulation equal the native baseline

#### Scenario: Split screen

- **WHEN** `$0DDB ≠ 0` and the race is live and wide
- **THEN** the two-player rider OAM policy runs instead of the 1P policy
