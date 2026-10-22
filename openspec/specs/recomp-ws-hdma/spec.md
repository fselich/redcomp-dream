# recomp-ws-hdma Specification

## Purpose
TBD - created by archiving change uniracers-ws-hdma. Update Purpose after archive.
## Requirements
### Requirement: HDMA builder identified

The change SHALL identify the code that writes the per-line HDMA tables, with the
writer PC, the written values and the frame range, or SHALL document why the
builder could not be identified.

#### Scenario: Builder is found

- **WHEN** the WRAM watch on the table addresses and pointer is queried
- **THEN** the writer PC and written values are recorded
- **AND** the report names the builder and its frame range

#### Scenario: Builder is not found

- **WHEN** the watch log shows no writer or only indirect copies
- **THEN** the report records the watched addresses, the log limits and the
  unresolved cause
- **AND** no extension is claimed

### Requirement: Table layout mapped

The change SHALL map the HDMA table layout: base, end, count blocks, line count,
and how each per-line value maps to `$210D`/`$210F`.

#### Scenario: Layout is mapped

- **WHEN** the builder and table are traced
- **THEN** the report records the layout and the mapping to the scroll registers

#### Scenario: Layout is ambiguous

- **WHEN** the count/entries cannot be resolved
- **THEN** the ambiguity is recorded
- **AND** the extension is not attempted

### Requirement: Margin extension

The change SHALL extend the per-line scroll for the extra margin columns, or
SHALL record P-U1 as blocked/not applicable with evidence and keep the
renderer-only margins.

#### Scenario: Extension implemented

- **WHEN** the per-line function or table is extended for the margins
- **THEN** the wide frame uses the extended values
- **AND** the report states whether the extension is host-side or a
  generated-C hook

#### Scenario: Extension not applicable

- **WHEN** the enabled HDMA channels have empty tables and the curves come from
  BG tilemap/scroll
- **THEN** P-U1 is recorded as not applicable with the table/pointer evidence
- **AND** no extension is invented

#### Scenario: Extension blocked

- **WHEN** the curve source cannot be safely extended
- **THEN** P-U1 is recorded as blocked with the writer/layout evidence
- **AND** no invented curve values are used

### Requirement: 4:3 preserved

The change SHALL keep the 4:3 baseline aligned with the preserved build.

#### Scenario: 4:3 unchanged

- **WHEN** the wide flag is off or the game is outside live race
- **THEN** frames match the baseline at the documented offset
- **AND** no simulation state differs

#### Scenario: 4:3 regresses

- **WHEN** the flag-off frames no longer align
- **THEN** the change is reverted and the divergence is recorded

### Requirement: Wide seam evidence

The change SHALL capture 16:9 on a curve or hill and SHALL show whether the
margin curve is continuous with the native edge.

#### Scenario: Seam is continuous

- **WHEN** the curve capture is compared
- **THEN** the margin columns follow the same curve as the native edge
- **AND** no seam step or wrapped column appears

#### Scenario: Seam is wrong

- **WHEN** the margin curve steps, wraps or diverges
- **THEN** the first bad frame and the affected columns are recorded
- **AND** the report does not claim P-U1 complete

### Requirement: Simulation unchanged and report

The change SHALL verify that camera, collision, RNG, input and save-state state
are unchanged, and SHALL produce `report.md` with the builder, layout, extension
status, seam evidence, performance and remaining gaps.

#### Scenario: Simulation is unchanged

- **WHEN** the 4:3 and wide runs are compared
- **THEN** simulation state matches at aligned frames
- **AND** the report states that only presentation/HDMA output changed

#### Scenario: Simulation changes

- **WHEN** camera, collision, RNG or input differs
- **THEN** the change is reverted
- **AND** the divergence is recorded

