# recomp-aot-dispatch Specification

## Purpose
TBD - created by archiving change uniracers-aot-dispatch. Update Purpose after archive.
## Requirements
### Requirement: Diagnosed entry path

The change SHALL record the live entry path to `$81AE7D`: the call site, the
live bank and M/X, the call kind, the canonicalized dispatch key, and whether
the AOT table contains a matching variant.

#### Scenario: Entry path is identified

- **WHEN** the temporary bridge trace runs through the race window
- **THEN** the trace records the first `$81AE7D` entry and the dispatch decision
- **AND** the report states whether the miss is a bank alias, an indirect call, a
  fall-through, or an intentional safety skip

#### Scenario: Entry path cannot be identified

- **WHEN** the trace does not capture the entry
- **THEN** the report records the trace limits and the unresolved cause
- **AND** no fix is claimed

### Requirement: Minimal dispatch fix

The change SHALL apply the smallest reversible fix for the diagnosed cause, SHALL
keep the interpreter as fallback, and SHALL dispatch only exact `(pc, M, X)`
variants.

#### Scenario: Fix is applied

- **WHEN** the diagnosed cause is addressed
- **THEN** the fix is saved as a documented patch or a project-local change
- **AND** the report records why it is minimal and how to revert it

#### Scenario: No safe fix exists

- **WHEN** the diagnosis shows an intentional safety skip or an unproven variant
- **THEN** the change records the blocker instead of forcing dispatch
- **AND** recommends the next evidence needed

### Requirement: Camera reaches AOT

The change SHALL verify that the `$81AE7D–$81AE9F` range is reached by its AOT
body, measured as a material drop in interpreted instructions for that range,
and SHALL record the actual before/after counts.

#### Scenario: Camera becomes AOT-reached

- **WHEN** the promoted build replays the route
- **THEN** the camera range's interpreted instructions drop materially from the
  Phase D baseline of 19,201
- **AND** the report records the exact new count

#### Scenario: Camera remains interpreted

- **WHEN** the range's interpreted count does not drop
- **THEN** the report states that the fix did not work
- **AND** does not claim the camera is AOT-reached

### Requirement: Replay equivalence

The promoted build SHALL replay the exact route for 3,000 frames and SHALL match
the baseline exactly or at the documented stable frame offset, with no new
tripwires and no regression in unrelated promoted targets.

#### Scenario: Replay is equivalent

- **WHEN** the fix is built and replayed
- **THEN** frames match exactly or at a stable documented offset
- **AND** no new tripwire or crash appears

#### Scenario: Replay regresses

- **WHEN** frames no longer align or an unrelated target regresses
- **THEN** the first divergent frame and the affected targets are recorded
- **AND** the fix is reverted

### Requirement: Timing policy

The change SHALL record the P16 comparison policy: permanent aligned-frame
equivalence with a documented offset, or exact cycle accounting as a separate
blocked change.

#### Scenario: Aligned policy is chosen

- **WHEN** the measured offset remains stable
- **THEN** the report records the offset and the aligned comparison rule
- **AND** states that exact fixed-frame hashes are not required for widescreen

#### Scenario: Exact timing is required

- **WHEN** exact hashes are required
- **THEN** the report names cycle accounting as a separate blocker
- **AND** does not claim P16 can be met in this change

### Requirement: Decision report

The change SHALL produce `report.md` with the diagnosis, fix, before/after
camera counts, replay result, timing policy, and remaining widescreen blockers,
and SHALL NOT claim that widescreen or playability has been achieved.

#### Scenario: Dispatch is fixed

- **WHEN** diagnosis, fix and replay complete
- **THEN** the report states the camera result and the timing policy
- **AND** states that this is dispatch evidence only

#### Scenario: Dispatch is not fixed

- **WHEN** any gate fails
- **THEN** the report names the failed gate and its evidence
- **AND** recommends the next diagnostic step instead of widescreen work

