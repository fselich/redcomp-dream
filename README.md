# Uniracers recomp

A native recompilation of **Uniracers** (USA), built on
[snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp).

The port is playable. It includes:

- widescreen/ultrawide presentation up to 512 px, with the track table serving
  both margins (`source` is the default);
- an in-game options menu (F10 or Select+L) that changes the widescreen and
  display settings live and saves them to `config.ini`;
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

After changing the port, run the output gate (it uses a copy of the executable
and never touches your saves):

```sh
game/tools/validate.sh            # compare against game/tools/validate_refs.txt
game/tools/validate.sh --record   # re-record references (double run)
```

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

### Options menu (F10 / Select+L)

Press **F10** or **Select + L** in a race, in the game's own menus or on the
title screen. The game freezes and a panel lists the port's user options:
widescreen, aspect, maximum width, track margin, wide HUD, cull window,
fullscreen, pixel aspect, audio volume, minimap and unlock all tours. Arrow
keys or the d-pad move between rows; left/right or `<`/`>` change a value
and apply it live; A/Enter activates *Guardar en config.ini* or *Cerrar*;
B/Escape/F10 closes. Closing keeps the changes for the session, and an
unsaved mark shows while they differ from the file.

F10 no longer loads save-state slot 10. Slot 10 stays reachable from the
F11 save-state browser (or by rebinding `Load` in `keybinds.ini`/`config.ini`).
The menu is not available during netplay.

The settings live in `config.ini`:

```ini
[Widescreen]
Aspect = auto        ; auto, 16:9, 21:9, 32:9 (a decimal such as 2.0 works too)
MaxWidth = 512       ; clamped to 342..512
Margin = source      ; source, ring, clamp, wrap
Hud = 1
Cull = 1
```

`[Graphics] Widescreen`, `Fullscreen` and `DisplayAspect`, `[Video] MiniMap`
and `[Emulation] UnlockAllTours` keep their existing keys. The precedence is
**environment > `config.local.ini` > `config.ini` > default**; an option set
by the environment or by `config.local.ini` is shown with `[ENV]`/`[LOCAL]`
and cannot be changed in the menu. `SNESRECOMP_WS_TRACK_CLAMP` keeps its
legacy meaning and locks the margin to `clamp`. The debug variables
(`*_LOG`, `*_TEST_*`, `GHOST_*`, `OAM_XHIGH`) are still environment-only and
are not in the menu.

### Unlock every tour

The game reveals its nine tours in stages as you earn medals. To have all of
them from the start, add to `config.ini`:

```ini
[Emulation]
UnlockAllTours = 1
```

`SNESRECOMP_UNLOCK_TOURS=1 ./run.sh` does the same for one session.

The option raises one byte in your save (`$10D3`, the tour reveal stage, see
`docs/UNIRACERS_SAVE_FORMAT.md`). Medals, records and names stay as they are.
The change is **permanent for that save**: the game stores it on exit, and
turning the option off later does not hide the tours again. It has no effect
in netplay. To go back, restore a copy of `game/build-local/saves/save.srm`
made beforehand. `tools/scripts/save/srm.py` can inspect a save (`dump`,
`verify`) or write an unlocked copy (`unlock IN OUT`) without the option.

## Documentation

- `docs/UNIRACERS_TRACK_FORMAT.md` — decoded `$7F` track format.
- `docs/UNIRACERS_BG1_RING_FINDINGS.md` — BG1 ring, margins and the
  leading-edge prototype.
- `docs/UNIRACERS_NATIVE_MAP_HANDOFF.md` — current handoff/state.
- `docs/UNIRACERS_WS_PARKED_SPRITES.md` — parked OAM entries and the
  right-edge sliver at the 512 px cap.
- `docs/UNIRACERS_SAVE_FORMAT.md` — battery SRAM: checksummed blocks, tour
  reveal stage, medals.
- `docs/UNIRACERS_MEJORAS.md` — roadmap and ideas.
- `openspec/` — the change history, including the reports for each phase.

The `game/README.md` is the scaffold-era project README; the durable status is
in `docs/` and `openspec/`.

## License

The project's own source is under `game/LICENSE`. The framework carries its
own terms in `game/snesrecomp/LICENSE` and
`game/snesrecomp/THIRD_PARTY_ATTRIBUTION.md`. Game data is not distributed.
