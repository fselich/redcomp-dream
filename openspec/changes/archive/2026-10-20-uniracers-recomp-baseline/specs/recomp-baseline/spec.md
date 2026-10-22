## ADDED Requirements

### Requirement: Pinned toolchain revision

The baseline SHALL use SNESRecomp at commit
`9c6afcbfd4106e4613e522741d667047a48ded3b` and SHALL record the resolved
commit and the native analyzer build in the report.

#### Scenario: Pinned revision is used

- **WHEN** the framework is cloned for the baseline
- **THEN** the working tree is checked out at the pinned commit
- **AND** the report records that commit and the analyzer binary path

#### Scenario: Revision cannot be resolved

- **WHEN** the cloned tree does not resolve to the pinned commit
- **THEN** the run aborts before generation
- **AND** the report records the mismatch instead of continuing

### Requirement: ROM identity gate

Generation SHALL proceed only when the input ROM matches the pinned identity:
2,097,152 bytes, LoROM/FastROM, no coprocessor, 8 KiB SRAM, CRC32
`383858c7`, and SHA-256
`859ec99fdc25dd9b239d9085bf656e4f49c93a32faa5bb248da83efd68ebd478`.

#### Scenario: Matching ROM

- **WHEN** the input matches every pinned field
- **THEN** generation proceeds
- **AND** the report records the verified identity

#### Scenario: Non-matching ROM

- **WHEN** any pinned field differs
- **THEN** the run aborts before generating C
- **AND** no output directory is populated

### Requirement: Out-of-tree ROM-derived output

All ROM-derived artifacts SHALL be written under
`/tmp/opencode/uniracers-baseline/`, including generated C and the program
manifest, and SHALL NOT be written inside the project repository.

#### Scenario: Generation completes

- **WHEN** generation finishes successfully
- **THEN** generated C and `program_manifest.json` exist only under the scratch
  root
- **AND** the project tree contains no generated C or manifest files

#### Scenario: Scratch root cannot be created

- **WHEN** the scratch root is unavailable or not writable
- **THEN** the run aborts rather than falling back to the project tree
- **AND** the failure is reported

### Requirement: Generated library build

The generated project SHALL be compiled into `libsnesrecomp_game.a` with
CMake, Ninja, and GCC, and the report SHALL record build success, wall-clock
duration, library size, and the build log. Because the generated CMake
suppresses warnings with `-w`, the report SHALL state that diagnostics are
suppressed rather than presenting a warning count as meaningful.

#### Scenario: Build succeeds

- **WHEN** the generated project is configured and built
- **THEN** `libsnesrecomp_game.a` is produced
- **AND** its size, build duration, and build log are recorded
- **AND** the report states that warnings are suppressed by the generated flags

#### Scenario: Build fails

- **WHEN** configuration or compilation fails
- **THEN** the failure is classified (toolchain, missing dependency, or
  generated-code error)
- **AND** the baseline is reported as failed rather than partially green

### Requirement: Coverage metrics

The baseline SHALL record, from the emit output and `program_manifest.json`:
analysis roots, emitted AOT variants, LLE-only variants, bank count, per-bank
variant distribution, and the frequency of each LLE reason.

#### Scenario: Metrics are available

- **WHEN** the manifest and emit output are present
- **THEN** every listed metric is recorded
- **AND** per-bank counts sum to the reported totals

#### Scenario: A metric is missing

- **WHEN** a manifest field or emit field is absent
- **THEN** the report marks it as unavailable
- **AND** no value is inferred or defaulted to zero

### Requirement: Deterministic generation

Two consecutive generation runs from identical inputs SHALL produce
byte-identical `program_manifest.json` files.

#### Scenario: Manifests match

- **WHEN** both runs complete
- **THEN** their manifest SHA-256 digests are equal
- **AND** the report records the shared digest

#### Scenario: Manifests differ

- **WHEN** the two digests differ
- **THEN** both digests are recorded
- **AND** the baseline is marked non-deterministic

### Requirement: Decision report

The change SHALL produce `report.md` containing the measured metrics, the three
most frequent LLE reasons, known limitations, and a go/no-go recommendation for
Phase B. The report SHALL NOT claim that the game boots, renders, or is
playable.

#### Scenario: All criteria pass

- **WHEN** generation succeeds, the library builds, and the manifests match
- **THEN** the report recommends Phase B
- **AND** states that the recommendation is based on static coverage only

#### Scenario: Any criterion fails

- **WHEN** generation, the build, or determinism fails
- **THEN** the report recommends remediation
- **AND** does not recommend Phase B
