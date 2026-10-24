# Architecture

The port is a static recompilation of the Uniracers ROM on top of the [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) framework. The game logic runs as generated C (`game/src/gen/`, produced locally from your ROM by `game/tools/regen.sh` and never committed), with the framework's interpreter covering whatever the analyzer could not prove. The framework also provides the PPU, APU, desktop host, launcher, save states, rewind and netplay. The hand-written part, `game/src/`, is the glue plus the widescreen and quality-of-life features.

## Layout

| Path | What it holds |
|---|---|
| `game/src/` | hand-written port code (below) |
| `game/src/gen/` | generated C, local only |
| `game/recomp/` | analyzer input: per-bank `bank*.cfg` and `symbols.toml` (see `game/recomp/README.md`) |
| `game/tools/` | `regen.sh` (generate C from a verified ROM), `validate.sh` and its references |
| `game/snesrecomp/` | the framework, vendored, with the local patch applied |
| `game/patches-snesrecomp-local.diff` | that patch against the pin in `game/framework_pins.txt` |
| `game/rom_identity.txt` | the accepted ROM digests |
| `tools/` | development scripts, routes and research patches ([`tools/README.md`](../tools/README.md)) |

## Modules (`game/src/`)

| Module | Owns | Writes |
|---|---|---|
| `main.c` | desktop host shim: game identity, the race frame width (`PrepareFrame`), host hooks | nothing |
| `game_rtl.c` | frame boundary (`GameRunOneFrame`), draw orchestration (`GameDrawPpuFrame`), host callbacks, `kGameInfo` | nothing |
| `host_contract.c`, `gen_stubs.c` | empty trace hooks for production builds; hand-written bodies for untranslatable code (none today) | nothing |
| `variables.h` | named WRAM and SRAM addresses and the 16-bit read/write helpers | — |
| `ws_frame.[ch]` | "widescreen enabled" and "live race" predicates (including the plausible-cull check), the extra width `E`, the per-frame context, cached environment lookups, small parsers | nothing |
| `ws_settings.[ch]` | the settings model: every option's key, environment variable, values, default, precedence, source and the in-place `config.ini` writer | `config.ini` (on save only) |
| `ws_menu.[ch]` | in-game options menu (F10 / Select+L), a host title-menu overlay | nothing |
| `ws_cull.[ch]` | rider cull-window widening and the shared cull-plausibility helper used by the live gate and the base wait | **guest WRAM** (`$0421`/`$0423`) |
| `ws_hud.[ch]` | BG3 elastic band and clamp bands | PPU configuration |
| `ws_oam_fix.[ch]` | 1P X-high/parked pair/sentinel; 2P frame-level sentinel/X and the per-line high/size synthesis for the bottom half | PPU OAM copy |
| `ws_margin.[ch]` | per-viewport BG1 margin policy (`source`/`clamp`), hook installation, the tile hook, the BG1 clamp; the line hook also calls the 2P per-line OAM fix | PPU configuration |
| `ws_ring.[ch]` | BG1 ring observation: seen table, scroll unwrap | nothing |
| `ws_source.[ch]` | `$7F` track-table reader, the source anchor and the header start seed (X from the start world/header, Y from the first samples) | nothing |
| `ws_radar.[ch]` | 64×32 radar panel | a PPU extra object |
| `ws_debug.[ch]` | lazily opened debug log files | log files |
| `uniracers_save.[ch]` | SRAM block checksums and the opt-in tour unlock | **guest SRAM** (`$10D3`, opt-in) |

**Guest-state rule.** `ws_cull.c` is the only module that writes guest WRAM in normal play, and `uniracers_save.c` the only one that writes guest SRAM (one byte, only with `UnlockAllTours`). Everything else is presentation: it reads guest memory and PPU state and changes only what the PPU draws. A feature that needs a new guest write must say so in its change and keep it in one module.

## Frame sequence

```text
GameRunOneFrame
  WsApplyCull                 widen (or restore) the cull window
  beam frame driver           run one field of the game
  WsApplyCull                 catch a base the game wrote during the frame

GameDrawPpuFrame
  WsFrameCtxBuild             wide? live race? E? 2P?
  WsOamFix                    rider OAM fixes in the PPU copy
  WsRadarUpdate               radar panel
  WsMarginFrameBegin          clamp BG1, or install the line and tile hooks
  WsApplyHud                  BG3 bands
  beam driver draw            rasterise; hooks run per line and per margin sample
  WsMarginFrameEnd            remove hooks, advance or drop the source anchor

GameAfterRunFrame
  SaveUnlockTick              opt-in tour unlock
```

Settings are loaded once after the host reads its configuration (`GameAfterConfig`) and then read every frame, so the menu applies changes live. `GameSessionReset` resets the beam driver on a rematch or soft return.

In 2P/VS, wide or native 4:3, the beam line hook also calls
`WsOamLineFix(ppu, line)` before the BG1 margin observation; it sets the
line's half rider slots to the big size bit and the true X high bit in the
PPU OAM copy, and hides the half's sentinel in native. The line hook is
installed for 2P even when the margin policy is `clamp`.

## Framework patch

The port relies on a few framework changes kept in `game/patches-snesrecomp-local.diff`:

- `g_snes_beam_line_hook`: a per-line host hook after the line's raster journal and HDMA, before it is drawn (ring observation);
- `g_ppu_margin_tile_hook`: a per-sample hook for BG tiles outside the native 256 px (margin policy);
- the BG pixel-source probe (`SNESRECOMP_BGPROBE_*`), used by the validation gate;
- the title-menu overlay hook and the live display and volume setters in the desktop host (options menu);
- the host config parser accepts the port's `[Widescreen]` section;
- the overlay 8×8 font gains `[ \ ] ^ _` (used by the menu).

The vendored tree must always equal the pin plus this patch. To refresh the framework, check out the new pin and reapply the patch; to change the framework, edit `game/snesrecomp/` and regenerate the patch from the tree. The archived change `uniracers-code-cleanup-2` describes how the patch was last regenerated and checked.

Research instrumentation that is not part of the build lives in `tools/patches/research/` (today the `$7F` read watch).

## Where the design decisions are

Each feature was built as an OpenSpec change; the archived change folders (`openspec/changes/archive/<date>-<name>/`) hold the proposal, design, the requirement deltas and a `report.md` with the evidence. The current requirements are in `openspec/specs/`.
