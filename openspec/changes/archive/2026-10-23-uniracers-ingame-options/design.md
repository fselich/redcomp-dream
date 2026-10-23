## Context

The port's options live in three places today:

```
 environment            config.ini / config.local.ini        compiled-in
 ───────────            ─────────────────────────────        ───────────
 SNESRECOMP_WS          [Graphics] Widescreen  (host:        kWsRaceMaxWidth = 512
 SNESRECOMP_WS_ASPECT              g_config.widescreen)
 SNESRECOMP_WS_MINIMAP  [Video] MiniMap       (WsConfigBool)
 SNESRECOMP_WS_HUD      [Emulation] UnlockAllTours
 SNESRECOMP_WS_CULL
 SNESRECOMP_WS_OFFWINDOW (+ legacy SNESRECOMP_WS_TRACK_CLAMP)
 SNESRECOMP_UNLOCK_TOURS
 + debug: *_LOG, *_TEST_*, GHOST_*, OAM_XHIGH
```

- `WsEnvStr` caches `getenv` by name. `WsEnvFlag`/`WsEnvInt` are called every frame (HUD, cull), so those options are already live once the source changes. `WsOffwindowMode` caches its answer in a function-local `static`. The radar and the unlock read config once in `GameAfterConfig`.
- `WsConfigBool` reads `config.ini` and then `config.local.ini`.
- The host owns `[Graphics] Widescreen` and exposes `snesrecomp_desktop_set_widescreen()`, which sets `g_config.widescreen` and calls `WriteConfigFile`. `WriteConfigFile` is surgical: it rewrites only the host's own keys and keeps every other line, so a `[Widescreen]` section written by the game survives it.
- The host also owns `[Graphics] Fullscreen` (0 windowed, 1 borderless, 2 exclusive) and `[Graphics] DisplayAspect` (`g_config.display_aspect`: CRT 4:3, square pixels 8:7, square frame 1:1). Its launcher already applies both live through the static `ApplyLiveSettings(before)`, which switches the window mode and resizes a non-custom window. The F hotkey toggles windowed/borderless and persists at shutdown. `PrepareFrame` already reads `g_config.display_aspect` every frame.
- The host (`game/snesrecomp/runner/src/desktop/host_main.c`) has one modal overlay, the save-state browser. Its loop freezes the guest, pauses audio, sends key presses to the panel through `PumpOverlayEvents` instead of `HandleInput`, polls pad navigation with `OverlayNavInputs`, and draws with `PresentFrozenWithOverlay`. `SnesDesktopHostGame` has no field for a title to add its own modal overlay, and `RtlGameInfo` has no way to filter guest input. That is why the menu needs a host patch (proposal, option B).
- Default keymap: `Load = F1…F10`, so F10 loads slot 10.

## Goals / Non-Goals

**Goals:**

- One place that knows every user option, its source, its range and how it applies.
- Change options from inside the game with keyboard or pad, see the result at once, and save it to `config.ini`.
- Keep the environment variables working, and winning, for scripts and debugging.
- Keep the host patch small, generic and in the same shape as the F11 menu, so it can be offered upstream.

**Non-Goals:**

- Window size, renderer, shaders and input mapping (fullscreen, pixel aspect and audio volume *are* in scope): they are host settings with their own UI (launcher, hotkeys).
- Exposing the debug variables or the parked `ghost` margin mode in the menu.
- A general-purpose widget toolkit: the menu is a list of rows with values.
- Netplay: the menu is unavailable while a netplay session is active.

## Decisions

### D1. `WsSettings`: one table of option descriptors

`game/src/ws_settings.c`/`.h` owns a static table. Each entry:

```
{ id, label, section, key, env, legacy_env, kind, choices/range,
  default, applies }        kind    = bool | enum | int
                            applies = live | next_race | restart | permanent_note
```

and a runtime value plus its `source` (`default | config | local | env`).

- `WsSettingsLoad()` runs from `GameAfterConfig`: default, then `config.ini`, then `config.local.ini`, then the environment. It generalises `WsConfigReadBool` to string values.
- Readers call `WsSettingGet(id)`, which is a plain array read and cheap enough for every frame.
- `[Graphics] Widescreen` is read from `g_config.widescreen`, which the host has already parsed. It is written through `snesrecomp_desktop_set_widescreen()`, never by the game's own writer, so the host stays the only owner of its key. `[Graphics] Fullscreen` and `[Graphics] DisplayAspect` work the same way through the display setter of D4. These host options are never locked: the host has no environment override for them.
- `WsEnvStr`/`WsEnvFlag`/`WsEnvInt` remain for the debug variables only.

*Alternative:* one `static` variable per module, as today. Rejected: the menu would need to know every module, and "locked by environment" would be re-implemented in each one.

### D2. Precedence and locking

The precedence is environment > `config.local.ini` > `config.ini` > default. An option whose winning source is `env` or `local` is **locked** in the menu: it is shown with a mark and `<` `>` does nothing. Saving the menu's value to `config.ini` would have no effect at the next launch, so the menu does not offer it.

`SNESRECOMP_WS_TRACK_CLAMP` keeps its legacy meaning: when it is set and `SNESRECOMP_WS_OFFWINDOW` is not, it locks `Margin = clamp`.

### D3. Writer: in-place, game keys only

`WsSettingsSave()` rewrites `config.ini`:

- it updates an existing `key = value` line in its section and keeps the rest of that line's layout;
- it appends missing keys at the end of their section, and creates `[Widescreen]` at the end of the file if it does not exist;
- it keeps comments, blank lines, unknown keys and other sections byte-for-byte;
- it writes to `config.ini.tmp` and renames, so a crash never leaves a half file.

It writes only options whose source is not locked and whose value differs from what the file has. `[Video] MiniMap` and `[Emulation] UnlockAllTours` use the same writer: the host does not own those keys (`WriteConfigFile` leaves them alone).

*Alternative:* extend the host's `WriteConfigFile` with title keys. Rejected: it grows the host patch for something the game can do on its own.

### D4. Host patch: a title modal overlay

A new optional block in `SnesDesktopHostGame`:

```c
const struct SnesTitleMenu {
  int  (*poll_open)(uint32_t pad);         /* gesture from the pad word   */
  int  (*is_open)(void);
  void (*handle_key)(int key, int repeat); /* SDL keycode, while open     */
  void (*poll_nav)(uint32_t pad, uint32_t ticks_ms);
  int  (*overlay_image)(const uint32_t **px, int *w, int *h);
  void (*close)(void);
  int  open_key;                           /* SDL keycode, e.g. F10       */
} *title_menu;
```

The host changes:

- In the main loop's key handling, `open_key` is checked **before** the keymap, so F10 opens the menu instead of loading slot 10. `poll_open` sees the same pad word the F11 gesture sees.
- A modal loop copies the save-state loop: freeze the guest, pause audio, `PumpOverlayEvents(running, title_menu->handle_key)`, `poll_nav`, present.
- `PresentFrozenWithOverlay` also asks `title_menu->overlay_image` and draws the panel the way it draws the browser panel.
- The guest input filter applies while the title menu is open, exactly as for the browser, so a direction held when the menu closes does not stick.
- The title menu and the save-state browser exclude each other: neither opens while the other is open.
- The menu does not open while netplay is active.

The patch also adds a display setter:

```c
/* Apply live (ApplyLiveSettings); persist (WriteConfigFile) when asked.
 * fullscreen: 0 windowed, 1 borderless, 2 exclusive. */
void snesrecomp_desktop_set_display(int fullscreen, int display_aspect,
                                    int persist);
```

It copies `g_config` into `before`, sets the two fields, calls `ApplyLiveSettings(&before)`, keeps `g_win_flags`/cursor in step as the F hotkey does, and calls `WriteConfigFile` only when `persist` is set. The menu applies host options with `persist = 0` while the player browses values and with `persist = 1` on *Save*. Widescreen is applied live by setting `g_config.widescreen`, and persisted on *Save* through `snesrecomp_desktop_set_widescreen()`. The frozen-frame presenter redraws at the new window size on its next pass, so changing the mode with the menu open is safe.

The patch goes into `game/patches-snesrecomp-local.diff`, which is how the project already carries its local host changes.

*Alternative (option A):* read `SDL_GetKeyboardState` from the game and draw through `draw_frame`, opening only in the game's pause. Rejected: the guest still receives every key and button (A on *Quit* leaves the race), the emulation cannot be frozen outside the game's pause, and F10 still loads slot 10.

*Alternative (option C):* add recomp-ui and use `in_game_launcher`. Rejected for now: it adds a submodule, and there is no evidence that it supports title-specific pages.

### D5. Menu: rows, values, panel

`game/src/ws_menu.c` builds a 512×448 ARGB panel (2× the SNES frame, the size of the browser panel) with `snes_ovl_*` from `snes_overlay_draw.h`:

```
+---------------- OPCIONES ----------------+
| > PANORAMICO              <  ON   >      |
|   PROPORCION              <  AUTO >      |
|   ANCHO MAXIMO            <  512  >      |
|   MARGEN DE PISTA         < SOURCE>      |
|   MARCADOR ANCHO          <  ON   >      |
|   VENTANA DE DESCARTE     <  ON   >      |
|   PANTALLA COMPLETA       < VENTANA >    |
|   FORMA DEL PIXEL         < CRT 4:3 >    |
|   MINIMAPA                   OFF  [ENV]  |
|   TODOS LOS TOURS         <  OFF  >      |
|                                          |
|   GUARDAR EN CONFIG.INI      CERRAR      |
| ---------------------------------------- |
| ! PERMANENTE PARA LA PARTIDA GUARDADA:   |
|   APAGARLO NO VUELVE A OCULTAR LOS TOURS |
+------------------------------------------+
```

The host's 8×8 font (`FONT8` in `snes_overlay_draw.c`) covers ASCII 32–90 only. Lowercase is drawn as uppercase and anything else as `?`. Labels are therefore uppercase ASCII without accents, arrows are `<` `>`, and the warning mark is `!`. The rows are drawn at scale 2 (16 px glyphs) on the 512×448 panel, which leaves room for about 30 characters per row.

- **Navigation:** pad ↑ ↓ and keys ↑ ↓ move between rows; `<` `>` (pad or keys) change the value; A/Enter activates *Save*/*Close*; B/Escape/F10 closes. Held directions auto-repeat like the browser.
- **Footer:** a help line for the selected row, shown when that option is locked, permanent, or not live.
- **Unsaved changes:** closing keeps the changes for this session. A "sin guardar" mark appears while values differ from the file.
- **Language:** Spanish labels, uppercase and unaccented (see the font note above). Extending the host font is out of scope.

### D6. Options and how they apply

| Row | Key | Env (locks) | Values | Applies |
|-----|-----|-------------|--------|---------|
| Panorámico | `[Graphics] Widescreen` | `SNESRECOMP_WS` | off/on | live |
| Proporción | `[Widescreen] Aspect` | `SNESRECOMP_WS_ASPECT` | auto, 16:9, 21:9, 32:9 | live |
| Ancho máximo | `[Widescreen] MaxWidth` | — | 384, 448, 512 | live |
| Margen de pista | `[Widescreen] Margin` | `SNESRECOMP_WS_OFFWINDOW`, `…_TRACK_CLAMP` | source, ring, clamp, wrap | live (ring state reset) |
| Marcador ancho | `[Widescreen] Hud` | `SNESRECOMP_WS_HUD` | on/off | live |
| Ventana de descarte | `[Widescreen] Cull` | `SNESRECOMP_WS_CULL` | on/off | live (tested mid-race) |
| Pantalla completa | `[Graphics] Fullscreen` | — | ventana, completa (borderless), exclusiva | live |
| Forma del pixel | `[Graphics] DisplayAspect` | — | CRT 4:3, 8:7, 1:1 | live |
| Volumen | `[Sound] Volume` | — | 0..100 (pasos de 10) | live |
| Minimapa | `[Video] MiniMap` | `SNESRECOMP_WS_MINIMAP` | on/off | live |
| Todos los tours | `[Emulation] UnlockAllTours` | `SNESRECOMP_UNLOCK_TOURS` | off/on | permanent note |

- An `Aspect` value in the file that is not one of the choices (e.g. `2.0`) is kept and shown as such. `<` `>` cycles into the list.
- A `MaxWidth` in the file is clamped to [342, 512]. The menu offers the three presets plus the current value if it is different.
- **Margin:** `WsOffwindowMode` drops its `static` cache and reads the setting. When the mode changes, the menu calls `WsOwReset()` so the ring starts clean on the next frame. If a mid-race switch shows artifacts during testing, the row becomes `next_race` instead.
- **Cull:** turning it off restores `$0421/$0423` (existing behaviour). Live switching mid-race is part of the test plan.
- **Unlock tours:** `SaveUnlockTick` already runs every frame, so turning it on raises `$10D3` at once. A tour screen that is already open shows the change the next time it is entered. Turning it off does nothing to the save, and the footer says so.
- The `restart` class is supported by the menu (footer: "Requiere reiniciar") but no option in scope uses it.

### D7. Freeze-safe apply

Values change while the guest is frozen. Options that drive the frame width (`Panorámico`, `Proporción`, `Ancho máximo`) take effect at the next `PrepareFrame`, which runs after the menu closes. While the menu is open, the frozen frame keeps its width. Nothing writes guest RAM from the menu except through the existing per-frame paths (cull, unlock), which run only when the guest runs.

## Risks / Trade-offs

- [Host patch drifts on a submodule bump] → The patch is small and touches the same lines as the F11 wiring. The tasks include a re-apply check, and the hook is generic enough to propose upstream.
- [F10 no longer loads slot 10] → Documented in the README. Slot 10 stays reachable through the F11 browser. `open_key` is one constant to change.
- [Mid-race margin or cull switch leaves stale state] → `WsOwReset()` on change, and an explicit mid-race test. Fallback: mark the row `next_race`.
- [Writer corrupts a hand-edited `config.ini`] → Line-preserving rewrite, temp file and rename, and a test that round-trips a file with comments, unknown keys and host sections byte-for-byte except the changed keys.
- [A host write persists unsaved host options] → If the host writes `config.ini` for its own reasons (volume or fullscreen hotkey, launcher) while the menu has unsaved fullscreen, pixel-aspect or widescreen changes, those changes are saved too. Accepted: it is the host's normal behaviour for its own keys, and the values are the ones on screen.
- [Two writers on one file] → The host writes its own keys and the game writes its own. Both are surgical and both run on the main thread, so they never write at the same time.
- [Locked options confuse players] → The row shows the source (`[env]`/`[local]`), and the footer names the variable or file that sets it.

## Open Questions

- `Proporción` (target aspect of the race frame) and `Forma del pixel` (how SNES pixels are stretched) are easy to confuse. The footer of each row explains it; a better label can come from testing.
- Should *Close* with unsaved changes ask for confirmation, or is the "sin guardar" mark enough? Proposed: the mark is enough.
- `32:9` in the aspect list: with the 512 px cap it renders the same as any aspect past ~2.28. Keep it for clarity, or drop it?
