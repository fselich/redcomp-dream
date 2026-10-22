## ADDED Requirements

### Requirement: Preserved baseline

The change SHALL preserve the Phase C project, executable, frame hashes, and
coverage totals, and SHALL run the promoted build from an isolated copy.

#### Scenario: Baseline is preserved

- **WHEN** the promotion workspace is created
- **THEN** the Phase C project and executable remain runnable
- **AND** the baseline hashes and coverage totals are recorded before any change

#### Scenario: Baseline would be overwritten

- **WHEN** the promotion would modify the Phase C project in place
- **THEN** the run aborts
- **AND** no promotion is attempted

### Requirement: Profile-driven promotion

The change SHALL feed the Phase C race checkpoint and journal into regeneration
through the project's regen path and SHALL record the resulting promoted roots
and AOT variants.

#### Scenario: Profile is accepted

- **WHEN** regeneration runs with the race checkpoint and journal
- **THEN** the emitted manifest records the promoted roots and variants
- **AND** the report lists them

#### Scenario: Profile is rejected or mismatched

- **WHEN** the profile fails the ROM or manifest identity check
- **THEN** the failure is recorded verbatim
- **AND** the change falls back to explicit symbols or stops, without inferring
  promotion

### Requirement: Conservative promotion set

The promotion SHALL include the per-frame display block, the camera/scroll
update, the HDMA setup, and the highest-hit race entries, and SHALL exclude the
frame-wait loop and the asset VRAM upload.

#### Scenario: Intended set is promoted

- **WHEN** the emitted manifest is inspected
- **THEN** the included targets are present and the excluded targets remain
  interpreted
- **AND** the report lists both sets

#### Scenario: Unintended targets are promoted

- **WHEN** a blanket promotion pulls in unintended banks or targets
- **THEN** the promotion is narrowed or regenerated
- **AND** the deviation is recorded

### Requirement: Executable build

The promoted project SHALL build a Linux executable, and the report SHALL record
build success, duration, and executable size.

#### Scenario: Build succeeds

- **WHEN** the promoted project is configured and built
- **THEN** an executable is produced
- **AND** its size and build duration are recorded

#### Scenario: Build fails

- **WHEN** configuration or compilation fails
- **THEN** the failure is classified
- **AND** no replay or reduction claim is made

### Requirement: Replay equivalence

The promoted executable SHALL replay the exact Phase C route for 3,000 frames
and SHALL either match the baseline frames exactly at 2400 and 3000, or match
them at a documented, stable frame offset (promoted frame N = baseline frame
N−k) with identical visual and state content; otherwise it SHALL classify the
divergence and restore the baseline.

#### Scenario: Frames match exactly

- **WHEN** the promoted build runs the route
- **THEN** the PPM hashes at 2400 and 3000 match the baseline exactly
- **AND** no new tripwire or crash appears

#### Scenario: Stable aligned offset

- **WHEN** the marker hashes differ but a frame window shows a stable offset
  (promoted N = baseline N−k)
- **THEN** the offset k and the aligned match are recorded
- **AND** the report claims visual/state equivalence with a documented timing
  offset, not bit-identical fixed-frame timing

#### Scenario: Divergence is not an alignment

- **WHEN** no stable offset aligns the frames
- **THEN** the first divergent frame and the active promoted targets are recorded
- **AND** the baseline is restored instead of claiming success

### Requirement: Instruction reduction and target resolution

The change SHALL record the interpreted-instruction delta and the status of each
promoted target, and SHALL NOT claim a reduction without the coverage comparison.

#### Scenario: Reduction is measured

- **WHEN** the promoted capture is ingested
- **THEN** the interpreted-instruction delta is recorded
- **AND** promoted targets are no longer `missing_entry` or their hit count drops

#### Scenario: No reduction is measured

- **WHEN** the delta is zero or negative
- **THEN** the actual numbers are reported
- **AND** no performance claim is made

#### Scenario: Target is emitted but not reached

- **WHEN** a promoted target remains interpreted despite an emitted AOT body
- **THEN** the report records it as an unresolved dispatch gap
- **AND** does not count it as resolved

### Requirement: Widescreen hand-off

The report SHALL record the P1–P16 mapping, the Uniracers-specific P-U1/P-U2
invariants, and the missing code needed before a widescreen change: gameplay
discriminator, cull windows, OAM reject compare, and spawn/staging.

#### Scenario: Hand-off is recorded

- **WHEN** the promotion report is written
- **THEN** each relevant pattern is marked as applying, not applicable, planned,
  or missing
- **AND** the missing code paths are named as blockers

#### Scenario: A pattern cannot be classified

- **WHEN** the evidence does not support a classification
- **THEN** the pattern is marked unresolved
- **AND** no widescreen behavior is asserted

### Requirement: Decision report

The change SHALL produce `report.md` with the promotion set, measured delta,
equivalence result, rollback decision, doctrine hand-off, and limitations, and
SHALL NOT claim that 16:9 or playability has been achieved.

#### Scenario: Promotion succeeds

- **WHEN** build, replay and measurement complete
- **THEN** the report states the promoted set and the measured effect
- **AND** states that this is promotion evidence only

#### Scenario: Promotion fails

- **WHEN** any gate fails
- **THEN** the report names the failed gate and its captured evidence
- **AND** recommends remediation rather than a widescreen implementation
