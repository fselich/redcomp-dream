# recomp-boot-launcher Specification

## Purpose

The pre-boot GUI: ROM selection and verification, the generic host settings it owns, saves and mods, Generate & rebuild and setup packs, the headless bypass, and the settings boundary it shares with the in-game menu.

## Requirements
### Requirement: Launcher at boot

The executable SHALL open the recomp-ui pre-boot launcher before `SnesInit()`
when it is launched for a player, using the `snes` launcher profile. The
launcher SHALL be skipped when `--no-launcher` or `SNESRECOMP_NO_LAUNCHER` is
set, when the host is headless or scripted (`SDL_VIDEODRIVER=dummy`,
`--script`, `--framedump`, or a start-paused run), when `SkipLauncher=1` and a
cached ROM exists, or when a ROM argument (`--rom` or a positional path) is
given. `--launcher` SHALL force it.

#### Scenario: Player launch

- **WHEN** the executable starts without `--no-launcher` and without a headless
  or scripted mode
- **THEN** the launcher window opens before the guest boots

#### Scenario: Forced launcher

- **WHEN** the executable starts with `--launcher`
- **THEN** the launcher opens even if `SkipLauncher=1`

#### Scenario: Headless run

- **WHEN** the executable starts with `SDL_VIDEODRIVER=dummy` or `--no-launcher`
- **THEN** no launcher window is created and the existing text path runs

#### Scenario: Conflicting flags

- **WHEN** the executable starts with both `--launcher` and `--no-launcher`
- **THEN** the host reports a usage error and does not boot

### Requirement: ROM selection and verification

The launcher SHALL show the ROM the host resolved (explicit argument, a copy
beside the executable, or the `rom.cfg` cache) and SHALL let the player choose
another file with the native picker. It SHALL verify the chosen image against
the digests in `rom_identity.txt` and SHALL refuse a different dump, naming the
expected SHA-256 and CRC32. The repository and the release pack SHALL NOT
contain ROM data, generated C, `funcs.h`, or a ROM image.

#### Scenario: Correct ROM

- **WHEN** the player selects a file matching `rom_identity.txt`
- **THEN** the launcher shows the ROM as verified and PLAY starts the game

#### Scenario: Wrong dump

- **WHEN** the player selects a different revision or region
- **THEN** the launcher refuses it and shows the expected digests
- **AND** PLAY does not start the game

#### Scenario: No ROM yet

- **WHEN** no argument, beside-executable copy, or cache exists
- **THEN** the launcher opens on the ROM picker instead of a game window

#### Scenario: Headered copy

- **WHEN** the player selects a `.smc` copy with a copier header whose payload
  matches the expected digests
- **THEN** the launcher accepts it, as the host's ROM verifier already does

### Requirement: Generic settings at boot

The launcher SHALL own the generic host settings: window scale, fullscreen,
linear filtering, frame blend, run-ahead, vsync, renderer, audio volume and
rate, player input sources and deadzone, `[KeyMap]`, `[GamepadMap]`, and the
SNES pixel aspect. It SHALL show only rows whose capability the host wires. It
SHALL NOT show the port's widescreen model (`[Widescreen]`): the launcher's
generic Widescreen row SHALL be hidden.

#### Scenario: Pixel aspect

- **WHEN** the player changes the pixel aspect in the launcher
- **THEN** the chosen CRT 4:3, 8:7 or 1:1 mode is written to
  `[Graphics] DisplayAspect` and used for the booted game

#### Scenario: No widescreen row

- **WHEN** the launcher's video page is open
- **THEN** it has no Widescreen toggle, because the port's widescreen options
  live in the F10 menu

#### Scenario: Controls

- **WHEN** the player rebinds a key or gamepad button in the launcher
- **THEN** the new binding is in effect for the booted game without a second
  launch

### Requirement: Saves and mods

When the host provides a battery save path, the launcher SHALL show the SAVES
panel for `saves/save.srm` with import and clear actions. When the game builds
with mod packages, the launcher SHALL show the Mods page and SHALL use the
framework's mod runtime provider. The launcher SHALL NOT edit the battery save
while a game session is running.

#### Scenario: Battery save

- **WHEN** the launcher opens on a fresh start
- **THEN** the SAVES panel targets `saves/save.srm` and can import or clear it

#### Scenario: Mods

- **WHEN** the launcher opens in a build with mod packages enabled
- **THEN** the Mods page lists the packages the runtime provider exposes

### Requirement: Generate & rebuild and setup packs

A build configured with `SNESRECOMP_SETUP_HOST=ON` and an empty `src/gen/` SHALL link the framework's codegen host and SHALL open the launcher's first-run Generate & rebuild wizard as its only path forward. The build SHALL
stage the launcher assets (`assets/fonts` and images) beside the executable so
`stage_setup_host.sh` can package them. The setup pack SHALL carry the source
tree and submodules needed for the player's rebuild and SHALL NOT carry a ROM,
generated C, or `recomp/funcs.h`.

#### Scenario: Setup host first run

- **WHEN** a player runs the setup host with their own ROM
- **THEN** the launcher offers Generate & rebuild, generates, rebuilds, and
  relaunches into the rebuilt game

#### Scenario: Packaging

- **WHEN** `scripts/package_release.sh` runs on a setup-host build
- **THEN** the launcher assets are present, the zip is produced, and it
  contains no ROM-derived output

#### Scenario: Playable build

- **WHEN** the project is built with generated code and no setup-host option
- **THEN** the launcher's Generate & rebuild action remains available but the
  game boots normally

### Requirement: Playable builds do not force setup

A build made with generated code SHALL open the normal launcher even when
`src/gen/dispatch_v2.c` is not present beside the executable or in the
discovered project root. Only a setup host (`SNESRECOMP_SETUP_HOST`) or an
explicit `SNESRECOMP_FORCE_SETUP` SHALL force the first-run wizard. The
Generate & rebuild action SHALL remain available when a project root is
present.

#### Scenario: Playable build moved away

- **WHEN** a playable build is copied to a folder without the source tree
- **THEN** the launcher opens normally and PLAY boots the game
- **AND** no first-run setup wizard is shown

#### Scenario: Source tree without generated C

- **WHEN** a playable build runs where a project root exists but
  `src/gen/dispatch_v2.c` is absent
- **THEN** the launcher opens normally instead of forcing Generate & rebuild

#### Scenario: Setup host

- **WHEN** a build configured with `SNESRECOMP_SETUP_HOST=ON` starts
- **THEN** the first-run wizard is shown, because that binary has no game code

#### Scenario: Explicit force

- **WHEN** `SNESRECOMP_FORCE_SETUP=1` is set
- **THEN** a playable build shows the first-run wizard for testing

### Requirement: Pinned launcher dependency

`recomp-ui` SHALL be a git submodule at `game/recomp-ui` and its commit SHALL
be recorded in `game/framework_pins.txt`. `record_pins.sh` SHALL report
`recomp-ui: ok` for the checked-out submodule. The `snesrecomp`, `recomp-net`
and `retcomm-rbengine` entries are provenance pins for a vendored tree, not
live gitlinks; a full `--check` reports them as present but not git checkouts,
and that is expected.

#### Scenario: Pin check

- **WHEN** `record_pins.sh --check framework_pins.txt` runs in `game/`
- **THEN** the report includes `recomp-ui: ok` at the pinned commit
- **AND** the vendored framework entries are reported as present but not git
  checkouts
