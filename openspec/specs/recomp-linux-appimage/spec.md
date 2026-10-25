# recomp-linux-appimage Specification

## Purpose

The single-file Linux AppImage: the playable game and recomp-ui launcher with embedded assets, writable state beside the image, the portable toolchain baseline, desktop integration, ROM fallback and package verification.

## Requirements
### Requirement: Single-file playable AppImage

The build SHALL produce one `redcomp-dream-<version>-x86_64.AppImage` that contains the playable game, the recomp-ui launcher and its assets, and SHALL run without an `assets/` folder, source tree, generated C, toolchain or ROM beside it. The AppImage SHALL be a type 2 AppImage and SHALL accept a ROM path as a command-line argument.

#### Scenario: Run from a clean folder

- **WHEN** the player copies only the `.AppImage` to a writable folder and runs it
- **THEN** the launcher opens with its fonts and artwork and the game boots after PLAY or `--rom`

#### Scenario: No game data inside

- **WHEN** the AppImage contents are inspected
- **THEN** they contain no ROM, `src/gen/*.c`, `recomp/funcs.h`, `config.ini`, `saves/` or source tree

### Requirement: Embedded launcher assets

The AppImage SHALL contain the launcher assets inside the image, at `usr/bin/assets`, so recomp-ui's `SDL_GetBasePath()` resolution finds the fonts, box art, brand, pad and icons from the read-only mount. The image SHALL NOT require an `assets/` directory beside the `.AppImage`.

#### Scenario: Banner from inside the image

- **WHEN** the launcher opens from an AppImage with no `assets/` folder beside it
- **THEN** the GAME card shows the packaged box art and the header shows the packaged brand

#### Scenario: Launcher text

- **WHEN** the launcher draws any text
- **THEN** it uses the fonts packaged under `usr/bin/assets/fonts`

### Requirement: Writable state beside the AppImage

The AppImage SHALL keep `config.ini`, `keybinds.ini`, `rom.cfg`, `saves/` and the runtime `mods/` directory beside the user-visible `.AppImage` when that folder is writable, using the framework's `$APPIMAGE` policy. It SHALL NOT write those files inside the read-only squashfs mount.

#### Scenario: First run

- **WHEN** the AppImage runs from a writable folder with no `config.ini`
- **THEN** the default `config.ini` and `keybinds.ini` are created beside the `.AppImage`
- **AND** no write is attempted inside the mount

#### Scenario: Save state and rewind

- **WHEN** the player saves a state or changes a setting
- **THEN** the state/config lands beside the `.AppImage` and survives a restart

#### Scenario: Read-only location

- **WHEN** the `.AppImage` is placed in a read-only folder
- **THEN** the launcher reports that config and saves cannot be anchored there and falls back to the current directory

### Requirement: Portable build baseline

The AppImage binary SHALL be built with the project's Linux `cmake-clang-v1` toolchain pack against its glibc 2.35 sysroot, with SDL3 linked statically, and SHALL NOT depend on a build-host library such as Homebrew's SDL3, FreeType or HarfBuzz. Any remaining non-system library SHALL be bundled in the AppDir and reachable at runtime.

#### Scenario: Build-host paths

- **WHEN** `ldd` runs on the packaged binary
- **THEN** it names no path under the build checkout, Homebrew or another build-host prefix

#### Scenario: System libraries

- **WHEN** the AppImage runs on a supported distro
- **THEN** it uses the system OpenGL and display libraries and needs no bundled Mesa or X11

### Requirement: Desktop entry and FUSE fallback

The AppImage SHALL include an `AppRun`, a `.desktop` entry and a 256×256 icon, and SHALL be directly executable. The README SHALL state the FUSE/libfuse2 requirement and the `--appimage-extract-and-run` fallback for systems without FUSE.

#### Scenario: Desktop entry

- **WHEN** the AppImage is integrated by AppImageLauncher or inspected
- **THEN** it presents `Name=Uniracers`, `Icon=uniracers`, `Categories=Game;` and a working `Exec`

#### Scenario: No FUSE

- **WHEN** FUSE is unavailable on the player's system
- **THEN** `--appimage-extract-and-run` still starts the launcher

### Requirement: ROM selection fallback

The AppImage SHALL accept a positional ROM path or `--rom <path>` and SHALL pass it to the host. When the desktop has no `zenity` or `kdialog`, the README SHALL document the command-line and drag-and-drop fallbacks instead of a native Browse dialog.

#### Scenario: ROM by argument

- **WHEN** the player runs `redcomp-dream-<version>-x86_64.AppImage --rom /path/game.sfc`
- **THEN** the launcher opens on that ROM and PLAY boots it

#### Scenario: No picker dependency

- **WHEN** `zenity` and `kdialog` are both absent
- **THEN** the game still starts from `--rom` and does not fail because the picker is missing

### Requirement: Package verification

The packaging script SHALL stage an AppDir with the executable, `usr/bin/assets`, `AppRun`, `.desktop`, icon, README, VERSION, build pins and the licenses, SHALL package it with a pinned `appimagetool`, and SHALL write `redcomp-dream-<version>-x86_64.AppImage` plus a SHA-256 file. It SHALL smoke-test the image by starting it with `--appimage-extract-and-run` and quitting via the launcher script.

#### Scenario: Reproducible package

- **WHEN** `game/tools/package-appimage.sh --build` completes
- **THEN** `game/dist/redcomp-dream-<version>-x86_64.AppImage` and its `.sha256` exist

#### Scenario: Smoke test

- **WHEN** the packaging script runs its smoke test
- **THEN** the launcher opens from the image, the packaged artwork is visible, and the process exits cleanly

#### Scenario: Tool mismatch

- **WHEN** the downloaded `appimagetool` does not match its pinned SHA-256
- **THEN** the script fails instead of producing an image with an unverified tool
