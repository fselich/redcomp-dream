## MODIFIED Requirements

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

### Requirement: Research probes removed

The port and its local framework patch SHALL NOT contain research code that normal play does not run: the stage-1/2 generator probes (`SNESRECOMP_WS_TEST_GEN`, `SNESRECOMP_WS_TEST_GEN_FRAME`, `SNESRECOMP_WS_TEST_STAGE2*`), the track-repeat experiment (`SNESRECOMP_WS_TRACK_REPEAT`), the ghost lane (`SNESRECOMP_WS_GHOST_*`) and the `$7F` read watch (`SNESRECOMP_READ_WATCH_*`). The read watch SHALL be kept as a standalone patch under `tools/patches/research/`, stating the experiments that used it and how to apply it. The BG pixel-source probe (`SNESRECOMP_BGPROBE_*`) is a validation tool used by `validate.sh` and SHALL stay.

#### Scenario: Removed switch set

- **WHEN** `SNESRECOMP_WS_TRACK_REPEAT=1`, `SNESRECOMP_WS_GHOST_AHEAD=4` or `SNESRECOMP_READ_WATCH_FILE=x` is set
- **THEN** the output equals a run without it
- **AND** no log file is created

#### Scenario: Probe still reproducible

- **WHEN** `tools/patches/research/read-watch.diff` is applied to the framework tree of this change
- **THEN** it applies cleanly and the build logs `$7F` reads for the requested frames

## ADDED Requirements

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
