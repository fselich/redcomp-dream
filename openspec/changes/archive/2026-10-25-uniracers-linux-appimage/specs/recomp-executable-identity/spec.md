## ADDED Requirements

### Requirement: Redcomp Dream executable name

The project SHALL build a `redcomp-dream` executable on Linux and a `redcomp-dream.exe` executable on Windows. The CMake target SHALL use the same name so the codegen host's `cmake_target` and `exe_basename` agree for Generate & rebuild. The in-game title, launcher header and desktop display name SHALL remain `Uniracers`.

#### Scenario: Linux binary

- **WHEN** the Linux build completes
- **THEN** `game/build-local/redcomp-dream` exists and `run.sh` starts it

#### Scenario: Windows binary

- **WHEN** the Windows cross-build completes
- **THEN** `game/build-windows/redcomp-dream.exe` exists

#### Scenario: Codegen target

- **WHEN** the setup host or launcher runs Generate & rebuild
- **THEN** it builds target `redcomp-dream` and relaunches `redcomp-dream`/`redcomp-dream.exe`, not the old target name

### Requirement: Windows executable icon

The Windows binary SHALL embed the brand mark as its executable icon. The icon SHALL be generated from `game/launcher_assets/brand_mark.tga` into `game/resources/redcomp-dream.ico`, reference-multiple sizes 256/128/64/48/32/16, and be compiled into the PE through a `.rc` resource on `WIN32`. The icon SHALL be the Explorer file icon; changing the SDL window icon is out of scope.

#### Scenario: Explorer icon

- **WHEN** `redcomp-dream.exe` is inspected in Explorer
- **THEN** it shows the wheel/brand mark icon rather than the generic executable icon

#### Scenario: Missing generated ICO

- **WHEN** `game/resources/redcomp-dream.ico` is absent
- **THEN** the build still succeeds without an embedded icon

#### Scenario: Brand art changes

- **WHEN** the player runs `tools/launcher-art.sh --brand image.png`
- **THEN** `game/resources/redcomp-dream.ico` is regenerated from the new brand mark

### Requirement: Packaging names

The Windows playable zip and the Linux AppImage SHALL use the `redcomp-dream-<version>-*` name and SHALL contain the renamed executable. The setup-pack release prefix MAY remain `uniracers`, but its setup host `--exe-name` SHALL be `redcomp-dream`.

#### Scenario: Windows package

- **WHEN** `tools/package-windows.sh` runs
- **THEN** `game/dist/redcomp-dream-<version>-windows-x64.zip` contains `redcomp-dream.exe`

#### Scenario: AppImage package

- **WHEN** `tools/package-appimage.sh` runs
- **THEN** `game/dist/redcomp-dream-<version>-x86_64.AppImage` contains `usr/bin/redcomp-dream` and its `AppRun`/`.desktop` exec that name

#### Scenario: Setup pack

- **WHEN** `scripts/package_release.sh` runs
- **THEN** the setup host and rebuilt game are named `redcomp-dream` while the pack prefix may stay `uniracers`
