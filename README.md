# Uniracers recomp

A native recompilation of **Uniracers** (USA) for desktop, built on [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp).

The port is playable. On top of the original game it adds:

- **widescreen races** from 16:9 up to 512 px wide, in 1P and in 2P/VS split screen, with the extra track drawn from the game's own track data, the HUD spread to the corners and riders kept correct in the margins;
- a small **radar** in the bottom-right corner with your position and the rival's;
- an **in-game options menu** (F10 or Select+L) that applies every option live and saves it;
- an opt-in **unlock of all nine tours**;
- save states, rewind, audio and controllers from the framework (netplay is not built into this project).

> **You must legally own a copy of the game.** No ROM data is distributed with this project, in the repository or in any release. The recompiled C is generated locally from your own copy and is never committed.

## Build

Put your ROM at `rom/Uniracers (USA).sfc` (the folder is git-ignored), check
out the launcher submodule, then:

```sh
git submodule update --init --recursive   # game/recomp-ui
cd game
bash tools/regen.sh --rom "../rom/Uniracers (USA).sfc"
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local --parallel
```

`regen.sh` checks the ROM against `game/rom_identity.txt` and refuses anything else, then generates `game/src/gen/`. More on the `game/` directory in [`game/README.md`](game/README.md).

The helper wraps the same configure/build steps:

```sh
game/tools/build.sh                                  # Linux -> game/build-local
game/tools/build.sh windows --fetch-toolchain         # .exe -> game/build-windows
game/tools/build.sh both --fetch-toolchain            # both
```

`--fetch-toolchain` downloads llvm-mingw into `game/.cache/` (ignored by git)
the first time; the Windows binary is a static `.exe` that needs `assets/`
beside it. The playable `.exe` does not need the source tree or a compiler; a
setup-host pack does, because its first-run wizard builds the game.

Create a ready-to-copy Windows folder/zip:

```sh
game/tools/package-windows.sh --build
# -> game/dist/redcomp-dream-<version>-windows-x64.zip
```

It contains the playable `.exe`, `assets/` (the launcher banner/fonts),
`mods/` when present, a README and the licenses. No ROM, source tree or
toolchain.

Create a single-file Linux AppImage:

```sh
game/tools/package-appimage.sh --build --fetch-toolchain
# -> game/dist/redcomp-dream-<version>-x86_64.AppImage
```

The AppImage contains the game, launcher and `assets/` inside the image; no asset folder is needed beside it. `config.ini`, `keybinds.ini`, `saves/` and `mods/` are written beside the `.AppImage`, so keep it in a writable folder. It needs Linux x86_64 with glibc 2.35+, OpenGL and FUSE/libfuse2 (or `--appimage-extract-and-run`); `--rom` selects a ROM when no `zenity`/`kdialog` is installed.

## Run

From the repository root:

```sh
./run.sh              # straight into the game (development path)
./run.sh --launcher   # pre-boot launcher: ROM, display, audio, input, saves, mods
```

The first time you run the executable without a ROM, the launcher asks for your
copy, checks it against `rom_identity.txt` and remembers it in `rom.cfg`. The
launcher is also where display, audio, controller bindings, save management,
mods and *Generate & rebuild* live. The **F10** menu is still the in-game
options menu: the launcher owns the generic host settings, F10 owns the port's
widescreen, margin, HUD, radar and unlock options. If you tick *Skip launcher
on boot*, `./run.sh --launcher` brings it back.

The executable keeps `config.ini`, `keybinds.ini` and `saves/` in `game/build-local/`. Extra arguments are passed to the game; use absolute paths, because the script starts the game from `game/build-local/`:

```sh
./run.sh --script "$PWD/tools/scripts/ws-native-map-spike/route-dragster-move.txt"
```

## Options

Press **F10** or **Select + L** in a race, in the game's menus or on the title screen to open the options menu. The options are also plain keys in `config.ini`:

```ini
[Graphics]
Widescreen = 1       ; on for a fresh config.ini; set 0 to turn it off

[Widescreen]
Aspect = auto        ; auto, 16:9, 21:9, 32:9, or a decimal such as 2.0
MaxWidth = 512       ; 342..512
Margin = source      ; source (track table) or clamp (native 256 px track)
Hud = 1
Cull = 1

[Video]
MiniMap = 1          ; the radar

[Emulation]
UnlockAllTours = 0
```

Widescreen appears during a live race in 1P and in 2P/VS split screen; the radar remains 1P-only. **`UnlockAllTours` is permanent for that save**: it raises the tour reveal stage in the battery save, which the game stores on exit; turning it off later does not hide the tours again. Keep a copy of `game/build-local/saves/save.srm` if you may want to go back.

Every option, its environment override, the precedence rules and the debug variables are in [`docs/CONFIG.md`](docs/CONFIG.md).

### Launcher art

`game/tools/launcher-art.sh` converts an image to the TGA the launcher uses and
rebuilds:

```sh
game/tools/launcher-art.sh --boxart cover.png --key-black --trim
game/tools/launcher-art.sh --brand mark.png --key-black --trim
game/tools/launcher-art.sh --clear
```

`--boxart` is the GAME card, `--brand` the small mark next to the title and
`--pad` the controller image. The generated files live in
`game/launcher_assets/`; commit them if the release should carry them.

## Checking a change

```sh
game/tools/validate.sh
```

runs a set of headless races on a copy of the executable (your saves are never touched) and compares frames and logs against `game/tools/validate_refs.txt`. See [`docs/VALIDATION.md`](docs/VALIDATION.md).

## Repository

| Path | What lives there |
|---|---|
| `game/` | the recomp project: hand-written `src/`, analyzer input `recomp/`, CMake, `tools/` |
| `game/snesrecomp/` | the framework, vendored with the local patch `game/patches-snesrecomp-local.diff` applied |
| `game/recomp-ui/` | the shared pre-boot launcher (a submodule) |
| `docs/` | documentation, starting at [`docs/README.md`](docs/README.md) |
| `openspec/` | requirements (`specs/`) and the history of every change (`changes/archive/`) |
| `tools/` | development scripts, input routes and research patches ([`tools/README.md`](tools/README.md)) |
| `maps/` | third-party line-art maps of 15 tracks, used as references when checking track topology; not game data |
| `rom/` | your own ROM, git-ignored |
| `run.sh` | launch from the repository root |

## License

The project's own source is under `game/LICENSE`. The framework carries its own terms in `game/snesrecomp/LICENSE` and `game/snesrecomp/THIRD_PARTY_ATTRIBUTION.md`. Game data is not distributed.
