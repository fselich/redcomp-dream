# game/

The Uniracers recomp project: the hand-written port, the analyzer input, the build and the framework. For what the port does and how to play it, see the [root README](../README.md); for how it is put together, [`docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md).

> **You must legally own a copy of the game.** The recompiled C in `src/gen/` is generated from your own ROM and is never committed.

## ROM identity

| | |
|---|---|
| File | `Uniracers (USA).sfc` |
| Mapping | LoROM |
| Region | USA |
| Coprocessor | none |
| CRC32 | `383858c7` |
| SHA-256 | `859ec99fdc25dd9b239d9085bf656e4f49c93a32faa5bb248da83efd68ebd478` |

The digests live in `rom_identity.txt`, read by `tools/regen.sh`, the build and the release script. `regen.sh` refuses any other dump, so a wrong revision fails at once instead of producing subtly wrong code.

## Generate and build

```sh
# from the repository root, once per checkout
git submodule update --init --recursive   # game/recomp-ui
cd game
bash tools/regen.sh --rom "../rom/Uniracers (USA).sfc"
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local --parallel
```

`regen.sh` verifies the ROM, generates `src/gen/*.c` and re-syncs `recomp/funcs.h`. Run it again after any change under `recomp/`. Without `--rom` it tries `SNESRECOMP_ROM`, a ROM at the repository root, then the path in `rom.cfg`. Other flags: `--no-verify`, `--cfg-roots`, `--profile-manifest` (see `bash tools/regen.sh --help`).

The executable is `build-local/redcomp-dream`. It opens the shared recomp-ui launcher at boot: with no ROM argument it looks beside the executable and in `rom.cfg`, then asks for one in the launcher. The root `run.sh` passes the ROM and `--no-launcher` for the development path; `./run.sh --launcher` opens the GUI. The in-game F10 menu is separate and unchanged: the launcher handles the generic host settings and the port's options stay in F10.

### Build helper

`tools/build.sh` wraps the configure/build steps:

```sh
tools/build.sh                              # Linux -> build-local
tools/build.sh windows --fetch-toolchain    # redcomp-dream.exe -> build-windows
tools/build.sh both --fetch-toolchain       # both
```

`--fetch-toolchain` downloads a pinned llvm-mingw into `.cache/` (ignored by
git) if no MinGW cross compiler is installed. The Windows result is a static
`.exe`; copy it with its `assets/` directory (and `mods/`, when present).
`--clean` runs the CMake clean target and keeps `config.ini` and `saves/`.

The playable `.exe` does **not** need the source tree or a compiler beside it.
A **setup host** (built with `SNESRECOMP_SETUP_HOST=ON`, as a release pack is)
does: its first-run wizard generates and rebuilds the game, so it needs the
cmake-clang toolchain.

To produce a playable zip instead of a setup pack:

```sh
tools/package-windows.sh              # package the existing build-windows
tools/package-windows.sh --build      # build first, then package
```

It writes `dist/redcomp-dream-<version>-windows-x64.zip` with
`redcomp-dream.exe`, the `assets/` folder (fonts, box art, brand, pad),
`mods/` when present, `README.txt` and the licenses. It carries no source
tree, generated C, ROM or toolchain. Run `tools/launcher-art.sh` before
packaging if you changed the banner or brand; the zip copies whatever is
staged in `build-windows/assets/`.

### Linux AppImage

```sh
tools/package-appimage.sh --build --fetch-toolchain
# -> dist/redcomp-dream-<version>-x86_64.AppImage
```

The AppImage bundles the game, the launcher and `assets/` inside the image (`usr/bin/assets`), so no asset folder is needed beside it. `config.ini`, `keybinds.ini`, `rom.cfg`, `saves/` and `mods/` are written beside the `.AppImage`; keep it in a writable folder. It needs Linux x86_64 with glibc 2.35+, a working OpenGL driver and FUSE/libfuse2 (or `--appimage-extract-and-run`). `--rom PATH` selects a ROM when `zenity`/`kdialog` are not installed. The build uses the retcomm Linux `cmake-clang-v1` pack, not the host Homebrew libraries. Mods are not embedded; the launcher creates/uses `mods/` beside the image.

## Layout

| Path | What lives there |
|---|---|
| `src/` | the hand-written port (module table in `docs/ARCHITECTURE.md`) |
| `src/gen/` | generated C, local only |
| `recomp/` | analyzer input: `bank*.cfg` and `symbols.toml` (see [`recomp/README.md`](recomp/README.md)) |
| `tools/regen.sh` | ROM → C pipeline |
| `tools/build.sh` | Linux / Windows (.exe) / both build helper |
| `tools/package-windows.sh` | playable Windows zip: exe + assets + mods + licenses |
| `tools/package-appimage.sh` | playable Linux AppImage: binary + embedded launcher assets |
| `tools/validate.sh`, `tools/validate_refs.txt`, `tools/route-validate-dragster.txt` | the output gate (see `docs/VALIDATION.md`) |
| `tools/launcher-art.sh` | convert box art / brand / pad images to the launcher TGAs and rebuild |
| `launcher_assets/` | optional launcher art (`boxart.tga`, `brand_mark.tga`, `pad.tga`) |
| `resources/` | Windows icon (`redcomp-dream.ico`) and its `.rc` template |
| `scripts/package_release.sh` | builds the player-facing zip |
| `mods/preloaded/` | place for reviewed, default-disabled mod packages shipped with a release (empty) |
| `snesrecomp/` | the framework, vendored, with `patches-snesrecomp-local.diff` applied |
| `recomp-ui/` | the shared Dear ImGui pre-boot launcher (ROM picker, verification, display/audio/input, saves, mods, Generate & rebuild) |
| `framework_pins.txt` | the framework and launcher commits the tree is based on |
| `VERSION` | the project version |

## Launcher art

`tools/launcher-art.sh` converts an image to the 32-bit TGA the pre-boot
launcher loads and rebuilds the game so it stages the result:

```sh
tools/launcher-art.sh --boxart cover.png --key-black --trim
tools/launcher-art.sh --brand mark.png --key-black --trim
tools/launcher-art.sh --clear
```

`--boxart` is the big GAME card, `--brand` the small mark next to the title,
and `--pad` the controller image. `--key-black` removes a black backdrop (the
usual case for a logo), `--trim` crops the transparent border, and `--fill`
crops to fill the slot instead of fitting it. The output goes to
`launcher_assets/`; commit it if the release should carry it. A brand image
also regenerates `resources/redcomp-dream.ico`, which is embedded in the
Windows executable.

## Working rules

- Never edit `src/gen/` by hand and never invent a result to get past code the recompiler could not translate: fix the analyzer input or the framework and regenerate. `src/gen_stubs.c` is the place for real hand-written bodies, and it is empty.
- After a change, run `tools/validate.sh` (from the repository root: `game/tools/validate.sh`).
- A framework change is made in `snesrecomp/` and then captured in `patches-snesrecomp-local.diff` (see `docs/ARCHITECTURE.md`).

## License

This project's own source is under `LICENSE`. The framework carries its own terms in `snesrecomp/LICENSE` and `snesrecomp/THIRD_PARTY_ATTRIBUTION.md`. Neither covers the game data, which is not distributed.
