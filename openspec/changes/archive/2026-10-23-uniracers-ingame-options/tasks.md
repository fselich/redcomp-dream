## 1. Settings model (A6)

- [x] 1.1 Create `game/src/ws_settings.c`/`.h` with the option table (id, label, section, key, env, legacy env, kind, values/range, default, apply class) and the runtime value plus its source; add it to `game/CMakeLists.txt`
- [x] 1.2 Generalise `WsConfigReadBool` into a string reader shared by bool, enum and int options; implement `WsSettingsLoad()` with precedence env > `config.local.ini` > `config.ini` > default, and call it from `GameAfterConfig`
- [x] 1.3 Read `[Graphics] Widescreen`, `Fullscreen` and `DisplayAspect` from `g_config`; `SNESRECOMP_WS` still overrides widescreen
- [x] 1.4 Move readers to `WsSettingGet`: `WsWideEnabled`, `WsTargetAspect` and the 512 cap in `main.c` (now `MaxWidth`, clamped to [342, 512]), `ws_hud.c`, `ws_cull.c`, `ws_radar.c`, `uniracers_save.c`
- [x] 1.5 Make `WsOffwindowMode` read the setting instead of its `static` cache, keep `ghost` env-only and the `SNESRECOMP_WS_TRACK_CLAMP` legacy rule, and add `WsSettingsOnChange` that calls `WsOwReset()` when the margin changes
- [x] 1.6 Check that every env-only run from before still behaves the same: `SNESRECOMP_WS=1`, `SNESRECOMP_WS_ASPECT=21:9`, `SNESRECOMP_WS_OFFWINDOW=clamp`, `SNESRECOMP_WS_HUD=0`, `SNESRECOMP_WS_MINIMAP=0`, `SNESRECOMP_UNLOCK_TOURS=1`

## 2. Config writer

- [x] 2.1 Implement `WsSettingsSave()`: in-place update of `key = value` lines, append missing keys at the end of their section, create `[Widescreen]` at the end when missing, write to `config.ini.tmp` and rename
- [x] 2.2 Skip options whose source is `env` or `local`, and the host-owned `[Graphics]` keys
- [x] 2.3 Round-trip test (script under `tools/scripts/`): a `config.ini` with comments, `[KeyMap]`, an unknown key and no `[Widescreen]`; after saving one change, only the expected lines differ
- [x] 2.4 Check that a later host `WriteConfigFile` (fullscreen hotkey, volume) keeps the `[Widescreen]` section

## 3. Host patch

- [x] 3.1 Add the optional `SnesTitleMenu` block to `SnesDesktopHostGame` in `runner/src/desktop/host_main.h`
- [x] 3.2 In `host_main.c`: check `open_key` before the keymap, and `poll_open` on the same pad word as the F11 gesture; skip both during netplay and while the save-state browser is open
- [x] 3.3 Add the modal loop for the title menu (freeze, pause audio, `PumpOverlayEvents` with the menu's `handle_key`, `poll_nav`, present), the guest input filter, and the reverse exclusion (no F11 browser while the title menu is open)
- [x] 3.4 Make `PresentFrozenWithOverlay` draw the title menu's panel
- [x] 3.5 Add `snesrecomp_desktop_set_display(fullscreen, display_aspect, persist)` on top of `ApplyLiveSettings`, keeping `g_win_flags` and the cursor in step like the F hotkey
- [x] 3.6 Regenerate `game/patches-snesrecomp-local.diff` with the new hunks, and check that it applies cleanly to a fresh submodule checkout
- [x] 3.7 Check that a title without `title_menu` behaves as before: F10 loads slot 10, F11 unchanged

## 4. Options menu

- [x] 4.1 Create `game/src/ws_menu.c`/`.h`: state (open, selected row, repeat timers), the `SnesTitleMenu` callbacks, and registration in `kGameHost` with F10 and Select+L
- [x] 4.2 Draw the 512×448 panel with `snes_ovl_*`: title, rows with `<` `>` values, `[ENV]`/`[LOCAL]` marks, *Guardar en config.ini* / *Cerrar*, unsaved mark, footer; uppercase unaccented labels
- [x] 4.3 Navigation: up/down between rows, left/right change the value (cycling enums, presets plus current value for `MaxWidth`, file-only `Aspect` values shown as is), A/Enter activates, B/Escape/F10 closes, auto-repeat like the browser
- [x] 4.4 Apply: game options through the settings model (`WsSettingsOnChange`); widescreen through `g_config.widescreen`; fullscreen and pixel aspect through `snesrecomp_desktop_set_display(…, 0)`
- [x] 4.5 Save: `WsSettingsSave()`, `snesrecomp_desktop_set_widescreen()` and `snesrecomp_desktop_set_display(…, 1)`; clear the unsaved mark
- [x] 4.6 Footer texts: locked (name the variable or file), `next_race`, `restart`, and the unlock-tours permanence note; one-line help that tells `Proporción` and `Forma del pixel` apart
- [x] 4.7 Add the audio volume row (`[Sound] Volume`, 0..100 in steps of 10) and apply it through the host's volume setter

## 5. Validation

- [x] 5.1 Open the menu in a race, in the game's menus and on the title screen, with F10 and Select+L; the guest stays frozen, audio paused, and no input reaches the game (A on a pause-menu *Quit* does nothing)
- [x] 5.2 Mid-race live switches: widescreen, aspect, maximum width, each margin policy, HUD, cull (check `$0421/$0423` and objects at the edges), minimap; no artifacts after the menu closes
- [x] 5.3 Fullscreen windowed → borderless → exclusive → windowed and the three pixel aspects with the menu open; the panel stays usable and the window size follows
- [x] 5.4 Unlock tours from the menu on a save with `$10D3 < 3`; `srm.py verify` passes afterwards
- [x] 5.5 Locked rows with `SNESRECOMP_WS_MINIMAP=0` and with a `config.local.ini` key; *Save* leaves those keys untouched
- [x] 5.6 Save, quit, relaunch: every saved value comes back
- [x] 5.7 Netplay: the menu does not open

## 6. Docs

- [x] 6.1 `README.md`: the menu, F10/Select+L, F10 no longer loading slot 10 (slot 10 stays reachable from F11), the `[Widescreen]` section and the precedence rule
- [x] 6.2 `docs/UNIRACERS_MEJORAS.md`: mark A6 and A6b done and point to this change
