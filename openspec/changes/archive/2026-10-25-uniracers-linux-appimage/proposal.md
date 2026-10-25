## Why

The Windows build now has a playable zip with the `.exe`, its launcher assets and the licenses. Linux has no equivalent single-file artifact: a player either builds from source, uses a setup pack that still needs a toolchain, or runs the executable from a folder that must keep `assets/` beside it.

An AppImage fits this port well. The framework already prefers `$APPIMAGE` over `/proc/self/exe` for config.ini, keybinds.ini, rom.cfg and saves (`host_paths.c`, `keybinds.c`), so state lands beside the user-visible AppImage rather than inside the read-only squashfs mount. The launcher loads its fonts and art through `SDL_GetBasePath()`, which resolves to `usr/bin/` inside that mount, so **the assets can be packaged inside the AppImage**.

The missing piece is the packaging workflow and the reproducible portable build, not a game feature.

## What Changes

- Add `game/tools/package-appimage.sh`: build a portable Linux binary, stage an AppDir, package it with a pinned `appimagetool`, and write `dist/redcomp-dream-<version>-x86_64.AppImage`.
- Build the portable binary with the retcomm Linux toolchain pack (`fetch_toolchain.sh --artifact linux-x64`, glibc 2.35 baseline) instead of the host's Homebrew libraries. SDL3 stays static; no build-host library is required at runtime beyond the system OpenGL/display stack.
- Put the launcher assets inside the AppImage at `usr/bin/assets`, where `SDL_GetBasePath()` finds them. The image carries no `assets/` folder beside it and no `config.ini`, `saves/`, ROM, source tree or toolchain.
- Keep mutable state beside the `.AppImage` file: `config.ini`, `keybinds.ini`, `rom.cfg`, `saves/` and the runtime `mods/` directory. The existing `$APPIMAGE` path policy does this; the script verifies it.
- Add an `AppRun`, `.desktop` entry and icon so the AppImage is a normal executable (and desktop-integratable); document the FUSE/libfuse2 requirement and the `--appimage-extract-and-run` fallback.
- Document `--rom` for ROM selection when the desktop has no `zenity`/`kdialog` for recomp-ui's native picker.
- Optionally accept a custom icon PNG and a custom AppImage version; include the project/font/image licenses inside the image.
- Rename the executable target/binary to `redcomp-dream` on Linux and `redcomp-dream.exe` on Windows, keep the codegen target and packaging scripts aligned, and embed the brand mark as the Windows executable icon. The in-game/launcher titles stay `Uniracers`.

## Capabilities

### New Capabilities

- `recomp-linux-appimage`: a single-file playable Linux AppImage, the location of its embedded launcher assets, the writable state beside the AppImage, the portable build baseline, and the packaging/smoke-test contract.
- `recomp-executable-identity`: the `redcomp-dream`/`redcomp-dream.exe` binary name, the CMake/codegen target alignment, and the Windows icon generated from the brand mark.

### Modified Capabilities

<!-- None: the game and launcher requirements do not change. -->

## Impact

- New `game/tools/package-appimage.sh`; optionally a portable mode/option in `game/tools/build.sh`.
- `README.md` and `game/README.md`: AppImage build/run instructions, FUSE, writable state, `--rom`.
- `game/.gitignore` / `.gitignore`: toolchain and `appimagetool` caches under `game/.cache/`; `build-appimage/` and `dist/` already match the build rules.
- New build/runtime dependencies: the retcomm Linux toolchain pack and `appimagetool` (downloaded to the cache; not committed), FUSE/libfuse2 for mounting the AppImage, and the system OpenGL/display libraries.
- No guest-state change, no framework patch, no ROM data. Mods are not embedded in the image; the runtime creates/uses `mods/` beside the AppImage.
- The executable rename reaches `game/CMakeLists.txt`, `run.sh`, `game/tools/build.sh`, `game/tools/validate.sh`, `game/scripts/package_release.sh`, the Windows/AppImage package scripts and the docs; the Windows icon adds `game/resources/redcomp-dream.{ico,rc.in}`.
- Distribution policy: the repository's `package_release.sh` describes the official release as a setup pack that "does not contain the game", while this AppImage is a playable binary compiled from ROM-derived C. Publishing it as an official artifact is a policy/legal decision, not a packaging detail; the README currently only says that no ROM data is distributed and that the recompiled C is generated locally. This change can produce the AppImage, but it does not by itself change that policy.
