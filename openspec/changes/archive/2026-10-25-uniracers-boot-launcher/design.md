## Context

The desktop host is `snesrecomp/runner/src/desktop/host_main.c`
(`snesrecomp_desktop_main`). Its pre-boot launcher lives behind
`#ifdef RECOMP_LAUNCHER` and is supplied by the optional
[recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui) submodule; the
framework documents this split in `snesrecomp/docs/LAUNCHER_DESIGN.md`. The
host already resolves a ROM (`--rom`/positional, beside the executable,
`rom.cfg`, then the native picker), seeds the launcher from `g_config`,
commits the player's edits, writes `config.ini`, reloads `[KeyMap]` and
`[GamepadMap]`, and stages the launcher assets from `recomp_ui.cmake`.

This project was scaffolded with `--no-recomp-ui`
(`game/CMakeLists.txt:119`), and the repository-root `run.sh` passes
`--no-launcher`. The
in-game F10 menu is a separate host hook, `SnesTitleMenu`, implemented in
`game/src/ws_menu.c`; its settings model is `game/src/ws_settings.[ch]` and it
owns the port's options. The framework's setup-pack flow
(`snesrecomp/docs/ci/README.md`) assumes recomp-ui: `stage_setup_host.sh`
requires `assets/fonts`, and a setup host's only path is the launcher's
Generate & rebuild wizard.

## Goals / Non-Goals

**Goals:**

- Open the recomp-ui launcher at boot for players: ROM selection and
  verification, generic display/audio/input settings, saves, mods and
  Generate & rebuild.
- Keep F10 and `game/src/ws_menu.c` unchanged; it remains the only in-game
  menu and the only surface for the port's widescreen options.
- Make setup packs buildable again by linking the codegen host and staging the
  launcher assets.
- Keep `--no-launcher`, `game/tools/validate.sh`, scripted runs and frame/log
  references unchanged.
- Pin the new dependency and document the ownership split.

**Non-Goals:**

- The host's in-game launcher (`in_game_launcher`, Ctrl+L / Select+L3).
- Moving the F10 menu to recomp-ui's `recomp_runtime_ui`, or merging the two
  menus.
- Showing the port's `[Widescreen]` model in the launcher.
- Rewind rows, shader rows, MSU-1, netplay, box art or a Spanish translation of
  the launcher. Each is a separate follow-up.
- Any guest-state change or any new ROM-derived data.

## Decisions

### D1. Vendor recomp-ui at `game/recomp-ui` and pin it

Add `recomp-ui` as a git submodule of the game project (`game/recomp-ui`),
beside the vendored `snesrecomp` tree, matching `record_pins.sh` (which
already knows the `recomp-ui` name and path relative to the game project).
Record the commit in `game/framework_pins.txt`. The setup packager copies
`game/`'s tracked files and submodules, so the submodule has to live inside
`game/`, not beside it.

*Alternative:* keep `--no-recomp-ui` and write a native boot panel in
`game/src`. Rejected: it duplicates ROM verification, the native picker, the
config editors and the wizard; it still does not produce the `assets/fonts`
the packager requires, so the release story stays blocked.

*Alternative:* the old in-tree snesrecomp launcher. Rejected: it was removed
from the framework; recomp-ui is the supported path.

### D2. The launcher is boot-only; F10 stays the only in-game menu

Do not set `SnesDesktopHostGame.in_game_launcher`. This keeps `OpenLauncher`
unbound, avoids the host's in-session restart machinery (`--resume-state`,
`resume.sav`) and leaves `ws_menu.c` untouched. The pre-boot launcher and F10
are layers, not alternatives: the first runs before `SnesInit()`, the second
freezes a live guest.

The trade-off is `SkipLauncher=1`: after the player checks it, the launcher is
reachable again with `--launcher` (or by removing the key). This is documented.
If that becomes a support problem, enabling `in_game_launcher` is a separate
change with its own modal-state and restart testing.

*Alternative:* enable `in_game_launcher` from the start. Rejected for this
change: two in-game overlays with overlapping generic settings, and the
in-session restart path has not been tested against this port's settings and
save timers.

*Alternative:* replace F10 with recomp-ui's runtime UI. Rejected: the pre-boot
launcher has no arbitrary title page (the archived
`uniracers-ingame-options` decision, option C), and the SNES host does not use
the runtime UI. That is a separate project.

### D3. Settings ownership

| Keys | Owner |
|---|---|
| `[Graphics]` (fullscreen, display aspect, linear filter, window scale, renderer, vsync, frame blend, run-ahead), `[Sound]`, `[KeyMap]`, `[GamepadMap]` | host / pre-boot launcher |
| `[Widescreen]` (widescreen, aspect, max width, margin, HUD, cull), `[Video] MiniMap`, `[Emulation] UnlockAllTours` | F10 / `ws_settings` |
| `[Graphics] Fullscreen`, `[Graphics] DisplayAspect`, `[Sound] Volume` | both surfaces read the same host fields; F10 shows the post-launcher values |

The launcher's generic **Widescreen** row is hidden by setting
`widescreen_supported = 0`. Two reasons: the host's `LauncherSeed` never
initializes `ls->widescreen` and `LauncherCommit` never copies it to
`g_config.widescreen`, so the row is inert today; and its "16:9 fixed"
semantics are not the port's `Aspect = auto/16:9/21:9/32:9` model. The port's
widescreen controls stay in F10.

Set `display_aspect_supported = 1`. The port already supports the three SNES
pixel aspects through `g_config.display_aspect`, `SnesDisplayAspect_Clamp` and
`PrepareFrame`, and `LauncherCommit` writes the value only when this flag is
set. The F10 row and the launcher row then share the same host field.

Leave `rewind_settings = 0`, `shader_supported = 0`, `msu1_supported = 0` and
`in_game_launcher = 0`. Those rows would be new surface, not a fix. `sram_path`
is already set, so the launcher shows the SAVES panel.

### D4. No extra settings-sync hook

`WsSettingsLoad()` runs in `GameAfterConfig`, before the launcher, so the
title's model initially holds the pre-launcher host values. This is harmless
for boot: the port reads `g_config.widescreen` and `g_config.display_aspect`
directly, the launcher cannot change any `[Widescreen]` or other port-owned
key, and `WsSettingsSyncHost()` refreshes the host-owned rows when F10 opens.
The change therefore adds no `after_launcher` hook and no framework patch.

*Alternative:* add an optional `after_launcher` callback to
`SnesDesktopHostGame`. Rejected for now: it would grow the local framework
patch to solve a stale value that is never read after boot. If a future title
option reads the model before the first F10 open, add the hook then.

### D5. CMake wiring

In `game/CMakeLists.txt`:

- set `RECOMP_UI_ROOT` to `${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui`
  (`game/recomp-ui`) and fail configure if `recomp_ui.cmake` is missing;
- set `RECOMP_UI_ENABLE_MODS` ON before including recomp-ui, because the
  project builds mod packages (`SNESRECOMP_ENABLE_MODS ON`) and the launcher's
  Mods page is separately opt-in;
- include `recomp_ui.cmake` and call
  `recomp_target_launcher_ui(UniracersSNESRecomp CONSOLE snes)`;
- add `${SNESRECOMP_ROOT}/host/snesrecomp_codegen_host.c` and the
  `${SNESRECOMP_ROOT}/host` + `${RECOMP_UI_ROOT}/src` include directories, as
  `setup_project.sh` does for a recomp-ui project.

The CMake call points `BOXART`, `BRAND` and `PAD` at
`game/launcher_assets/*.tga`; recomp-ui guards each with `EXISTS`, so a
checkout without art keeps the default cartridge, brand and SNES pad.
`game/tools/launcher-art.sh` generates those TGAs from a player-supplied image,
flips the rows (ImageMagick TGA output is upside down to stb_image), stages the
result into the build's `assets/img/` and rebuilds. It supports box art, brand,
pad, fit/fill, black-key removal, trim, per-slot sizes and `--clear`.

recomp-ui's `recomp_ui.cmake` maps `SNESRECOMP_SDL_BACKEND` to its own SDL3
build, so the default SDL3 host is used; it also stages `assets/` beside the
executable POST_BUILD. The project already enables CXX, so ImGui adds no new
language.

### D6. ROM selection and verification

Use the host's existing `LauncherSeed`/`LauncherCommit` path:

- `rom_file`, `expected_sha256_hex`, `expected_crc32_hex` already come from
  `rom_identity.txt` and the generated `snesrecomp_rom_identity.h`;
- the launcher opens on `--rom`, a copy beside the executable, or the `rom.cfg`
  cache, and falls back to the native picker;
- the headerless/headered handling and refusal text remain the host's.

`rom_revisions` is left NULL. A wrong dump is still refused with the expected
digests, which is the spec's requirement; naming sibling revisions is a
separate, optional improvement.

### D7. Setup packs

The codegen host source is what turns the launcher into the first-run wizard.
With it linked, `host_main.c` sees `snesrecomp_codegen_host.h` and defines
`SNESRECOMP_HOST_HAS_CODEGEN`; `snesrecomp_codegen_host_autowire()` marks a
setup build as `needs_setup = 1` and provides Generate & rebuild. Because
`recomp_ui.cmake` stages `assets/fonts`, the packager's current hard failure
disappears and `stage_setup_host.sh` can build the zip.

No change to `package_release.sh` is expected: it already passes the runtime
directory and identity metadata. The source tree in the zip includes the new
submodule through `git ls-files --recurse-submodules`.

### D8. Developer path

`run.sh` (repository root) keeps `--no-launcher` by default. It gains a `--launcher` flag
that omits `--no-launcher` while still passing `--rom`, so the GUI opens on the
repository's ROM. Passing both flags to the executable remains a usage error;
the script is responsible for not producing that combination.

`game/tools/validate.sh` and all scripted/framedump runs are unchanged: the
host skips the launcher for `SDL_VIDEODRIVER=dummy`, `--script`, `--framedump`,
start-paused runs and `--no-launcher`.

### D10. Fresh-config widescreen and the HUD label

The host's default `config.ini` text has no `[Graphics] Widescreen` key, so
`g_config.widescreen` is false on a fresh install. `WsSettingsLoad()` treats
"no key in either config file and no `SNESRECOMP_WS`" as widescreen on; an
explicit `0` (in `config.ini`, `config.local.ini` or the environment) still
wins. This is title-owned: no framework default changes for other ports and no
extra patch. The settings descriptor's default becomes `1` so the model and
the docs agree.

The F10 row `MARCADOR A LOS LADOS` becomes `HUD EXPANDIDO`. The feature
expands the BG3 top band (RACE / lap counter / timer) to the wide frame while
the lower prompt rows keep the authentic clamp; the new label describes that
without changing the `[Widescreen] Hud` key. The cull row (`RIVAL EN
MARGENES`) keeps its name: it makes the rival/rider visible in the extra
margin instead of the game culling it at the native 256-px edge.

### D11. Build helper and Windows cross-build

`game/tools/build.sh` wraps the configure/build steps for Linux
(`build-local`), Windows (`build-windows`) or both. The Windows path needs a
MinGW-w64 toolchain; when none is installed, `--fetch-toolchain` downloads a
pinned llvm-mingw Linux release into `game/.cache/` (ignored) and writes a
CMake toolchain file that points `CMAKE_FIND_ROOT_PATH` at its sysroot. The
build passes `SNESRECOMP_STATIC_RUNTIME=ON` and fetches SDL3 for the target, so
the `.exe` has no MinGW/SDL DLL dependencies beyond Windows system libraries.
`--clean` runs the CMake clean target instead of deleting the directory, so a
local `config.ini` and `saves/` survive.

`game/tools/package-windows.sh` turns `build-windows` into a playable zip:
`UniracersSNESRecomp.exe`, `assets/` (the launcher loads fonts and art from
there), `mods/` when present, `README.txt`, `VERSION`, build pins and the
licenses. It deliberately excludes `config.ini`, `saves/`, `src/gen`, the ROM
and the source tree; a setup pack remains a separate artifact.

### D12. Playable builds and the first-run wizard

The codegen host turns a missing `src/gen/dispatch_v2.c` on disk into
`needs_setup`. That is right for a setup host, but wrong for a playable binary
that carries generated code: moving the `.exe` away from the project tree must
not ask the player for a compiler. The guard is
`#if !defined(SNESRECOMP_HAS_GENERATED_CODE)` around the `sources_missing`
check in `snesrecomp_codegen_host.c`; `SNESRECOMP_SETUP_HOST` and
`SNESRECOMP_FORCE_SETUP` still force the wizard. The framework change is
carried in `game/patches-snesrecomp-local.diff`. Generate & rebuild remains
available when a project root is found, but is never required to boot.

### D9. Documentation and pins

- `game/framework_pins.txt`: add the recomp-ui commit. `record_pins.sh`
  reports it as `recomp-ui: ok`; the existing `snesrecomp`, `recomp-net` and
  `retcomm-rbengine` lines remain provenance pins for the vendored tree and a
  full `--check` reports them as present but not git checkouts.
- `README.md`: Run section (`./run.sh`, `./run.sh --launcher`), first-run ROM
  selection, and the note that F10 is still the in-game menu.
- `game/README.md`: remove the "scaffolded without the GUI launcher" paragraph,
  add the launcher to Layout.
- `docs/ARCHITECTURE.md`: add recomp-ui as a dependency and the boot/ingame
  split.
- `docs/CONFIG.md`: ownership table and the note that the launcher exposes
  generic host keys while the port's widescreen stays in F10.
- `docs/VALIDATION.md` (if it names the launcher): the gate still uses
  `--no-launcher`.

## Risks / Trade-offs

- **A second dependency pin and upstream churn** → pin an exact SHA in
  `framework_pins.txt`; recomp-ui's C ABI is additive and its CMake target is
  stable. Update deliberately with the framework pin.
- **Two settings surfaces drift** → the ownership table in D3, the hidden
  Widescreen row, and a test that F10 reflects a launcher edit.
- **`SkipLauncher` is a one-way door** → `--launcher` and the documented
  `SkipLauncher=0` escape; enabling the in-game launcher remains a follow-up.
- **Build size and time** → ~16 MB of source/assets plus ImGui/OpenGL. The
  headless path adds no runtime behavior, and the launcher assets are staged
  only beside the executable.
- **Language mix** → recomp-ui ships `en`/`it`; the F10 panel is Spanish. This
  change accepts an English launcher and leaves a Spanish translation to
  upstream.
- **Packaging is only as good as its test** → build a setup host and run
  `stage_setup_host.sh` as part of the release dry run; the full wizard needs a
  ROM and toolchain and is a manual desktop test.
- **The launcher exposes rows the port does not use** → only capability-wired
  rows appear; widescreen is hidden, shader/MSU-1/rewind stay off.

## Migration Plan

1. Add the submodule and pin; wire CMake and `kGameHost`;
2. build and run with `--no-launcher` to confirm the headless/gate path is
   unchanged;
3. run with `--launcher`, verify ROM/verify, display, audio, controls, saves,
   mods, and that F10 shows the post-launcher host values;
4. build a setup host and run `stage_setup_host.sh`; complete a wizard run on a
   desktop with a ROM and toolchain;
5. update docs and the pin.

Rollback is removing the CMake block, `kGameHost` flags, the codegen source and
the submodule. `run.sh` and `validate.sh` already pass `--no-launcher`, so the
development path returns to today's behavior without touching game code.

## Open Questions

- Should a follow-up expose rewind rows (`rewind_settings = 1`)? It is a
  generic host feature with no current UI, but it changes the config surface.
- Should the launcher name sibling revisions (`rom_revisions`) instead of only
  reporting the expected digests?
- Should F10 eventually move to recomp-ui's runtime UI so both surfaces share
  one model? That is a separate change, not this one.
