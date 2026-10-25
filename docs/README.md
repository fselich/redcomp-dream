# Documentation

## Current state

- The port boots and plays Uniracers (USA) as a static recompilation on snesrecomp, with the framework's save states, rewind, audio and controllers. Netplay is not built into this project.
- In a live race the picture is widened from 16:9 to ultrawide (21:9, 32:9 or any decimal aspect, rendered up to the 512 px cap), in 1P and in 2P/VS split screen. Widescreen applies only in-game; menus and the title screen stay native. The track margins are filled from the game's own unpacked track table (`source`), with a native-width fallback (`clamp`); the HUD is spread to the corners; riders stay visible and correct in the margins. Each half of a split-screen race is widened individually, with its own camera, BG1 map and anchor. A small radar sits in the bottom-right corner in 1P only and can be turned off from the in-game menu.
- An in-game menu (F10 or Select+L) changes every option live and saves it to `config.ini`.
- An opt-in option unlocks all nine tours in the save.
- Automated checks cover the tour-1 1P cases, the 2P/VS width matrix and the Hopper PICK TRACK/race-start cases; the other later tours are not part of the matrix yet (see [`ROADMAP.md`](ROADMAP.md), R1).

## Documents

| Document | Contents |
|---|---|
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | repository layout, the `game/src` modules, the frame sequence, the guest-state rule, the framework patch |
| [`WIDESCREEN.md`](WIDESCREEN.md) | how the wide race picture is built: frame width, track, HUD, riders, radar, limits |
| [`TWO_PLAYER.md`](TWO_PLAYER.md) | 2P/VS split screen: detection, per-viewport BG1, HUD bands, the four rider slots, cull and validation |
| [`TRACK_MARGIN.md`](TRACK_MARGIN.md) | the `source` and `clamp` margin policies in detail, and the approaches that were rejected |
| [`CONFIG.md`](CONFIG.md) | every option, precedence, the in-game menu, debug and framework variables |
| [`VALIDATION.md`](VALIDATION.md) | the output gate, references and determinism, the settings test |
| [`ROADMAP.md`](ROADMAP.md) | open work |
| [`formats/TRACK_FORMAT.md`](formats/TRACK_FORMAT.md) | the ROM track directory, RNC1 compression and the `$7F` track image |
| [`formats/BG1_RING.md`](formats/BG1_RING.md) | the race BG1 ring and how screen positions map to world cells |
| [`formats/SAVE_FORMAT.md`](formats/SAVE_FORMAT.md) | battery SRAM: checksummed blocks, tour reveal stage, medals |

Each feature was developed as an OpenSpec change. The current requirements are in `openspec/specs/`; the archived changes in `openspec/changes/archive/` keep each proposal, design and `report.md` with the measured evidence.

A temporary folder of superseded investigation logs also exists under `docs/`; it is not maintained and will be removed.
