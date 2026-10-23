# recomp-code-structure Specification

## Purpose
TBD - created by archiving change uniracers-code-cleanup. Update Purpose after archive.
## Requirements
### Requirement: Refactors preserve output

A change scoped as a refactor SHALL leave the game's output identical: frame
hashes, WRAM writes, log files and stderr lines for every case in
`game/tools/validate.sh`.

#### Scenario: Default widescreen

- **WHEN** the refactored build runs the 16:9 Dragster case to frame 2500
- **THEN** its frame SHA-256 equals the reference recorded from the
  pre-refactor commit

#### Scenario: Native

- **WHEN** the refactored build runs with `SNESRECOMP_WS=0`
- **THEN** Dragster frame 2500 SHA-256 is `3417f91a…df44`

#### Scenario: Every policy and width

- **WHEN** `validate.sh` runs the native, 16:9, 21:9 and 512 px cases and the
  `wrap`, `ring`, `source`, `ghost` and `clamp` policies
- **THEN** no case differs from its recorded reference

#### Scenario: Logs

- **WHEN** the source, offwindow, park and radar logs are enabled
- **THEN** each log file is byte-identical to the reference run

### Requirement: Validation never touches the player's data

`validate.sh` SHALL run a copy of the executable in a scratch directory with
its own empty `saves/`, and SHALL NOT read, write or delete anything under
`game/build-local/saves/` or the user's `config.ini`.

#### Scenario: Existing save

- **WHEN** `game/build-local/saves/save.srm` exists and `validate.sh` runs
- **THEN** that file's content and modification time are unchanged

### Requirement: Validation references are reproducible

`validate.sh --record` SHALL run each case twice and SHALL only store a frame
hash that is equal in both runs; a case whose frame differs between runs
SHALL be reported as non-deterministic and compared by its log score instead.

#### Scenario: Stable capture

- **WHEN** both record runs of a case give the same frame hash
- **THEN** that hash is written to `validate_refs.txt`

#### Scenario: Unstable capture

- **WHEN** the two record runs differ
- **THEN** the case is listed as non-deterministic and no frame hash is stored
  for it

### Requirement: One live-race predicate

The port SHALL define the widescreen-enabled and live-race predicates once,
and the host shim and every presentation module SHALL use that definition.

#### Scenario: Predicate fix

- **WHEN** the live-race condition changes
- **THEN** exactly one function body changes

### Requirement: Named guest addresses

Every WRAM address the hand-written port reads or writes SHALL be referenced
through a named constant in `variables.h` with a short explanation.

#### Scenario: Reading a rider position

- **WHEN** a module reads the player's X
- **THEN** it uses the named constant, not the literal `0x0411`

### Requirement: Guest-memory writers are isolated

Presentation code SHALL NOT write guest WRAM. The cull window module SHALL be
the only module that writes guest WRAM in normal play, and its header SHALL
say so.

#### Scenario: Locating guest writes

- **WHEN** a reader looks for production writes to `g_ram`
- **THEN** they are all in the cull module

### Requirement: Debug switches are read once

Environment switches and log paths SHALL be read at most once per process
through a shared helper. Values from `g_config` SHALL still be read when used.

#### Scenario: Per-frame cost

- **WHEN** a race frame is drawn
- **THEN** no `getenv` call happens after each switch's first lookup

#### Scenario: Config change

- **WHEN** `g_config.widescreen` changes at run time
- **THEN** the next frame uses the new value

### Requirement: Research probes removed

The port SHALL NOT contain the stage-1/2 generator probes
(`SNESRECOMP_WS_TEST_GEN`, `SNESRECOMP_WS_TEST_GEN_FRAME`,
`SNESRECOMP_WS_TEST_STAGE2*`) or the track-repeat experiment
(`SNESRECOMP_WS_TRACK_REPEAT`), and all other switches and policies SHALL
keep their current behaviour.

#### Scenario: Removed switch set

- **WHEN** `SNESRECOMP_WS_TRACK_REPEAT=1` is set
- **THEN** the output equals a run without it

#### Scenario: Kept switch

- **WHEN** `SNESRECOMP_WS_OFFWINDOW=ghost` is set
- **THEN** the ghost lane runs as specified in `recomp-ws-ghost-lane`

