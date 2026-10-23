## Why

Every user-facing option of the port is set through environment variables (`SNESRECOMP_WS_*`, `SNESRECOMP_UNLOCK_TOURS`) or by editing `config.ini` by hand, and the readers are scattered: `WsEnvStr` caches `getenv` per name, `WsConfigBool` re-reads `config.ini`/`config.local.ini`, and some modules cache their result in a `static` on first use. A player cannot try 21:9, turn the minimap off or unlock the tours without quitting the game. `docs/UNIRACERS_MEJORAS.md` A6 (options in `config.ini`) and A6b (in-game options menu) describe this; the unlock-tours change added one more option and made the gap obvious.

## What Changes

- **Settings model (A6).** One `WsSettings` structure in the game code holds every user-facing option. Each value is resolved with the precedence environment > `config.local.ini` > `config.ini` > default, and the structure records where each value came from. Modules read the structure instead of calling `getenv`/`WsConfigBool` themselves.
- **New `config.ini` section `[Widescreen]`** for the options that have no key today: `Aspect`, `MaxWidth`, `Margin`, `Hud`, `Cull`. The existing keys stay where they are: `[Graphics] Widescreen`, `[Graphics] Fullscreen` and `[Graphics] DisplayAspect` (host-owned), `[Video] MiniMap`, `[Emulation] UnlockAllTours`.
- **Settings writer.** The game writes its own keys back to `config.ini` in place, keeping comments, unknown keys and the host's sections intact.
- **Host hook for a title menu.** A small patch to the snesrecomp host, carried in `game/patches-snesrecomp-local.diff`, lets a title register a modal overlay menu. The host wires it exactly like the save-state browser (F11): it freezes the guest, routes keys and pad navigation to the menu instead of the game, and presents the frozen frame with the menu panel on top. The patch also exposes a setter for the host's fullscreen mode and pixel aspect that applies them live (the same path the launcher uses, `ApplyLiveSettings`) and persists them.
- **In-game options menu (A6b).** It opens with **F10** or **Select+L** and lists: widescreen on/off, aspect, maximum width, track margin, wide HUD, cull window, minimap, fullscreen (windowed / borderless / exclusive), pixel aspect (CRT 4:3 / 8:7 / 1:1), audio volume, unlock all tours. `<` `>` changes a value and applies it live; *Save* writes `config.ini`. While the menu is open, F10 opens it and does not load save-state slot 10 (the host's default `Load` binding); this is documented.
- **Options locked by the environment.** If an environment variable sets an option, it wins: the menu shows the value with an "environment" mark and does not let the player change it. The same applies to `config.local.ini`.
- **Warnings.** Each option carries an "applies" class. Unlock all tours shows that it is permanent for the save. An option that only takes effect at the next race or after a restart says so. All options in scope apply live or at the next race; the mechanism exists for future options that need a restart.
- **Out of the menu:** window size, renderer and shaders (host-owned), and the debug variables (`*_LOG`, `*_TEST_*`, `GHOST_AHEAD`, `GHOST_MAXSTEPS`, `GHOST_RESYNC`, `OAM_XHIGH`, `TRACK_CLAMP`, and the parked `ghost` margin mode). They keep working as environment variables.
- The menu is not available during netplay.

## Capabilities

### New Capabilities

- `recomp-ws-settings`: the settings model: option list (including the host-owned audio volume), sources and precedence, the `[Widescreen]` section, the in-place writer, live re-application.
- `recomp-ingame-options-menu`: the host's title-menu hook and the options menu built on it: opening, freezing, navigation, live apply, save, locked options, warnings, netplay.

### Modified Capabilities

- `recomp-ws-ultrawide`: the target aspect also comes from `[Widescreen] Aspect`, and the 512 px cap becomes a `MaxWidth` setting (512 remains the maximum).
- `recomp-ws-source-default`: the margin policy is also selectable with `[Widescreen] Margin`, not only with `SNESRECOMP_WS_OFFWINDOW`.
- `recomp-ws-hud`: the HUD switch is also `[Widescreen] Hud`.

## Impact

- `game/src/ws_frame.c`/`.h`: `WsEnvStr`, `WsEnvFlag`, `WsEnvInt`, `WsConfigBool` stay for the debug variables; user options move to a new `game/src/ws_settings.c`/`.h`.
- Readers that change: `main.c` (`WsTargetAspect`, `kWsRaceMaxWidth`, `kGameHost`), `ws_frame.c` (`WsWideEnabled`), `ws_ring.c` (`WsOffwindowMode`, now re-read), `ws_hud.c`, `ws_cull.c`, `ws_radar.c`, `uniracers_save.c`.
- New `game/src/ws_menu.c`/`.h`: the options menu.
- `game/snesrecomp` (submodule): `runner/src/desktop/host_main.c`/`.h`, a new title-menu field in `SnesDesktopHostGame`. The patch is added to `game/patches-snesrecomp-local.diff`; a submodule bump must re-apply it.
- `config.ini`: new `[Widescreen]` section; existing keys unchanged. `[Graphics] Fullscreen` and `DisplayAspect` are written by the host, as today.
- Docs: `README.md` (menu, keys, `[Widescreen]`), `docs/UNIRACERS_MEJORAS.md` (A6/A6b status).
