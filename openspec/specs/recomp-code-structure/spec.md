# recomp-code-structure Specification

## Purpose

How the hand-written port is organised and changed: refactors preserve output under the validation gate, named guest addresses, isolated guest-memory writers, one live-race predicate, one owner for the margin policy, a minimal framework patch and no research probes in the build.

## Requirements
### Requirement: Refactors preserve output

A change scoped as a refactor SHALL leave the game's output identical: frame hashes, WRAM writes, log files and stderr lines for every case in `game/tools/validate.sh`.

#### Scenario: Default widescreen

- **WHEN** the refactored build runs the 16:9 Dragster case to frame 2500
- **THEN** its frame SHA-256 equals the reference recorded from the pre-refactor commit

#### Scenario: Native

- **WHEN** the refactored build runs with `SNESRECOMP_WS=0`
- **THEN** Dragster frame 2500 SHA-256 is `3417f91a…df44`

#### Scenario: Every policy and width

- **WHEN** `validate.sh` runs the native, 16:9, 21:9 and 512 px cases and the `source` and `clamp` policies
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

The port and its local framework patch SHALL NOT contain research code that normal play does not run: the stage-1/2 generator probes (`SNESRECOMP_WS_TEST_GEN`, `SNESRECOMP_WS_TEST_GEN_FRAME`, `SNESRECOMP_WS_TEST_STAGE2*`), the track-repeat experiment (`SNESRECOMP_WS_TRACK_REPEAT`), the ghost lane (`SNESRECOMP_WS_GHOST_*`) and the `$7F` read watch (`SNESRECOMP_READ_WATCH_*`). The read watch SHALL be kept as a standalone patch under `tools/patches/research/`, stating the experiments that used it and how to apply it. The BG pixel-source probe (`SNESRECOMP_BGPROBE_*`) is a validation tool used by `validate.sh` and SHALL stay.

#### Scenario: Removed switch set

- **WHEN** `SNESRECOMP_WS_TRACK_REPEAT=1`, `SNESRECOMP_WS_GHOST_AHEAD=4` or `SNESRECOMP_READ_WATCH_FILE=x` is set
- **THEN** the output equals a run without it
- **AND** no log file is created

#### Scenario: Probe still reproducible

- **WHEN** `tools/patches/research/read-watch.diff` is applied to the framework tree of this change
- **THEN** it applies cleanly and the build logs `$7F` reads for the requested frames

### Requirement: Framework patch carries only port hooks

`game/patches-snesrecomp-local.diff` SHALL contain only framework changes that the port calls in normal play or that the validation gate uses (the BG pixel-source probe), and SHALL equal the diff of the vendored tree against its pinned upstream.

#### Scenario: No speculative-lane hooks

- **WHEN** the patch is searched for `snes_beam_frame_driver_get_exec`, `ppu_rasterState` or `read_watch_note`
- **THEN** none is found

#### Scenario: Patch matches the tree

- **WHEN** the diff is regenerated from `game/snesrecomp/` against `framework_pins.txt`
- **THEN** it equals the committed patch

### Requirement: One owner for the margin policy

A single margin module SHALL own the BG1 margin policy: choosing `source` or `clamp`, installing and removing the per-frame line and tile hooks, applying the BG1 clamp, and resetting margin state on a policy change. `game_rtl.c` SHALL only call it at the frame boundaries. The ring module SHALL hold the ring observation (seen table, scroll unwrap) and no policy.

#### Scenario: Locating the policy

- **WHEN** a reader looks for where the margin policy is decided or the margin hooks are installed
- **THEN** both are in the margin module

#### Scenario: No code for removed policies

- **WHEN** the port is searched for `WS_OW_RING`, `WS_OW_GHOST`, `WS_OW_WRAP` or `WsGhost`
- **THEN** none is found

