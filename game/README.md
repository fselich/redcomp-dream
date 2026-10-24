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
bash tools/regen.sh --rom "../rom/Uniracers (USA).sfc"
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local --parallel
```

`regen.sh` verifies the ROM, generates `src/gen/*.c` and re-syncs `recomp/funcs.h`. Run it again after any change under `recomp/`. Without `--rom` it tries `SNESRECOMP_ROM`, a ROM at the repository root, then the path in `rom.cfg`. Other flags: `--no-verify`, `--cfg-roots`, `--profile-manifest` (see `bash tools/regen.sh --help`).

The executable is `build-local/UniracersSNESRecomp`. It was scaffolded without the GUI launcher (`--no-recomp-ui`): with no ROM argument it looks beside the executable and in `rom.cfg`, then opens a file picker. The root `run.sh` passes the ROM and `--no-launcher`.

## Layout

| Path | What lives there |
|---|---|
| `src/` | the hand-written port (module table in `docs/ARCHITECTURE.md`) |
| `src/gen/` | generated C, local only |
| `recomp/` | analyzer input: `bank*.cfg` and `symbols.toml` (see [`recomp/README.md`](recomp/README.md)) |
| `tools/regen.sh` | ROM → C pipeline |
| `tools/validate.sh`, `tools/validate_refs.txt`, `tools/route-validate-dragster.txt` | the output gate (see `docs/VALIDATION.md`) |
| `scripts/package_release.sh` | builds the player-facing zip |
| `mods/preloaded/` | place for reviewed, default-disabled mod packages shipped with a release (empty) |
| `snesrecomp/` | the framework, vendored, with `patches-snesrecomp-local.diff` applied |
| `framework_pins.txt` | the framework commits the vendored tree is based on |
| `VERSION` | the project version |

## Working rules

- Never edit `src/gen/` by hand and never invent a result to get past code the recompiler could not translate: fix the analyzer input or the framework and regenerate. `src/gen_stubs.c` is the place for real hand-written bodies, and it is empty.
- After a change, run `tools/validate.sh` (from the repository root: `game/tools/validate.sh`).
- A framework change is made in `snesrecomp/` and then captured in `patches-snesrecomp-local.diff` (see `docs/ARCHITECTURE.md`).

## License

This project's own source is under `LICENSE`. The framework carries its own terms in `snesrecomp/LICENSE` and `snesrecomp/THIRD_PARTY_ATTRIBUTION.md`. Neither covers the game data, which is not distributed.
