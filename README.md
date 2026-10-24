# Uniracers recomp

A native recompilation of **Uniracers** (USA) for desktop, built on [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp).

The port is playable. On top of the original game it adds:

- **widescreen races** from 16:9 up to 512 px wide, with the extra track drawn from the game's own track data, the HUD spread to the corners and riders kept correct in the margins;
- a small **radar** in the bottom-right corner with your position and the rival's;
- an **in-game options menu** (F10 or Select+L) that applies every option live and saves it;
- an opt-in **unlock of all nine tours**;
- save states, rewind, audio and controllers from the framework (netplay is not built into this project).

> **You must legally own a copy of the game.** No ROM data is distributed with this project, in the repository or in any release. The recompiled C is generated locally from your own copy and is never committed.

## Build

Put your ROM at `rom/Uniracers (USA).sfc` (the folder is git-ignored), then:

```sh
cd game
bash tools/regen.sh --rom "../rom/Uniracers (USA).sfc"
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local --parallel
```

`regen.sh` checks the ROM against `game/rom_identity.txt` and refuses anything else, then generates `game/src/gen/`. More on the `game/` directory in [`game/README.md`](game/README.md).

## Run

From the repository root:

```sh
./run.sh
```

The executable keeps `config.ini`, `keybinds.ini` and `saves/` in `game/build-local/`. Extra arguments are passed to the game; use absolute paths, because the script starts the game from `game/build-local/`:

```sh
./run.sh --script "$PWD/tools/scripts/ws-native-map-spike/route-dragster-move.txt"
```

## Options

Press **F10** or **Select + L** in a race, in the game's menus or on the title screen to open the options menu. The options are also plain keys in `config.ini`:

```ini
[Graphics]
Widescreen = 1       ; off in a fresh config.ini

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

Widescreen and the radar only appear during a live race. **`UnlockAllTours` is permanent for that save**: it raises the tour reveal stage in the battery save, which the game stores on exit; turning it off later does not hide the tours again. Keep a copy of `game/build-local/saves/save.srm` if you may want to go back.

Every option, its environment override, the precedence rules and the debug variables are in [`docs/CONFIG.md`](docs/CONFIG.md).

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
| `docs/` | documentation, starting at [`docs/README.md`](docs/README.md) |
| `openspec/` | requirements (`specs/`) and the history of every change (`changes/archive/`) |
| `tools/` | development scripts, input routes and research patches ([`tools/README.md`](tools/README.md)) |
| `maps/` | third-party line-art maps of 15 tracks, used as references when checking track topology; not game data |
| `rom/` | your own ROM, git-ignored |
| `run.sh` | launch from the repository root |

## License

The project's own source is under `game/LICENSE`. The framework carries its own terms in `game/snesrecomp/LICENSE` and `game/snesrecomp/THIRD_PARTY_ATTRIBUTION.md`. Game data is not distributed.
