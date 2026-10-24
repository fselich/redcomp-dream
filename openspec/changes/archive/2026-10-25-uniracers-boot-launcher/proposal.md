## Why

The port has no graphical boot path. A player who double-clicks the executable
gets the framework's native file picker, and every generic setting (renderer,
filters, key bindings, saves, mods) must be edited in `config.ini` or reached
through a hotkey. The in-game F10 menu was built for the port's own options and
was deliberately not made the setup surface.

The release path is also blocked: `scripts/package_release.sh` calls
`stage_setup_host.sh`, which requires `assets/fonts` staged by
`recomp_ui.cmake`, and the setup host's only reachable path is recomp-ui's
Generate & rebuild wizard. Neither exists in this tree today.

This change adds the framework's shared pre-boot launcher and keeps the in-game
F10 menu exactly as it is.

## What Changes

- **Vendor recomp-ui as a submodule.** Pin it beside the framework pin and wire
  it into `UniracersSNESRecomp` with the `snes` profile. It brings its own
  Dear ImGui, native file pickers and staged assets, so the launcher opens with
  no per-title UI code.
- **Boot through the launcher.** ROM selection and verification, display,
  audio, controller bindings, save management, mods and Generate & rebuild
  become available before the guest starts.
- **Keep F10 as the only in-game menu.** No `in_game_launcher`, no
  `OpenLauncher` binding, no changes to `game/src/ws_menu.c`. The port's
  widescreen, aspect, margin, HUD, cull, radar and unlock options stay in F10.
- **Split settings ownership.** The launcher owns the generic host keys
  (`[Graphics]`, `[Sound]`, `[KeyMap]`, `[GamepadMap]`); F10 keeps
  `[Widescreen]`, `[Video] MiniMap` and `[Emulation] UnlockAllTours`. The
  launcher's generic Widescreen row is hidden because the port's widescreen
  model has different semantics. The launcher exposes the pixel-aspect row,
  which the port already supports.
- **Keep the two surfaces consistent.** After a launcher edit, the F10 menu
  shows the current fullscreen, pixel-aspect and volume values.
- **Boot widescreen on a fresh configuration.** The host's default config has
  no `[Graphics] Widescreen` key; the port treats that as on. An explicit `0`
  or `SNESRECOMP_WS=0` still turns it off. The F10 wide-HUD row is renamed
  from `MARCADOR A LOS LADOS` to `HUD EXPANDIDO`, which says what it does.
- **Launcher art helper.** `game/tools/launcher-art.sh` turns a PNG/JPG into
  the launcher's 32-bit TGA for the box art, brand mark or pad, stages it and
  rebuilds. `game/launcher_assets/` holds the optional results.
- **ASCII launcher window title.** The framework's default title uses a UTF-8
  em dash; SDL3's X11 backend put the bytes in `WM_NAME` without
  `_NET_WM_NAME`, which some window managers render as mojibake. The port sets
  `launcher_title = "Uniracers - Launcher"`.
- **Build helper.** `game/tools/build.sh` builds Linux, a Windows `.exe`, or
  both. Windows cross-build uses an installed MinGW-w64 or a pinned
  llvm-mingw fetched into `game/.cache/`.
- **Playable builds never force setup.** A binary compiled with generated code
  boots normally even when its source tree or `src/gen` is not beside it. Only
  a setup host or `SNESRECOMP_FORCE_SETUP` shows the first-run wizard, so a
  player who copies the `.exe` to another folder is not asked to install a
  compiler.
- **Playable Windows package.** `game/tools/package-windows.sh` zips the
  playable `.exe` with the launcher `assets/`, `mods/`, a README and the
  licenses. It is not a setup pack and contains no source tree or toolchain.
- **Make setup packs buildable.** Add the framework's codegen host source so
  the released setup host can take a player's ROM, generate, rebuild and
  relaunch; the launcher assets stage beside the executable as the packager
  requires.
- **Keep the developer path unchanged.** `game/tools/validate.sh` and
  `run.sh` continue to pass `--no-launcher`; `run.sh` gains an explicit way to
  open the GUI.
- Update the README, configuration, architecture and packaging documentation,
  and record the recomp-ui pin.

## Capabilities

### New Capabilities

- `recomp-boot-launcher`: the pre-boot GUI (ROM selection and verification,
  generic settings, saves, mods, Generate & rebuild), its settings boundary
  with the in-game menu, the headless bypass, and the setup-pack first-run
  flow it enables.

### Modified Capabilities

- `recomp-ws-settings`: names the pre-boot launcher as a writer of the
  host-owned keys and keeps the port's own options out of it.
- `recomp-ingame-options-menu`: states that F10 remains the only in-game menu
  and shows the host values a pre-boot launcher edit leaves behind.

## Impact

- New submodule `recomp-ui` (MIT, ~16 MB with assets) and a pin in
  `game/framework_pins.txt`.
- `game/CMakeLists.txt`: launcher target, assets, codegen host source and
  include paths.
- `game/src/main.c`: host flags (`display_aspect_supported`,
  `widescreen_supported`, no `in_game_launcher`).
- `run.sh`, `game/scripts/package_release.sh`, `game/README.md`,
  `README.md`, `docs/ARCHITECTURE.md`, `docs/CONFIG.md`.
- The build now compiles C++/Dear ImGui and links OpenGL. Headless and scripted
  runs keep using `--no-launcher`, so `game/tools/validate.sh` and the
  frame/log references are unchanged.
- No guest-state writes, no ROM data, and no change to F10 or the framework's
  title-menu patch.
