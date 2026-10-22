## ADDED Requirements

### Requirement: Deterministic route to a race

The change SHALL provide an input script that reaches a 1P race from boot, and
two consecutive runs of that script SHALL reach the same race state at the
recorded markers.

#### Scenario: Route reaches the race

- **WHEN** the script is run to the race budget
- **THEN** captured frames show the race rather than the menu
- **AND** two runs produce matching frame hashes at the recorded markers

#### Scenario: Route fails or diverges

- **WHEN** the script does not reach a race, or the two runs diverge
- **THEN** the first differing frame and the observed state are recorded
- **AND** the route is marked non-deterministic instead of assuming it worked

### Requirement: Bounded race capture

The race run SHALL capture selected frames, tier2 coverage, and PPU/VRAM/WRAM
state for a bounded window, within a declared frame budget and wall-clock
timeout.

#### Scenario: Capture is produced

- **WHEN** the race run completes within the budget
- **THEN** frame captures, the coverage checkpoint and journal, and the state
  dumps exist under `work/race-recon/`
- **AND** the budget, elapsed time, and exit status are recorded

#### Scenario: Capture is missing

- **WHEN** a capture artifact is absent or the run exceeds the timeout
- **THEN** the report marks that artifact as unavailable
- **AND** no race analysis is inferred from it

### Requirement: Race coverage analysis

The change SHALL ingest the race capture against the program manifest from the
same build and SHALL record interpreted instructions, new dynamic entries, and
the top instruction PCs for the race window.

#### Scenario: Ingest succeeds

- **WHEN** the checkpoint and journal pass the manifest identity check
- **THEN** interpreted-instruction counts, discoveries, and ranked hot PCs are
  recorded
- **AND** race-only entries are distinguished from the Phase B menu entries

#### Scenario: Ingest fails

- **WHEN** the capture is missing or fails the identity check
- **THEN** the failure is recorded verbatim
- **AND** no coverage counts are inferred

### Requirement: Renderer map

The report SHALL identify the PPU modes and layers used during the race, whether
Mode 7 and HDMA are used, and the top code sites that write the relevant
registers, with evidence for each claim.

#### Scenario: Renderer evidence is found

- **WHEN** hot PCs can be correlated with PPU register writes
- **THEN** the report records mode, layer, register, site PC, and supporting
  evidence
- **AND** distinguishes confirmed sites from candidates

#### Scenario: Renderer evidence is inconclusive

- **WHEN** a site or mode cannot be resolved
- **THEN** it is marked as an unresolved candidate
- **AND** no renderer behavior is asserted without evidence

### Requirement: Camera, viewport, and culling candidates

The report SHALL name at least one concrete camera, viewport, or culling
candidate with supporting evidence, or SHALL explain why none could be
identified.

#### Scenario: Candidate is identified

- **WHEN** a hot site or state variable is tied to camera, viewport, or culling
- **THEN** the report records the site, the evidence, and the expected effect of
  changing it

#### Scenario: No candidate is identified

- **WHEN** the evidence does not support a candidate
- **THEN** the report says so explicitly
- **AND** recommends what additional evidence would be needed

### Requirement: Widescreen feasibility assessment

The report SHALL assess renderer-level widening separately from engine-level
widening and SHALL state whether AOT promotion is required before the
framework's generated-C hooks can be used.

#### Scenario: A feasible path exists

- **WHEN** the evidence supports a first 16:9 experiment
- **THEN** the report names the layer, the functions or registers involved, and
  the next change
- **AND** states whether those functions must be promoted to AOT

#### Scenario: No feasible path is found

- **WHEN** the evidence is insufficient or the engine requires deeper reverse
  engineering
- **THEN** the report explains the blocker
- **AND** does not recommend a widescreen implementation yet

### Requirement: Decision report

The change SHALL produce `report.md` with the route, capture evidence, renderer
map, camera/viewport/culling candidates, feasibility assessment, and
limitations, and SHALL NOT claim that 16:9 or playability has been achieved.

#### Scenario: Recon completes

- **WHEN** route, capture, coverage, and renderer analysis have been attempted
- **THEN** the report states what is known, what is a candidate, and the next
  step
- **AND** states that the result is reconnaissance only

#### Scenario: A stage fails

- **WHEN** any stage fails
- **THEN** the report names the failed stage and its captured error
- **AND** recommends remediation for that stage
