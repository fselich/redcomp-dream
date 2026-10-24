# Local work directory

Persistent, local-only workspace for the Uniracers recompilation experiments.
Everything here is derived from the ROM or from the SNESRecomp checkout. It is
gitignored: do not commit it and do not redistribute it.

## Layout

Only the work directories for the active changes are kept:

- `ws-native-map-spike/` — project checkout, build, captures, dumps, logs and
  scripts for the native-map spike.
- `ws-offwindow/` — off-window follow-up. `project` is an **old standalone
  copy**, not a symlink: it lacks the `source`/ultrawide code. Do not build or
  patch it; the working tree is
  `work/ws-native-map-spike/project/UniracersSNESRecomp`. Holds the BG1 probe
  captures (`logs/bgprobe-*.csv`), `scripts/world_key_check.py`,
  `patches/bgprobe.diff` + `patches/ppu.c.orig`, and `ring-formula.md`.
- `ws-leading-edge/` — Option A (`SNESRECOMP_WS_OFFWINDOW=ring`): probe/scorer,
  wide captures and the renderer-side margin hook patch
  (`patches/ws-leading-edge.diff`).
- `ws-ghost-lane/` — ghost lane (`SNESRECOMP_WS_OFFWINDOW=ghost`): speculative
  future harvest, state traces, CHR/timing logs and
  `patches/ws-ghost-lane.diff` + `patches/beam-exec.diff`.
- `ws-ghost-xhigh/` — rider OAM X high-bit fix for the widened right cull:
  before/after frames, fix/lag logs, `patches/ws-ghost-xhigh.diff`;
  `regress-zoom/` (zoomed-track and parked-sprite follow-ups) and `gate/`
  (live RAM monitor that found the tour-1-only race gate).
- `ws-radar/` — local 32×32 radar (`[Video] MiniMap`): logs, panel crops,
  state dumps and `patches/ws-radar.diff`.
- `ws-ghost-offscreen/` — the original rival-offscreen diagnosis (recording
  scripts `rec.py`/`ana.py`/`run.sh`, frames 1725–1750).

Test runs that start from a clean SRAM must use a **copy** of the executable
in another directory: `build-local/saves/` holds the user's real save.

The work directories for archived phases (`baseline`, `boot-spike`, `aot-*`,
`race-recon`, `ws-discriminator`, `ws-hdma`, `ws-hud`, `ws-objects`, `ws-spawn`,
`ws-spike`, `ws-track-fill`, `ws-track-margins`, `ws-validation` and
`ws-zoomzoo-spawn`) were removed on 2026-10-21. Their conclusions live in the
archived OpenSpec changes and in `docs/`.

- `ws-map-decode/` — map-decode spike (`openspec/changes/uniracers-ws-map-decode`):
  an isolated project copy (`project/`, build `build-decode/`, debug port 9998),
  dumps, logs, renders, unpacked tracks and Python tools. Start with
  `ws-map-decode/notes/findings.md`; the format is in
  `docs/UNIRACERS_TRACK_FORMAT.md`.

- `ws-source/` — runtime `source` mode (`openspec/changes/uniracers-ws-source`):
  patch (`patches/ws-source.diff`), validation script
  (`scripts/validate_all.sh [mode]`), logs, state traces and captures.

## Executable

```
work/ws-native-map-spike/project/UniracersSNESRecomp/build-local/UniracersSNESRecomp
```

`build-local/` is the working build on this machine. The original `build/`
was configured at `<workspace>/...` and does not rebuild
here; it is kept only for its `config.ini`/`keybinds.ini` (copied into
`build-local/`).

Run it from its build directory (the host anchors `config.ini` and saves beside
the executable):

```sh
cd game/build-local
./UniracersSNESRecomp
```

It needs the ROM at runtime:

```sh
./UniracersSNESRecomp --rom "<repo>/rom/Uniracers (USA).sfc"
```

Headless, 600 frames:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
SNESRECOMP_NO_LAUNCHER=1 SNESRECOMP_RUN_FRAMES=600 \
./UniracersSNESRecomp --rom "<repo>/rom/Uniracers (USA).sfc" --no-launcher
```

## Widescreen BG1 margin policy

With `SNESRECOMP_WS=1` (or `Widescreen = 1`), `SNESRECOMP_WS_OFFWINDOW` selects
it: unset = `source` (track table, default), `ring`, `ghost` (slow), `clamp`,
`wrap` (raw ring). Diagnostics: `SNESRECOMP_WS_OFFWINDOW_LOG`,
`SNESRECOMP_WS_SOURCE_LOG`. See `openspec/changes/uniracers-ws-source-default/`.

Race width: `SNESRECOMP_WS_ASPECT` = `auto` (default: max(16:9, window)),
`16:9`, `21:9`, any `W:H` or decimal; capped at 512 px. See
`openspec/changes/uniracers-ws-ultrawide/`.

## Rebuild

```sh
cd work/ws-native-map-spike/project/UniracersSNESRecomp
cmake -S . -B build-local -G Ninja -DCMAKE_BUILD_TYPE=Release -DSNESRECOMP_ENABLE_TRACE=ON
cmake --build build-local --parallel
```

The build directory records absolute paths. After moving the project, create a
fresh build directory and reconfigure.

## Local framework patches

`snesrecomp/runner/src/snes/ppu.c` carries a test-only BG pixel-source probe
(`SNESRECOMP_BGPROBE_FRAME=N[,M]`, `SNESRECOMP_BGPROBE_LAYERS`,
`SNESRECOMP_BGPROBE_FILE`). It is off by default and leaves native output
bit-identical. Reapply `ws-offwindow/patches/bgprobe.diff` after a submodule
update; restore with `ws-offwindow/patches/ppu.c.orig`. See
`docs/UNIRACERS_BG1_RING_FINDINGS.md`.

The widescreen work also carries:
- a renderer-side margin-tile hook in `ppu.c` (Option A / ghost serving);
- a per-line host hook in `beam_frame_driver.c`;
- beam-exec get/set accessors;
- raster-journal save/restore in `ppu.c` (the ghost lane needs it because a
  speculative run overwrites the real field's journal).

Patches: `ws-leading-edge/patches/ws-leading-edge.diff` and
`ws-ghost-lane/patches/ws-ghost-lane.diff` (the latter includes all of the
above). `ws-ghost-lane/patches/beam-exec.diff` isolates the beam-driver part.

## Widescreen modes

```sh
SNESRECOMP_WS=1 ./UniracersSNESRecomp --rom ".../Uniracers (USA).sfc" --no-launcher
# clamp (fallback):  SNESRECOMP_WS_TRACK_CLAMP=1
# verified ring:     SNESRECOMP_WS_OFFWINDOW=ring
# ghost lane:        SNESRECOMP_WS_OFFWINDOW=ghost
# rider X-high off:  SNESRECOMP_WS_OAM_XHIGH=0
# radar off:         SNESRECOMP_WS_MINIMAP=0
```

The rider OAM X high-bit fix is on by default in widescreen 1P races
(`SNESRECOMP_WS_OAM_XHIGH=0` disables it). It draws the rival at its true X in
the right margin instead of the 256-px phantom;
`SNESRECOMP_WS_OAM_XHIGH_LOG` traces fixed slots.

The local radar is on by default (`[Video] MiniMap = 1`); set
`[Video] MiniMap = 0` in `config.ini` or `SNESRECOMP_WS_MINIMAP=0` to hide it.
It shows a 64×64-cell window from the `$7F` table with player/rival dots.

`ghost` is the correctness-first mode (99.84 % leading served on Dragster →,
0 wrong cells, real lane identical to `ring`, ≈+2.3 ms/frame CPU), but it runs
at ~42 fps headless vs 60 for `ring`; the cost is the lane's snapshot/restore
traffic (AHEAD=0 is still ~42 fps, `guest_ms` 10.97 vs 4.04, `hook_ms` 0.069),
not the ghost raster. Use `ring` for smooth play and `ghost` for validation
until a lighter snapshot strategy lands. The clamp remains the default in the
current build.

## Licensing

SNESRecomp is PolyForm Noncommercial 1.0.0. The generated C, frame captures and
coverage journals are derived from copyrighted ROM data. Keep them local; do not
publish or redistribute them.

## OpenSpec note

The archived change reports in `openspec/` still describe the original
`/tmp/opencode/...` paths and artifacts that have since been deleted; they are
historical records. This README and the files under `work/` are the current
source of truth.
