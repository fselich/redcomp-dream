## Context

The Windows playable package is `redcomp-dream.exe` plus `assets/`, `mods/` when present, a README and the licenses. Linux has no equivalent single-file artifact; `game/scripts/package_release.sh` builds a **setup pack** that still needs a toolchain and a rebuild on the player's machine.

The framework already anticipates AppImages:

- `runner/src/host_paths.c` resolves the executable via `$APPIMAGE` on Linux, so config.ini, keybinds.ini, rom.cfg and saves are anchored beside the user-visible `.AppImage`, not inside the read-only squashfs mount.
- `runner/src/keybinds.c` uses the same `$APPIMAGE` policy.
- `runner/src/desktop/host_main.c` documents that this is what makes an AppImage work; `desktop/glsl_shader.c` resolves bundled shaders against a fresh mount.
- recomp-ui's `asset()` uses `SDL_GetBasePath()`, which inside an AppImage is `.../usr/bin/`. Assets placed in `usr/bin/assets` are therefore found inside the image.

So the hard part is not the game: it is a reproducible portable build and a correct AppDir.

## Goals / Non-Goals

**Goals:**

- One `redcomp-dream-<version>-x86_64.AppImage` that runs the game and the recomp-ui launcher with no folder beside it.
- The launcher assets (fonts, box art, brand, pad) inside the image, at `usr/bin/assets`.
- Writable state (`config.ini`, `keybinds.ini`, `rom.cfg`, `saves/`, `mods/`) beside the `.AppImage` file, using the existing `$APPIMAGE` policy.
- A portable binary built against the project's Linux toolchain pack (glibc 2.35 baseline), with no build-host Homebrew library.
- A repeatable packaging script, smoke test, desktop entry and icon.
- No ROM, source tree, generated C, toolchain or setup wizard inside the image.

**Non-Goals:**

- Building a Windows AppImage or changing the Windows zip.
- Bundling a native file picker; recomp-ui's `zenity`/`kdialog` dependency stays, with `--rom`/drag-and-drop as the fallback.
- Embedding mods; the runtime uses a writable `mods/` beside the AppImage.
- Changing game/launcher behavior or guest state.
- AppImage updates, signing, AppStream metadata or desktop integration beyond a valid `.desktop`/icon.

## Decisions

### D1. Portable build with the retcomm Linux toolchain pack

Use `snesrecomp/tools/ci/fetch_toolchain.sh --artifact linux-x64` to fetch the pinned `cmake-clang-v1` pack into `game/.cache/`, source its `env.sh`, and build `build-appimage` with the pack's CMake/Ninja and `CMAKE_SYSROOT`. This is the same glibc 2.35/sysroot baseline the release CI uses. SDL3 comes from the framework's static FetchContent build.

*Alternative:* build with the host compiler and bundle Homebrew libraries with `linuxdeploy`. Rejected as the primary path: the current binary links `/home/linuxbrew` SDL3/FreeType/HarfBuzz and has no `$ORIGIN` RUNPATH; making that portable is more fragile than the project's own release pack.

*Fallback:* if the pack build still links a non-system library, copy it into `AppDir/usr/lib` and add `LD_LIBRARY_PATH` in `AppRun` (or run `linuxdeploy` on the AppDir). The packaging script fails if `ldd` names a build-host path.

### D2. AppDir layout: assets beside the binary, state beside the AppImage

```
AppDir/
  AppRun
  uniracers.desktop
  uniracers.png
  usr/bin/redcomp-dream
  usr/bin/assets/fonts|img/...     <- SDL_GetBasePath() finds these
  usr/share/doc/uniracers/         <- README, licenses
```

`asset("assets/...")` becomes `$APPDIR/usr/bin/assets/...`, read-only inside the mount. `$APPIMAGE` remains the user-visible file, so `snesrecomp_anchor_to_exe_dir()` writes `config.ini`/`saves/` next to it when that folder is writable. `AppRun` only `exec`s the binary; it must not `cd` into the mount.

*Alternative:* put assets in `usr/share/uniracers/assets` and export an asset directory. Rejected: recomp-ui has no asset-root environment variable, and `SDL_GetBasePath` is already correct.

### D3. AppRun and desktop entry

```sh
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/bin/redcomp-dream" "$@"
```

`.desktop`: `Type=Application`, `Name=Uniracers`, `Exec=redcomp-dream`, `Icon=uniracers`, `Categories=Game;`, `Terminal=false`. The icon is a 256×256 PNG at `usr/share/icons/hicolor/256x256/apps/uniracers.png` (and `.DirIcon`). `--icon PATH` overrides it; without one, use `game/launcher_assets/appicon.png` if present, else a simple generated placeholder.

### D4. `appimagetool` is fetched and pinned, never assumed installed

Download `appimagetool-x86_64.AppImage` from the AppImage project into `game/.cache/appimagetool/`, verify a pinned SHA-256, and run it with `--appimage-extract-and-run --no-appstream`. The tool is itself an AppImage; extract-and-run removes the need for FUSE on the build machine. The produced AppImage is type 2; players need FUSE/libfuse2 or use `--appimage-extract-and-run`.

*Alternative:* `linuxdeploy` for both bundling and packaging. Kept only as a fallback for library bundling; a portable toolchain build makes its deployment plugin unnecessary.

### D5. No mods in the image; a writable `mods/` beside it

The mod runtime resolves its catalog root through `snesrecomp_exe_dir_path`, which prefers `$APPIMAGE`; `mods/` therefore belongs beside the AppImage, not inside the read-only mount. The image carries no mods. The launcher can create the directory and install `.snesmod` packages there when the AppImage lives in a writable folder. If the image sits in a read-only location, config, saves and mods cannot be written; the README says to keep the AppImage in a writable folder.

### D6. ROM selection

The launcher's native picker uses tinyfiledialogs (`zenity`/`kdialog` on Linux). The AppImage does not bundle a picker. Players can drag a ROM onto the AppImage or pass `--rom PATH`; the README documents both. The launcher remains the first-run surface when a picker is available.

### D7. Portable contents and verification

The image contains only the Release executable, `assets/` (fonts, boxart, brand, pad, verdict icons, flags), `AppRun`, `.desktop`, icon, `README.txt`, `VERSION`, build pins and the project/framework/recomp-ui/font licenses. It contains no ROM, `src/gen`, `recomp/funcs.h`, `config.ini`, `saves/`, source tree or toolchain. The packaging script verifies this and runs the AppImage once with `--appimage-extract-and-run` under the current display, using `LNG_SCRIPT` to open, take a screenshot and quit; the screenshot must show the launcher art from inside the image. It also checks `ldd` for build-host paths and confirms `config.ini`/`keybinds.ini` appear beside the `.AppImage`, not in the mount.

### D8. Package script surface

`game/tools/package-appimage.sh`:

```
--build                 build the portable Linux binary first
--fetch-toolchain       fetch the Linux cmake-clang pack if absent
--toolchain-dir DIR     use an existing pack
--out-dir DIR           default game/dist
--version V             default game/VERSION
--icon PNG              desktop/AppImage icon
--no-build              package an existing build-appimage (default)
--keep-appdir           keep the staging tree for inspection
```

Output: `game/dist/redcomp-dream-<version>-x86_64.AppImage` and `.sha256`.

### D9. Executable identity and Windows icon

The CMake target and output binary are renamed from `UniracersSNESRecomp` to `redcomp-dream`. The hyphenated target is valid in CMake, and using the executable basename as the target name keeps the codegen host aligned: `snesrecomp_codegen_host_autowire()` derives both `cmake_target` and `exe_basename` from the running executable, so a setup-host rebuild runs `cmake --build --target redcomp-dream` and looks for `build/redcomp-dream`. Linux gets `redcomp-dream`; Windows gets `redcomp-dream.exe`. `run.sh`, `build.sh`, `validate.sh`, `package-windows.sh`, `package-appimage.sh` and `package_release.sh` use the same name. The AppImage and Windows zip filenames become `redcomp-dream-<version>-*`.

The Windows executable icon comes from `game/launcher_assets/brand_mark.tga` (the wheel/brand mark). `tools/launcher-art.sh --brand ...` regenerates `game/resources/redcomp-dream.ico` with 256/128/64/48/32/16 sizes. A configured `redcomp-dream.rc.in` embeds resource ID 1 through `windres` on `WIN32`; CMake uses an absolute icon path because the RC compiler runs from the build directory. The icon is the Explorer file icon; the SDL window icon is not changed by this resource and would be a separate SDL call. The in-game title, launcher header and `.desktop` display name stay `Uniracers`; only the file/executable identity is Redcomp Dream.

The setup-pack release script keeps its `uniracers-<version>-<platform>.zip` prefix (it is a different, source-based artifact), but its `--exe-name` is `redcomp-dream` so the setup host and rebuilt game match. The `redcomp-dream.ico` is a generated binary; if it is absent the CMake block skips the icon and the build still succeeds.

## Risks / Trade-offs

- **FUSE/libfuse2 is not universal** → document `--appimage-extract-and-run`; the packaging smoke test uses it so CI does not depend on FUSE.
- **glibc floor is 2.35** → inherit the release pack's baseline; say so in the README. Older distros need a newer pack or a source build.
- **The AppImage folder may be read-only** → config/saves/mods fall back to cwd or fail; recommend `~/Applications` or another writable folder.
- **No zenity/kdialog** → no Browse dialog; `--rom`/drag-and-drop is the fallback. Bundling a picker is out of scope.
- **A non-system library sneaks in** → `ldd` check plus `usr/lib` copy/`AppRun` `LD_LIBRARY_PATH` fallback; fail the package if unresolved.
- **AppImageTool is a moving `continuous` asset** → pin its SHA-256 and fail closed if the download does not match.
- **Mods in a read-only mount** → not embedded; writable `mods/` beside the AppImage, documented.
- **Wayland/OpenGL differences** → use the system OpenGL/display stack; test under X11/XWayland on the build machine. No bundled Mesa.
- **Asset licensing** → include the existing font/image notices and licenses inside `usr/share/doc/uniracers/`.
- **Windows icon caching** → Explorer can cache an old icon for the same path; a rename (`redcomp-dream.exe`) and a fresh build avoid most of it.
- **Icon art** → the `.ico` is generated from the user-supplied brand mark; if the mark changes, rerun `tools/launcher-art.sh --brand ...` before building Windows.

## Migration Plan

1. Build the portable binary with the Linux toolchain pack and verify `ldd`.
2. Stage the AppDir and package a first AppImage.
3. Run the smoke test; confirm assets from inside the image, state beside the file, and `--rom` boot.
4. Document the command, FUSE note, writable-state rule and mods behavior.
5. Keep the Windows zip script untouched. Rollback is deleting the AppImage script/docs; no game or framework change is involved.

## Open Questions

- Should the AppImage be produced by CI/release automation as well, or only by the local script for now?
- Do we want to bundle `zenity` or a small picker later, so first-run works on minimal desktops without `zenity`/`kdialog`?
- Should mods eventually be seeded beside the AppImage by a first-run helper, or stay a manual/launcher-created directory?
- Public distribution: the repository's release script describes the official artifact as a setup pack (`package_release.sh`: "The zip does not contain the game"), while this AppImage is a playable binary compiled from ROM-derived C. Do we publish it as an official artifact, keep it personal/private, or change the release policy explicitly? The game's copyright/derivative-work position is a separate decision and is not settled by this design.
