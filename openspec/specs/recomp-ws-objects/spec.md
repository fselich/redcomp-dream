# recomp-ws-objects Specification

## Purpose

Riders in the widescreen margins: the symmetric cull widening, signed OAM X handling, the X-high fix and its gate, with spawn and progression untouched.
## Requirements
### Requirement: Object path identified

The change SHALL identify the object update, OAM staging and cull path, with the
writer PCs and the OAM slots used by objects, or SHALL document why it could not
be identified.

#### Scenario: Path is found

- **WHEN** the race trace is inspected
- **THEN** the OAM emitter, encoded-X path and cull routines are recorded
- **AND** object vs HUD OAM slots are distinguished

#### Scenario: Path is not found

- **WHEN** the trace does not identify the emitter or cull code
- **THEN** the report records the watched sites and the evidence gap
- **AND** no widening is claimed

### Requirement: Symmetric cull widening

The change SHALL widen each cull window symmetrically while preserving the
routine's own base window.

#### Scenario: Cull widens

- **WHEN** the cull comparison is patched
- **THEN** the condition preserves the base and adds the margin on both sides
- **AND** the report records each routine and its base

#### Scenario: Cull changes progression

- **WHEN** widening alters spawn/progression or RNG
- **THEN** the change is reverted
- **AND** the affected site is recorded

### Requirement: Signed OAM X validation

The change SHALL validate the runner's signed 9-bit OAM X path so riders at
negative screen X are drawn in the left margin, and SHALL NOT add a redundant
reject compare where the game has none.

#### Scenario: Both margins draw

- **WHEN** an object straddles the left or right margin
- **THEN** it remains visible on both sides
- **AND** the report records whether the runner decoded a negative X

#### Scenario: No separate reject exists

- **WHEN** the game writes a signed low byte plus the OAM high bit
- **THEN** the runner's existing decode path is used
- **AND** no new game-side compare is invented

#### Scenario: Only one margin draws

- **WHEN** an object disappears on the left or right margin
- **THEN** the defect is recorded
- **AND** the change is not claimed complete

### Requirement: Independent kill switches

The change SHALL provide independent runtime switches for cull widening and for
any OAM behavior it changes.

#### Scenario: Switches are independent

- **WHEN** one change is disabled
- **THEN** the other remains active
- **AND** the disabled one falls back to authentic 4:3 behavior

#### Scenario: Only cull is widened

- **WHEN** no OAM behavior is changed
- **THEN** only the cull switch is required
- **AND** the report records why no OAM switch exists

#### Scenario: A single global switch is used

- **WHEN** more than one behavior is changed and only one toggle exists
- **THEN** it is rejected
- **AND** separate gates are required

### Requirement: 4:3 and simulation preserved

The change SHALL keep the 4:3 baseline aligned and SHALL verify that simulation,
collision, RNG and input are unchanged.

#### Scenario: 4:3 is preserved

- **WHEN** the wide flag is off or outside live race
- **THEN** frames match the baseline at the documented offset
- **AND** no simulation state differs

#### Scenario: Simulation changes

- **WHEN** camera, collision, RNG or input differs
- **THEN** the change is reverted
- **AND** the divergence is recorded

### Requirement: Wide object evidence

The change SHALL capture a second-track curve with objects and SHALL show them
drawn while straddling both margins.

#### Scenario: Objects survive the margins

- **WHEN** the wide capture is reviewed
- **THEN** objects are visible at the left and right margins
- **AND** no native-edge disappearance is observed

#### Scenario: Objects still pop

- **WHEN** an object disappears at either margin
- **THEN** the first bad frame and slot are recorded
- **AND** the report does not claim P7/P8 complete

### Requirement: Spawn and progression untouched

The change SHALL NOT implement spawn/activation/staging (P9–P13) and SHALL
record any observed pop-in for the following change.

#### Scenario: Spawn is deferred

- **WHEN** the wide capture shows spawn pop-in
- **THEN** it is recorded as P9–P13 work
- **AND** no spawn change is made in this change

#### Scenario: Spawn is accidentally changed

- **WHEN** the change alters spawn or progression
- **THEN** that work is separated into a follow-up
- **AND** not claimed here

### Requirement: Decision report

The change SHALL produce `report.md` with the object path, cull/OAM patches,
kill switches, margin evidence, 4:3 result and remaining P9–P13 gaps.

#### Scenario: P7/P8 complete

- **WHEN** cull and OAM widening pass the margin evidence
- **THEN** the report records the patches and switches
- **AND** names the spawn/staging follow-up

#### Scenario: P7/P8 incomplete

- **WHEN** either margin still clips objects
- **THEN** the report states the residual defect
- **AND** recommends the next diagnostic step

### Requirement: Right-margin riders keep their true X

The system SHALL draw a rider admitted by the widened right cull at its true
screen X in the right margin. It SHALL NOT draw the rider 256 px to the left.
It SHALL NOT draw a parked/culled rider, or the parked 1P 2P-pair slots
(96/97), in either margin. The fix SHALL modify only the PPU's uploaded OAM
copy and high table and SHALL NOT write guest memory.

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

#### Scenario: Parked 2P pair in a 1P race

- **WHEN** slots 96/97 carry their parked entries with the X high bit set
  (`X = $165/$160`, `Y = $20/$60`)
- **THEN** both slots are moved below the field in the PPU copy
- **AND** no part of them is drawn in the left or right margin at any `E`,
  including image X 480/485 at `E = 128`
- **AND** a positive-X entry (high bit clear) in those slots is not hidden

#### Scenario: Real rider shares the sentinel low byte

- **WHEN** a rider's uploaded entry has the high bit clear, or its low byte or
  Y differs from the parking sentinel
- **THEN** it is treated as a real rider and is not hidden by the parked
  sentinel rule

#### Scenario: Ambiguity not resolvable

- **WHEN** the WRAM-derived X does not clearly prefer `lo + 256` over `lo`
- **THEN** the slot is left unchanged by the high-bit fix

### Requirement: High-bit fix has its own gate

The system SHALL apply the frame-level rider high-bit/sentinel pass only in a live widescreen race with the cull widening active and `E > 0`: with the 1P policy when `$0DDB` is zero and with the two-player rider OAM policy when it is not. The two-player per-line high/size/sentinel pass SHALL run in any live 2P/VS race, wide or native 4:3, with the cull active. The independent switch `SNESRECOMP_WS_OAM_XHIGH=0` SHALL disable both. The 1P native 4:3 output SHALL remain the pre-change baseline.

#### Scenario: Switch off

- **WHEN** `SNESRECOMP_WS_OAM_XHIGH=0`
- **THEN** frames equal the output without the OAM fix, in 1P and in 2P
- **AND** the cull widening stays active

#### Scenario: 1P native 4:3

- **WHEN** widescreen is off in 1P, or the race is not live
- **THEN** output and simulation equal the native baseline

#### Scenario: 2P native 4:3

- **WHEN** widescreen is off in a live 2P/VS race
- **THEN** the per-line high/size/sentinel pass runs and the bottom half shows its rider
- **AND** the frame matches the re-recorded `2p-native` reference

#### Scenario: Split screen

- **WHEN** `$0DDB ≠ 0` and the race is live
- **THEN** the two-player rider OAM policy runs instead of the 1P policy
- **AND** the frame-level missing-X/sentinel part is active only when the frame is wide

### Requirement: Two-player rider OAM

In a live 2P/VS split-screen race the system SHALL handle OAM slots 96..99 as the four staged rider entries: 96 P2 bottom, 97 P1 bottom, 98 P1 top, 99 P2 top, with the bottom camera (`$041B/$041F`) and a +112 Y offset for slots 96/97 and the top camera (`$0419/$041D`) for slots 98/99. For each slot that matches a rider entry it SHALL, in order: hide the half's parking sentinel; leave an entry with the high bit set (unless the measurement shows the game sets it for a real positive X, in which case it SHALL clear the bit when the WRAM-derived X clearly prefers `lo`); leave an entry with `lo >= E`; set the missing X high bit when the WRAM-derived X clearly prefers `lo + 256` within the one-frame slack; otherwise leave it. It SHALL modify only the PPU's OAM copy and high table, and SHALL NOT write guest memory. A frame-level pass SHALL do the above in a wide race; a per-line high-table pass SHALL run for any live 2P/VS, wide or native 4:3, and set the line's half rider slots to the big size bit and the true X high bit before each line is drawn, because the single end-of-frame high table stages the bottom entries small.

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

