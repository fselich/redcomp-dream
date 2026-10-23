# Uniracers recomp

A native recompilation of **Uniracers** (USA), built on
[snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp).

The port is playable. It includes:

- widescreen/ultrawide presentation up to 512 px, with the track table serving
  both margins (`source` is the default);
- the rival OAM X-high fix and the parked-sprite/zoomed-track follow-ups;
- a local 64×32 radar in the lower-right corner, with player and rival markers;
- save states, rewind, audio and controller support from the framework.

> **You must legally own a copy of the game.** No ROM data is distributed with
> this project, in the repository or in any release. The recompiled C is
> generated locally from your own copy and is never committed.

## Repository layout

| Path | What lives there |
|---|---|
| `game/` | The Uniracers recomp project (`src/`, `recomp/`, CMake, tools) |
| `game/snesrecomp/` | Framework working tree, including the local patches |
| `game/patches-snesrecomp-local.diff` | Diff of those local framework changes |
| `docs/` | Durable research, format notes, roadmap |
| `openspec/` | OpenSpec specs and change history |
| `maps/` | External line-art reference maps (see provenance notes) |
| `tools/` | Durable research scripts, routes and patches |
| `rom/` | Local place for your own ROM (git-ignored) |
| `run.sh` | Launch from the repository root |

`work/` is intentionally not part of the repository, and `rom/` is ignored so
the ROM is never committed.

## Build

Place your legally owned ROM in `rom/` (or pass its path):

```sh
cd game
bash tools/regen.sh --rom "../rom/Uniracers (USA).sfc"
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local --parallel
```

The ROM is checked against `rom_identity.txt` before generation. `src/gen/`
is generated from it and is never committed.

If `snesrecomp/` ever needs to be refreshed from upstream, reapply
`game/patches-snesrecomp-local.diff`; the vendored tree currently includes the
framework changes needed by this port.

## Run

From the repository root:

```sh
./run.sh
```

The script changes to `game/build-local/` internally so `config.ini`,
`keybinds.ini` and `saves/` stay beside the executable. Widescreen and the
radar only appear during a live race, not in menus. A fresh `config.ini`
starts with `Widescreen = 0`; set `Widescreen = 1` under `[Graphics]` to
enable both (the radar is on by default; `[Video] MiniMap = 0` hides it).
`SNESRECOMP_WS=1` overrides the setting for one session. Extra arguments are
passed through:

```sh
./run.sh --script tools/scripts/ws-native-map-spike/route-dragster-move.txt
SNESRECOMP_WS_MINIMAP=0 ./run.sh
```

## Documentation

- `docs/UNIRACERS_TRACK_FORMAT.md` — decoded `$7F` track format.
- `docs/UNIRACERS_BG1_RING_FINDINGS.md` — BG1 ring, margins and the
  leading-edge prototype.
- `docs/UNIRACERS_NATIVE_MAP_HANDOFF.md` — current handoff/state.
- `docs/UNIRACERS_WS_PARKED_SPRITES.md` — parked OAM entries and the
  right-edge sliver at the 512 px cap.
- `docs/UNIRACERS_MEJORAS.md` — roadmap and ideas.
- `openspec/` — the change history, including the reports for each phase.

The `game/README.md` is the scaffold-era project README; the durable status is
in `docs/` and `openspec/`.

## License

The project's own source is under `game/LICENSE`. The framework carries its
own terms in `game/snesrecomp/LICENSE` and
`game/snesrecomp/THIRD_PARTY_ATTRIBUTION.md`. Game data is not distributed.
