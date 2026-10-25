# Report — uniracers-linux-appimage

Date: 2026-10-25. Change: `uniracers-linux-appimage` (spec-driven). Host: Ubuntu 24.04 x86_64, glibc 2.39, X11, 12th Gen Intel i7-1280P.

## Portable build

- Toolchain: retcomm `cmake-clang-v1` Linux pack 1.0.14, fetched with `snesrecomp/tools/ci/fetch_toolchain.sh --artifact linux-x64` into `game/.cache/toolchain-linux` (ignored).
- Build: `game/tools/package-appimage.sh --build`; `CMAKE_SYSROOT` from the pack, `SNESRECOMP_SETUP_HOST=OFF`, `CMAKE_DISABLE_FIND_PACKAGE_Freetype=ON` to keep host Homebrew FreeType/HarfBuzz out. SDL3 static from the framework's FetchContent.
- Binary max GLIBC version: 2.34; max GLIBCXX: 3.4.29. `libstdc++.so.6` and `libgcc_s.so.1` from the sysroot are bundled under `usr/lib`; `AppRun` exports `LD_LIBRARY_PATH` to them.
- `ldd` (see `game/build-appimage/ldd.txt`): `libstdc++`/`libgcc` resolve under the AppDir; `libGL`, `libX11`, `libxcb`, `libGLdispatch`/`libGLX` are system libraries. No `/home/linuxbrew`, no build-checkout path.

## AppImage

- Output: `game/dist/redcomp-dream-0.1.0-x86_64.AppImage` (6.1 MB).
- SHA-256: `0b58f164b71648144475254c459c99a67edee2cf021f3b604154102ba7ab2957`.
- `appimagetool` continuous build (git 854e19e), SHA-256 pinned to `95cbe7cce9717fce90c484e34052ee7c7f1d7635b33c12525b4776826a7d29b6`; run with `--appimage-extract-and-run --no-appstream`.
- AppDir layout: `AppRun`, `uniracers.desktop`, `uniracers.png`/.DirIcon, `usr/bin/redcomp-dream`, `usr/bin/assets` (fonts + boxart/brand/pad/verdict/flags), `usr/lib/libstdc++.so.6` + `libgcc_s.so.1`, `usr/share/applications`, icon, and `usr/share/doc/uniracers` with README/VERSION/BUILD_INFO/licenses.
- Integrity: the AppImage was extracted and inspected; it contains no ROM, `.sfc`/`.smc`/`.srm`, `src/gen`, `recomp/funcs.h`, `config.ini`, `keybinds.ini`, `saves/` or source tree.

## Runtime smoke test

- The packaging script copies the AppImage to a clean writable temp folder, runs `redcomp-dream-0.1.0-x86_64.AppImage --appimage-extract-and-run --launcher --rom "rom/Uniracers (USA).sfc"` with `LNG_SCRIPT="wait:60;shot:<tmp>/launcher.png;quit"`, and exits 0.
- The screenshot shows the packaged Redcomp Dream box art, brand and fonts from inside the image; no `assets/` folder exists beside the AppImage.
- `config.ini` and `keybinds.ini` are created beside the AppImage in the temp folder; the launcher anchors state to the `$APPIMAGE` folder.
- A second run with `--no-launcher --rom ...` under `SDL_VIDEODRIVER=dummy`/`SDL_AUDIODRIVER=dummy` and `SNESRECOMP_RUN_FRAMES=60` reaches `exit: RUN_FRAMES reached after 60 frames`; the binary and bundled runtime work inside the image.
- F10 was tested on a live window from the AppImage: the log reports `title options menu OPEN` and the screenshot shows the `OPCIONES` panel with `HUD EXPANDIDO`, confirming the in-game menu path.
- Screenshots: `game/dist/redcomp-dream-0.1.0-launcher.png` (launcher) and `/tmp/opencode/redcomp-f10.png` (F10 during validation).

## Executable identity and Windows icon

- CMake project/target/binary renamed to `redcomp-dream`; Linux output `game/build-local/redcomp-dream`, Windows `game/build-windows/redcomp-dream.exe`.
- The Windows `.exe` embeds an ICON resource (`llvm-readobj --coff-resources` reports type ICON ID 3, six image entries). The icon comes from `game/launcher_assets/brand_mark.tga` via `game/resources/redcomp-dream.ico` and `game/resources/redcomp-dream.rc.in`; `tools/launcher-art.sh --brand` regenerates the ICO.
- Windows playable zip: `game/dist/redcomp-dream-0.1.0-windows-x64.zip` (5.5 MB), SHA-256 `4ac6c910aef8ae5a5af750dae2ef6ff8b3cdee1babdcc1ce99a164f91691f3ab`, containing `redcomp-dream.exe`, assets, mods, README and licenses.
- `game/tools/validate.sh` passes all frame/log cases with the renamed binary.

## Notes

- FUSE/libfuse2 is a runtime requirement for double-click mounting; the smoke test uses `--appimage-extract-and-run` and the README documents the fallback. The AppImage is type 2.
- glibc floor is 2.35 per the toolchain pack (binary currently requires 2.34); older distros need a newer pack or a source build.
- The launcher's native file picker still needs `zenity`/`kdialog`; `--rom` and drag-and-drop are the documented fallbacks. Mods are not embedded; the runtime creates/uses `mods/` beside the AppImage.
- Public distribution of this playable artifact is still the policy question recorded in the proposal/design: the repository's release script describes setup packs, not playable binaries. The game's copyright/derivative-work position is outside this report.
