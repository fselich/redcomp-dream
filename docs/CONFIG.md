# Configuration

The port's options live in `config.ini` beside the executable (`game/build-local/` when launched with `./run.sh`). One settings model, `game/src/ws_settings.c`, defines every option the port owns: its key, environment override, values, default and how it applies. The in-game menu and the config writer both go through it.

## Precedence

**environment > `config.local.ini` > `config.ini` > default.** A value set by the environment or by `config.local.ini` is shown in the menu with `[ENV]` or `[LOCAL]` and cannot be changed there; the writer never touches it.

## Options

| Option | Key | Values | Default | Environment |
|---|---|---|---|---|
| Widescreen | `[Graphics] Widescreen` | `0`, `1` | `1` (a fresh install; an explicit `0` stays off) | `SNESRECOMP_WS` |
| Aspect | `[Widescreen] Aspect` | `auto`, `16:9`, `21:9`, `32:9`, or a decimal such as `2.0` | `auto` | `SNESRECOMP_WS_ASPECT` |
| Maximum width | `[Widescreen] MaxWidth` | 342..512 (menu presets 384, 448, 512) | `512` | — |
| Track margin | `[Widescreen] Margin` | `source`, `clamp` | `source` | `SNESRECOMP_WS_OFFWINDOW`; legacy `SNESRECOMP_WS_TRACK_CLAMP` (any value) means `clamp` |
| Expanded HUD | `[Widescreen] Hud` | `0`, `1` | `1` | `SNESRECOMP_WS_HUD` |
| Cull window | `[Widescreen] Cull` | `0`, `1` | `1` | `SNESRECOMP_WS_CULL` |
| Fullscreen | `[Graphics] Fullscreen` | windowed, borderless, exclusive | host default | — |
| Pixel aspect | `[Graphics] DisplayAspect` | CRT 4:3, 8:7, 1:1 | host default | — |
| Audio volume | `[Sound] Volume` | 0..100 | host default | — |
| Minimap (radar) | `[Video] MiniMap` | `0`, `1` | `1` | `SNESRECOMP_WS_MINIMAP` |
| Unlock all tours | `[Emulation] UnlockAllTours` | `0`, `1` | `0` | `SNESRECOMP_UNLOCK_TOURS` |

Notes:

- Booleans accept `1/0`, `true/false`, `on/off`, `yes/no`.
- `[Graphics]` and `[Sound]` keys are owned by the framework host: the port reads them from the host's parsed configuration and changes them through the host's setters, which apply fullscreen, pixel aspect and volume live. Their file format is the host's.
- `MaxWidth` outside 342..512 is clamped.
- **Margin values of removed policies** still load: `ring` and `ghost` mean `source`; `wrap`, `0` and `off` mean `clamp`. Any other value means `source`. A mapped value is left as written unless the margin is changed and saved from the menu. See [`TRACK_MARGIN.md`](TRACK_MARGIN.md).
- **UnlockAllTours is permanent for that save**: it raises the tour reveal stage in battery SRAM and the game stores it on exit; turning it off later does not hide the tours again. It does nothing in netplay. See [`formats/SAVE_FORMAT.md`](formats/SAVE_FORMAT.md).
- Widescreen applies only in a live race, in 1P, 2P and VS; the radar remains 1P-only. See [`WIDESCREEN.md`](WIDESCREEN.md) and [`TWO_PLAYER.md`](TWO_PLAYER.md).

## Pre-boot launcher

The executable opens the shared recomp-ui launcher before the guest starts. It
is the place for the generic host settings and the first-run setup:

| Surface | Owns |
|---|---|
| Launcher | `[Graphics]` window scale, fullscreen, display aspect, linear filtering, frame blend, vsync, renderer, run-ahead; `[Sound]` volume and rate; `[KeyMap]`; `[GamepadMap]`; `saves/save.srm`; Mods; *Generate & rebuild* |
| F10 menu | `[Widescreen]` (widescreen, aspect, maximum width, margin, wide HUD, cull window); `[Video] MiniMap`; `[Emulation] UnlockAllTours` |
| Both | `[Graphics] Fullscreen`, `[Graphics] DisplayAspect`, `[Sound] Volume`; F10 reads the current host values when it opens |

The launcher's generic **Widescreen** row is hidden on purpose: the port's
widescreen model has its own `Aspect`, `MaxWidth` and `Margin` options and
lives in F10. The pixel-aspect row is the same `[Graphics] DisplayAspect` key
F10 shows.

Use `./run.sh --launcher` to open it. `--no-launcher` (and `run.sh`'s default),
`SNESRECOMP_NO_LAUNCHER=1`, headless/scripted runs and `SkipLauncher=1` skip it;
`--launcher` forces it even after `SkipLauncher=1`. The launcher's
*Generate & rebuild* writes the generated C into `src/gen/` and builds a
playable binary; a release setup pack uses the same flow.

## In-game menu

**F10** or **Select + L** opens the menu in a race, in the game's menus or on the title screen; the game freezes while it is open. It is not available in netplay. This is the only in-game menu: the pre-boot launcher above is not opened from a running game.

- Up/down (arrows or d-pad) move between rows; left/right (or `<`/`>`) change the value and apply it at once. Held directions repeat.
- A/Enter activates *SAVE TO CONFIG.INI* (save) or *CLOSE* (close). B, Escape or F10 close.
- Closing keeps the changes for the session; a mark shows while they differ from `config.ini`.
- Saving updates `config.ini` in place: existing lines keep their position and comments, missing keys are added to their section, a missing section is created at the end. The file is written to a temporary copy first.

The labels are uppercase English ASCII, drawn with the host's 8×8 font. F10 used to load save-state slot 10; that slot stays reachable from the F11 save-state browser or by rebinding `Load`.

## Debug variables

Environment only, read once per process, not in the menu.

| Variable | Effect |
|---|---|
| `SNESRECOMP_WS_OFFWINDOW_LOG=<path>` | CSV of BG1 margin decisions (`frame,cell_x,cell_y,world_x,world_y,real,mask,masked`), one line per cell and frame; also enables the `[ws-ow]` stderr lines |
| `SNESRECOMP_WS_SOURCE_LOG=<path>` | source anchor log (search, self-check, drops); also enables the `[ws-ow]` stderr lines |
| `SNESRECOMP_WS_PARK_LOG=<path>` | CSV of parked and right-margin OAM entries (`frame,slot,lo,y,hi,rel,rel_y,E,action`) |
| `SNESRECOMP_WS_OAM_XHIGH=0` | disables the rider OAM fixes (X high bit, parked pair, sentinel) |
| `SNESRECOMP_WS_OAM_XHIGH_LOG=<path>` | CSV of X-high fixes (`frame,slot,lo,want`; `lo=-1` is a hidden parked slot) |
| `SNESRECOMP_WS_RADAR_LOG=<path>` | CSV of radar positions (`frame,ptx,pty,rtx,rty,ax,ay,pword,rword`: player and rival table cells, anchor, words under them) |
| `SNESRECOMP_WS_2P_LINE_LOG=<path>` | 2P/VS per-line BG1 diagnostic (`frame,line,vp,map_base,bgmode,h,v,main_screen_layers`); viewport `-1` is the divider |
| `SNESRECOMP_WS_2P_OAM_LOG=<path>` | 2P/VS per-slot rider action log (`frame,slot,half,lo,y,hi,tile,attr,rel,rel_y,E,action`; actions `hide`, `xhigh`, `xhigh-clear`, `hi-positive`, `keep`, `skip`) |
| `SNESRECOMP_WS_2P_OAM_STAGE_LOG=<path>` | raw 2P OAM/staging trace (slots 96..99, high-table byte 24, `$1501-$1510`, `$1599`, cameras and relative rider positions) |
| `SNESRECOMP_WS_TEST_OAM=slot:x:y:tile:attr:big[;…]` | trace builds only: overrides OAM slots for the presentation pass |
| `SNESRECOMP_WS_TEST_OAM_LEFT_HINT=1` | trace builds only: marks the injected slots as left-margin for the renderer |

## Framework variables

Read by the framework, not by `game/src`; listed because the tools and the validation gate use them.

| Variable | Effect |
|---|---|
| `SNESRECOMP_NO_LAUNCHER=1` | skip the pre-boot launcher |
| `SNESRECOMP_RUN_FRAMES=<n>` | quit after `n` frames |
| `SNESRECOMP_SCREENSHOT=<path>`, `SNESRECOMP_SCREENSHOT_FRAME=<n>` | write frame `n` as a PPM |
| `SNESRECOMP_DEBUG_PORT=<port>` | start the debug server used by `tools/scripts/ws-native-map-spike/dbg.py` |
| `SNESRECOMP_BGPROBE_FRAME=<n>[,<m>]`, `SNESRECOMP_BGPROBE_LAYERS=<mask>`, `SNESRECOMP_BGPROBE_FILE=<path>` | BG pixel-source probe: logs the VRAM map word behind every drawn span on frames `n..m` (default layer mask 1 = BG1); scored by `tools/scripts/ws-offwindow/world_key_check.py` |
