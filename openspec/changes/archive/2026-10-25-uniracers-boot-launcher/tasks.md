## 1. Dependency and build wiring

- [x] 1.1 Add `recomp-ui` as a git submodule at `game/recomp-ui` and record its commit in `game/framework_pins.txt`.
- [x] 1.2 In `game/CMakeLists.txt`, set `RECOMP_UI_ROOT`, fail configure when `recomp_ui.cmake` is missing, set `RECOMP_UI_ENABLE_MODS` ON, include `recomp_ui.cmake`, and call `recomp_target_launcher_ui(UniracersSNESRecomp CONSOLE snes)`.
- [x] 1.3 Add `${SNESRECOMP_ROOT}/host/snesrecomp_codegen_host.c` to the target and add `${SNESRECOMP_ROOT}/host` and `${RECOMP_UI_ROOT}/src` to its include directories.
- [x] 1.4 Configure and build; confirm the binary links with `RECOMP_LAUNCHER`, the codegen host is compiled, and `assets/fonts` plus the launcher images are staged beside the executable.

## 2. Host flags and menu boundary

- [x] 2.1 In `game/src/main.c`, set `display_aspect_supported = 1` and `widescreen_supported = 0`; leave `in_game_launcher`, `rewind_settings`, `shader_supported` and `msu1_supported` at 0.
- [x] 2.2 Confirm `game/src/ws_menu.c`, `game/src/ws_menu.h` and the F10 title-menu patch are unchanged, and that no `OpenLauncher` binding is added.
- [x] 2.3 Confirm the launcher shows no Widescreen row and offers the CRT 4:3 / 8:7 / 1:1 pixel-aspect row.

## 3. Boot flow and settings

- [x] 3.1 Launch without `--no-launcher` and verify the launcher opens before the guest; select the repository ROM and confirm PLAY boots the game.
- [x] 3.2 Verify a wrong dump is refused with the expected SHA-256 and CRC32, a `.smc` copy with a matching payload is accepted, and a launch with no ROM opens on the picker.
- [x] 3.3 Change window scale, fullscreen, pixel aspect and volume in the launcher; confirm each is applied at boot and written to `config.ini`.
- [x] 3.4 Change linear filtering, frame blend, run-ahead, vsync and renderer; confirm each takes effect on the next boot.
- [x] 3.5 Rebind a key and a gamepad button, set a player source and the deadzone; confirm the booted game uses them.
- [x] 3.6 Open the SAVES panel and verify import/clear target `saves/save.srm`; open the Mods page and confirm the runtime provider's packages are listed.
- [x] 3.7 After a launcher edit to fullscreen, pixel aspect and volume, open F10 and confirm its rows show the new values.

## 4. Setup pack and Generate & rebuild

- [x] 4.1 Build with `-DSNESRECOMP_SETUP_HOST=ON` and an empty `src/gen/`; confirm the launcher opens the Generate & rebuild wizard as its only path and PLAY cannot boot the guest.
- [x] 4.2 Run `game/scripts/package_release.sh` on the setup-host build; confirm `assets/fonts` is packaged and the zip is produced.
- [x] 4.3 Inspect the zip: it contains the source tree and the `recomp-ui` submodule files, and no ROM, `src/gen/*.c` or `recomp/funcs.h`.
- [x] 4.4 On a desktop with a ROM and the toolchain, complete the wizard (ROM → generate → rebuild → relaunch) and confirm the rebuilt game starts.

## 5. Developer and headless paths

- [x] 5.1 Add a `--launcher` path to `run.sh` that omits `--no-launcher` and still passes the repository ROM; confirm the GUI opens and the default `./run.sh` still launches without it.
- [x] 5.2 Confirm `--launcher` together with `--no-launcher` is still a usage error when passed directly to the executable.
- [x] 5.3 Confirm `SDL_VIDEODRIVER=dummy`, `--script`, `--framedump`, start-paused runs and `--no-launcher` never create a launcher window.

## 6. Documentation and pins

- [x] 6.1 Update `README.md` with `./run.sh --launcher`, the first-run ROM flow, and the note that F10 remains the in-game menu.
- [x] 6.2 Update `game/README.md` (remove the `--no-recomp-ui` paragraph, add the launcher and recomp-ui to the layout).
- [x] 6.3 Update `docs/ARCHITECTURE.md` with the recomp-ui dependency and the boot/ingame split.
- [x] 6.4 Update `docs/CONFIG.md` with the settings ownership table and the launcher/F10 boundary.
- [x] 6.5 Run `snesrecomp/tools/ci/record_pins.sh --check framework_pins.txt` from `game/` and confirm `recomp-ui` is reported as `ok` (the vendored framework entries are expected to be "not a git checkout").

## 7. Validation gate

- [x] 7.1 Run `game/tools/validate.sh` and confirm every frame and log hash matches `game/tools/validate_refs.txt`.
- [x] 7.2 Run a desktop launcher smoke test: open, change a setting, PLAY, open F10, return to the game.
- [x] 7.3 Check `git status` and the build tree for accidental ROM, generated C, `funcs.h` or launcher cache files.

## 8. Defaults and labels

- [x] 8.1 Boot widescreen on when neither config file has `[Graphics] Widescreen` and `SNESRECOMP_WS` is unset; keep an explicit `0` and a `config.local.ini` value authoritative.
- [x] 8.2 Rename the F10 row `MARCADOR A LOS LADOS` to `HUD EXPANDIDO` without changing the `[Widescreen] Hud` key.
- [x] 8.3 Update `README.md`, `docs/CONFIG.md` and `docs/WIDESCREEN.md` for the new default and label.
- [x] 8.4 Rebuild and run the validation gate.

## 9. Launcher art helper

- [x] 9.1 Add `game/tools/launcher-art.sh` with `--boxart`, `--brand`, `--pad`, fit/fill, `--key-black`, `--trim`, per-slot sizes, `--clear` and an optional build.
- [x] 9.2 Point the CMake launcher call at `game/launcher_assets/{boxart,brand_mark,pad}.tga` and document the helper.
- [x] 9.3 Test conversion, TGA orientation, staging and the launcher with a real image.

## 10. Window title

- [x] 10.1 Set `kGameHost.launcher_title = "Uniracers - Launcher"` so the X11 window title does not carry the framework's UTF-8 em dash.
- [x] 10.2 Rebuild and confirm `WM_NAME` and `_NET_WM_NAME` are the ASCII title.

## 11. Build helper

- [x] 11.1 Add `game/tools/build.sh` for `linux`, `windows` and `both`, with debug/clean/jobs/build-dir options.
- [x] 11.2 Test the Linux build into `build-local`.
- [x] 11.3 Test the Windows cross-build with a fetched llvm-mingw and inspect the resulting `UniracersSNESRecomp.exe` (PE32+, no MinGW/SDL DLL imports).

## 12. Playable build vs setup host

- [x] 12.1 Guard the codegen host's `sources_missing` setup trigger with `!SNESRECOMP_HAS_GENERATED_CODE`; keep `SNESRECOMP_SETUP_HOST` and `SNESRECOMP_FORCE_SETUP` authoritative.
- [x] 12.2 Rebuild Linux and Windows and verify a playable build in a source-like folder without `src/gen` opens the normal launcher.

## 13. Playable Windows package

- [x] 13.1 Add `game/tools/package-windows.sh` (`--build`, `--fetch-toolchain`, build/out dirs, version).
- [x] 13.2 Package `UniracersSNESRecomp.exe`, `assets/`, `mods/`, README, VERSION, BUILD_INFO and licenses; exclude config.ini, saves, ROM, src/gen and funcs.h.
- [x] 13.3 Inspect the resulting zip and document the command.
