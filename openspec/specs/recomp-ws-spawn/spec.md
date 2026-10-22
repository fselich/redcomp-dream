# recomp-ws-spawn Specification

## Purpose
TBD - created by archiving change uniracers-ws-spawn. Update Purpose after archive.
## Requirements
### Requirement: Spawn and staging path identified

The change SHALL identify the object record walk, spawn anchor, activation
distance and stage triggers, with addresses and writer PCs, or SHALL record
P9–P13 as not applicable with evidence.

#### Scenario: Path is found

- **WHEN** the Dragster race is traced
- **THEN** the record table, anchor comparison, activation distance and stage
  triggers are recorded
- **AND** ordinary records are distinguished from progression/controllers

#### Scenario: No records exist

- **WHEN** every track object is BG or otherwise not camera-spawned
- **THEN** P9–P13 are recorded as not applicable
- **AND** no spawn logic is invented

### Requirement: Ordinary spawn anchor widened

The change SHALL widen only ordinary object spawn anchors by margin + 32,
preserving the native base for every other record.

#### Scenario: Ordinary object spawns in the margin

- **WHEN** the widened pass admits an ordinary object
- **THEN** it appears inside the widened view without a native-edge pop
- **AND** the base anchor remains recorded

#### Scenario: A progression record is widened

- **WHEN** a progression/controller record fires from the widened anchor
- **THEN** the change is reverted
- **AND** the record is moved to the native pass

### Requirement: Dual pass preserves progression

The change SHALL keep progression, controllers and RNG on the unmodified 4:3
pass and SHALL add a separate widened pass for ordinary records only.

#### Scenario: Dual pass is safe

- **WHEN** the native and widened passes run
- **THEN** progression/RNG state matches the native run
- **AND** no object is created twice

#### Scenario: Dual pass changes RNG

- **WHEN** the widened pass alters RNG or record order
- **THEN** the change is reverted or the pass is recorded as unsupported
- **AND** P10 is not claimed complete

### Requirement: Balanced synthetic call

The change SHALL save and restore `CpuState`, the anchor bytes and the guest
stack around the native pass.

#### Scenario: Native pass returns cleanly

- **WHEN** the synthetic native pass returns
- **THEN** the guest stack and registers are balanced
- **AND** the caller sees its original anchor bytes

#### Scenario: Stack is corrupted

- **WHEN** the pass unbalances the stack or leaves anchor bytes changed
- **THEN** the change is reverted
- **AND** the failure is recorded

### Requirement: Large object activation widened

The change SHALL widen large-object activation distance by margin + 32.

#### Scenario: Large object outer tiles arrive together

- **WHEN** a large object approaches the widened edge
- **THEN** its outer tiles are active before they enter the view
- **AND** no CHR/tile pop is visible

#### Scenario: Activation is not widened

- **WHEN** a large object's centre reaches the edge before its tiles
- **THEN** the defect is recorded
- **AND** P12 is not claimed complete

### Requirement: X-axis stage lead

The change SHALL bias only X-axis stage triggers by the margin, into the travel
direction, with complementary direction conditions; Y and camera locks remain
native.

#### Scenario: Stage swap is safe

- **WHEN** a section approaches in either travel direction
- **THEN** the trigger leads by at most the margin
- **AND** the old section's CHR is not swapped before it leaves

#### Scenario: Trigger fires early or loops

- **WHEN** staging fires beyond the margin or re-fires
- **THEN** the change is reverted
- **AND** the trigger PC and direction are recorded

### Requirement: Independent kill switches

The change SHALL provide independent `SNESRECOMP_WS_SPAWN` and
`SNESRECOMP_WS_STAGE` switches.

#### Scenario: One side falls back

- **WHEN** one switch is disabled
- **THEN** the other remains active
- **AND** the disabled side matches authentic 4:3 behavior

#### Scenario: Single global switch

- **WHEN** only one toggle controls both spawn and staging
- **THEN** it is rejected
- **AND** separate gates are required

### Requirement: 4:3, RNG and simulation preserved

The change SHALL keep 4:3 bit-identical and SHALL verify RNG, progression,
collision, camera and input are unchanged.

#### Scenario: 4:3 is preserved

- **WHEN** widescreen is off
- **THEN** frames match the preserved baseline
- **AND** no record or RNG state differs

#### Scenario: Simulation changes

- **WHEN** RNG, progression, collision, camera or input differs
- **THEN** the change is reverted
- **AND** the first divergent frame is recorded

### Requirement: Wide spawn and staging evidence

The change SHALL capture Dragster with ordinary objects entering the widened
margins and a staging transition in both travel directions.

#### Scenario: Objects enter smoothly

- **WHEN** the wide captures are reviewed
- **THEN** ordinary objects enter from the margin without popping
- **AND** large objects show their full tile footprint

#### Scenario: Pop remains

- **WHEN** an object or stage swap still pops
- **THEN** the first bad frame and record/trigger are recorded
- **AND** the requirement is not claimed complete

### Requirement: Decision report

The change SHALL produce `report.md` with the path, patches, switches, RNG and
4:3 evidence, margin captures and whether P9–P13 are complete, partial, N/A or
reverted.

#### Scenario: P9–P13 complete

- **WHEN** the spawn/staging evidence passes
- **THEN** the report records the patches and switches
- **AND** names any residual limitations

#### Scenario: Not applicable

- **WHEN** the engine has no camera-spawned records
- **THEN** the report records the BG evidence
- **AND** no synthetic spawn logic is claimed

#### Scenario: Partial

- **WHEN** only some of P9–P13 apply
- **THEN** the report states which are complete and which are not
- **AND** recommends the next diagnostic

### Requirement: Track-scoped N/A evidence

A spawn/staging N/A SHALL name the track it measured and SHALL test the second
track before generalizing, when the two tracks have different content.

#### Scenario: N/A names the track

- **WHEN** a change records P9–P13 as not applicable
- **THEN** the report names the track and the frame set used
- **AND** the result is not described as engine-wide

#### Scenario: Second track differs

- **WHEN** Dragster (first track) is N/A and Zoom Zoo (second track) has
  different track art or HUD
- **THEN** Zoom Zoo is measured independently
- **AND** its spawn/staging result is recorded separately

#### Scenario: Generalization without a second-track test

- **WHEN** an N/A from the first track is applied to the second without evidence
- **THEN** the claim is rejected
- **AND** the missing track measurement is recorded as a follow-up

