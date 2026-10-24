# recomp-boot-spike Specification

## Purpose

Records the first boot of the scaffolded port: executable build, headless runs, frame evidence and coverage. A finished investigation.

## Requirements
### Requirement: Pinned scaffold

The spike SHALL scaffold a project from the pinned ROM with
`setup_project.sh --no-recomp-ui` and SHALL pin the framework submodule to
`9c6afcbfd4106e4613e522741d667047a48ded3b` before generation.

#### Scenario: Scaffold succeeds

- **WHEN** the scaffold completes and the submodule is repinned
- **THEN** the project exists under `work/boot-spike/`
- **AND** `git -C snesrecomp rev-parse HEAD` equals the pinned commit
- **AND** the ROM identity recorded by the project matches the pinned SHA-256

#### Scenario: Pin cannot be set

- **WHEN** the submodule cannot be checked out at the pinned commit
- **THEN** the spike aborts before generation and build
- **AND** the report records the resolved commit instead of continuing

### Requirement: Executable build

The scaffolded project SHALL produce a Linux executable with CMake, Ninja, and
GCC, and the report SHALL record build success, duration, executable size, and
the build log.

#### Scenario: Build succeeds

- **WHEN** the project is configured and built
- **THEN** an executable is produced in the build directory
- **AND** its size, build duration, and build log are recorded

#### Scenario: Build fails

- **WHEN** configuration or compilation fails
- **THEN** the failure is classified (toolchain, missing dependency, or
  generated-code error)
- **AND** the spike is reported as failed at the build stage

### Requirement: Headless execution

The executable SHALL run headless with `SDL_VIDEODRIVER=dummy`,
`SDL_AUDIODRIVER=dummy`, `--no-launcher`, and `SNESRECOMP_RUN_FRAMES`, under a
wall-clock timeout, and SHALL record its exit status and logs.

#### Scenario: Run completes within the budget

- **WHEN** the executable reaches the guest-frame budget
- **THEN** its exit status, frames executed, and logs are recorded

#### Scenario: Run exceeds the timeout

- **WHEN** the wall-clock timeout is reached
- **THEN** the run is classified as a hang
- **AND** the last log line and guest-frame count are recorded

### Requirement: Frame evidence

The spike SHALL capture at least one frame and SHALL compute non-black pixel
fraction, unique color count, mean luminance, and frame-to-frame difference; a
missing capture SHALL be reported as unavailable.

#### Scenario: Capture is available

- **WHEN** a frame is captured
- **THEN** the four metrics are recorded for it
- **AND** at least one frame is classified as non-trivial or black using the 1%
  non-black threshold

#### Scenario: Capture is unavailable

- **WHEN** neither the screenshot path nor the frame-dump fallback produces an
  image
- **THEN** the report marks frame evidence as unavailable
- **AND** no frame classification is inferred

### Requirement: Coverage capture

The run SHALL produce tier2 coverage and SHALL ingest it against the program
manifest from the same build, recording interpreted entries and fallback
reasons.

#### Scenario: Capture is produced

- **WHEN** the coverage checkpoint and journal exist
- **THEN** ingestion completes without identity errors
- **AND** interpreted-entry and fallback-reason counts are recorded

#### Scenario: Capture is missing or mismatched

- **WHEN** the capture is absent or fails the manifest identity check
- **THEN** the report records the failure
- **AND** no coverage counts are inferred

### Requirement: Boot outcome and baseline targets

The report SHALL assign exactly one boot outcome from `visible_nonblack`,
`black`, `tripwire_or_crash`, `loader_failure`, or `hang`, and SHALL record
whether each Phase A target was observed: the 2 LLE-only variants and the PCs
of the 9 unresolved indirect nodes.

#### Scenario: Baseline targets observed

- **WHEN** the run reaches any Phase A target address
- **THEN** the report marks that target as hit
- **AND** records the transfer kind and fallback reason from the capture

#### Scenario: Baseline targets not observed

- **WHEN** a Phase A target does not appear in the capture
- **THEN** the report marks it as not hit
- **AND** does not infer execution that was not observed

### Requirement: Decision report

The change SHALL produce `report.md` containing the boot outcome, supporting
evidence, the first concrete obstacle, coverage results, limitations, and a
scoped recommendation, and SHALL NOT claim that the game is playable.

#### Scenario: Spike completes

- **WHEN** scaffold, build, run, frame evidence, and coverage ingestion have all
  been attempted
- **THEN** the report states one outcome and the next step
- **AND** states that the result is runtime evidence only, not a playability
  claim

#### Scenario: A stage fails

- **WHEN** any stage fails
- **THEN** the report names the failed stage and its captured error
- **AND** recommends remediation for that stage rather than per-game RTL work

