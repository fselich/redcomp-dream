# recomp-save-unlock Specification

## Purpose
TBD - created by archiving change uniracers-unlock-tours. Update Purpose after archive.
## Requirements
### Requirement: Documented progress format

The project SHALL document the SRAM progress data in
`docs/UNIRACERS_SAVE_FORMAT.md`: each checksummed block with its range and
algorithm, the tour reveal stage `$10D3` with its values, the per-tour
medal flags, and the last chosen level. Each fact SHALL be marked as
verified or as hypothesis.

#### Scenario: Checksum claim

- **WHEN** the document states a block's checksum
- **THEN** `srm.py verify` confirms it on a fresh save and on a progressed
  save

### Requirement: SRAM tool never overwrites its input

`tools/scripts/save/srm.py` SHALL decode (`dump`), verify (`verify`) and
modify (`set`, `unlock`) a save, and SHALL write only to an explicit output
path different from the input.

#### Scenario: Same path

- **WHEN** `srm.py unlock` is given the same input and output path
- **THEN** it exits with an error and the file is unchanged

#### Scenario: Unlocked copy

- **WHEN** `srm.py unlock IN OUT` runs on a valid save
- **THEN** OUT has `$10D3 ≥ 3`, `srm.py verify OUT` passes, and every other
  byte equals IN

### Requirement: Opt-in tour unlock

The game SHALL provide `UnlockAllTours` under `[Emulation]` in
`config.ini`, overridable by `SNESRECOMP_UNLOCK_TOURS`, defaulting to off.
When it is on outside netplay, the game SHALL raise the active save's
`$10D3` to 3 if it is lower, and SHALL NOT change any other SRAM byte.

#### Scenario: Default

- **WHEN** the option is unset
- **THEN** no SRAM byte is written by the port and every `validate.sh` case
  matches its reference

#### Scenario: Existing save

- **WHEN** the option is on and the save has `$10D3 = 1`
- **THEN** PICK TOUR shows all nine tours, the medals shown are unchanged,
  and the saved file differs from the original only at `$10D3` (and in
  whatever the player's own play changed)

#### Scenario: Never lowers

- **WHEN** the option is on and `$10D3` is already 3 or higher
- **THEN** nothing is written

#### Scenario: Netplay

- **WHEN** a netplay session is active
- **THEN** the option has no effect

### Requirement: Unlock applied only to a valid image

The game SHALL write `$10D3` only between frames, only when the SRAM magic
is present and every block checksum is valid.

#### Scenario: First boot without a save

- **WHEN** no `save.srm` exists and the option is on
- **THEN** after the game formats SRAM, `$10D3` becomes 3 and PICK TOUR shows
  every tour

#### Scenario: Invalid image

- **WHEN** a block checksum is invalid
- **THEN** no byte is written

### Requirement: Unlock is persistent

A save unlocked by the option SHALL keep every tour after a restart with the
option off.

#### Scenario: Option turned off

- **WHEN** a save was unlocked, the game exits, and it restarts with the
  option off
- **THEN** PICK TOUR still shows all nine tours

### Requirement: Tool and game agree

The SRAM tool and the game SHALL produce the same bytes for the same input.

#### Scenario: Cross-check

- **WHEN** the same valid save is unlocked by `srm.py unlock` and by the game
  (SRAM after the first patch frame)
- **THEN** the two images are byte-identical
