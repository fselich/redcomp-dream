## 1. Portable Linux build

- [x] 1.1 Add `game/tools/package-appimage.sh` with `--build`, `--fetch-toolchain`, `--toolchain-dir`, `--out-dir`, `--version`, `--icon`, `--no-build` and `--keep-appdir`, following the style of `package-windows.sh`.
- [x] 1.2 Fetch the pinned Linux `cmake-clang-v1` pack with `snesrecomp/tools/ci/fetch_toolchain.sh --artifact linux-x64`, source its `env.sh`, and configure/build `build-appimage` with its CMake/Ninja, `CMAKE_SYSROOT`, `SNESRECOMP_SETUP_HOST=OFF` and static SDL3.
- [x] 1.3 Fail the build if `ldd` names a path under the checkout, Homebrew or another build-host prefix; otherwise copy the remaining non-system libraries into `AppDir/usr/lib` and expose them from `AppRun`.

## 2. AppDir contents

- [x] 2.1 Stage `AppDir/usr/bin/redcomp-dream` and copy the built `assets/` tree to `AppDir/usr/bin/assets` so `SDL_GetBasePath()` finds fonts, box art, brand, pad and icons inside the image.
- [x] 2.2 Add `AppRun` that execs `usr/bin/redcomp-dream` without changing directory, a `uniracers.desktop` entry, and a 256×256 icon at `usr/share/icons/hicolor/256x256/apps/uniracers.png` plus `.DirIcon`; support `--icon`.
- [x] 2.3 Add `README.txt`, `VERSION`, `BUILD_INFO.txt` and the project/framework/recomp-ui/font/image licenses under `usr/share/doc/uniracers/`; verify the AppDir has no `config.ini`, `saves/`, ROM, `src/gen`, `recomp/funcs.h`, source tree or toolchain.

## 3. AppImage packaging

- [x] 3.1 Download `appimagetool-x86_64.AppImage` into `game/.cache/appimagetool/`, verify a pinned SHA-256, and run it with `--appimage-extract-and-run --no-appstream`.
- [x] 3.2 Write `game/dist/redcomp-dream-<version>-x86_64.AppImage` and a `.sha256`, make it executable, and clean the staging tree unless `--keep-appdir` was given.

## 4. Runtime and smoke test

- [x] 4.1 Run the AppImage from a clean writable folder with `--appimage-extract-and-run`; confirm the launcher shows the packaged box art/brand/fonts and that no `assets/` folder is needed beside it.
- [x] 4.2 Confirm `config.ini`, `keybinds.ini` and `saves/` are created beside the `.AppImage`, never inside the mount; confirm `--rom` boots a race and F10 opens the in-game menu.
- [x] 4.3 Use `LNG_SCRIPT` to open the launcher, take a screenshot and quit; the process must exit 0 and the screenshot must show the packaged art.
- [x] 4.4 Extract/inspect the AppImage and confirm it contains no ROM, `src/gen`, `recomp/funcs.h`, `config.ini`, `saves/`, source tree or toolchain.

## 5. Documentation and repository hygiene

- [x] 5.1 Document `game/tools/package-appimage.sh`, the output path, the FUSE/libfuse2 requirement, `--appimage-extract-and-run`, the writable-folder rule, `--rom`/drag-and-drop, the glibc 2.35 baseline and the mods-beside-the-AppImage behavior in `README.md` and `game/README.md`.
- [x] 5.2 Add `game/.cache/` (already ignored) and any new `build-appimage/`/`dist/` artifacts to the ignore rules if they are not already covered.

## 6. Verification

- [x] 6.1 Run `openspec validate uniracers-linux-appimage` and confirm all artifacts are valid.
- [x] 6.2 Run `game/tools/validate.sh` and confirm the AppImage packaging change does not alter the game's frame/log references.
- [x] 6.3 Record the AppImage size, SHA-256, `ldd` result and smoke-test screenshot in the change report.

## 7. Executable identity and Windows icon

- [x] 7.1 Rename the CMake project/target and output binary from `UniracersSNESRecomp` to `redcomp-dream`; keep the codegen host's `cmake_target`/`exe_basename` aligned with the target.
- [x] 7.2 Update `run.sh`, `build.sh`, `validate.sh`, `package-windows.sh`, `package-appimage.sh`, `package_release.sh` and the README/VALIDATION paths to `redcomp-dream`/`redcomp-dream.exe`.
- [x] 7.3 Generate `game/resources/redcomp-dream.ico` from `game/launcher_assets/brand_mark.tga`, add the configured `.rc.in`, and embed it on `WIN32`; verify the PE has an ICON resource.
- [x] 7.4 Rebuild Linux, Windows and the AppImage; verify the names, the Windows Explorer icon embedding and the `redcomp-dream-*` package names.
