## MODIFIED Requirements

### Requirement: Single settings model

The game SHALL hold every user-facing option in one settings model: widescreen
on/off, aspect, maximum width, track margin, wide HUD, cull window, fullscreen
mode, pixel aspect, audio volume, rival indicator, minimap and unlock all
tours. Each option SHALL have a `config.ini` section and key, an optional
environment variable, a default, a range or list of values, and an apply class
(`live`, `next_race`, `restart` or `permanent_note`). Modules SHALL read these
options from the model and SHALL NOT call `getenv` or read `config.ini` for
them directly.

#### Scenario: Module reads an option

- **WHEN** the HUD, cull, radar, rival indicator, margin, frame-width or unlock
  code needs its option
- **THEN** it reads the value from the settings model

#### Scenario: Debug variables untouched

- **WHEN** a debug variable (`*_LOG`, `*_TEST_*`, `SNESRECOMP_WS_GHOST_AHEAD`,
  `SNESRECOMP_WS_GHOST_MAXSTEPS`, `SNESRECOMP_WS_GHOST_RESYNC`,
  `SNESRECOMP_WS_OAM_XHIGH`) is set
- **THEN** it behaves as before this change and is not part of the settings model

### Requirement: Option keys

The options SHALL use these keys and environment variables:

| Option | Key | Environment | Default |
|---|---|---|---|
| Widescreen | `[Graphics] Widescreen` | `SNESRECOMP_WS` | host default |
| Aspect | `[Widescreen] Aspect` | `SNESRECOMP_WS_ASPECT` | `auto` |
| Maximum width | `[Widescreen] MaxWidth` | — | 512 |
| Track margin | `[Widescreen] Margin` | `SNESRECOMP_WS_OFFWINDOW`, legacy `SNESRECOMP_WS_TRACK_CLAMP` | `source` |
| Wide HUD | `[Widescreen] Hud` | `SNESRECOMP_WS_HUD` | on |
| Cull window | `[Widescreen] Cull` | `SNESRECOMP_WS_CULL` | on |
| Fullscreen | `[Graphics] Fullscreen` (0 windowed, 1 borderless, 2 exclusive) | — | host default |
| Pixel aspect | `[Graphics] DisplayAspect` (CRT 4:3, 8:7, 1:1) | — | host default |
| Audio volume | `[Sound] Volume` (0..100) | — | host default |
| Rival indicator | `[Widescreen] RivalIndicator` | `SNESRECOMP_WS_RIVAL` | on |
| Minimap | `[Video] MiniMap` | `SNESRECOMP_WS_MINIMAP` | on |
| Unlock all tours | `[Emulation] UnlockAllTours` | `SNESRECOMP_UNLOCK_TOURS` | off |

`[Graphics] Widescreen`, `[Graphics] Fullscreen`, `[Graphics] DisplayAspect`
and `[Sound] Volume` SHALL remain owned by the host: the game reads them from
the host's parsed configuration and changes them only through the host's
setters. The host SHALL expose a display setter that applies fullscreen mode
and pixel aspect live, by the same path its launcher uses, a volume setter that
applies `[Sound] Volume` live, and both SHALL write to `config.ini` only when
asked.

#### Scenario: Existing configuration keeps working

- **WHEN** a `config.ini` written before this change has
  `[Video] MiniMap = 0` and `[Emulation] UnlockAllTours = 1`
- **THEN** the minimap is off and the tours are unlocked, as before

#### Scenario: Legacy clamp variable

- **WHEN** only `SNESRECOMP_WS_TRACK_CLAMP` is set
- **THEN** the track margin is `clamp` and its source is `env`

#### Scenario: Fullscreen applied live

- **WHEN** the fullscreen mode changes from windowed to borderless through the
  display setter without persisting
- **THEN** the window becomes borderless fullscreen at once and `config.ini` is
  unchanged

#### Scenario: Width out of range

- **WHEN** `[Widescreen] MaxWidth = 900`
- **THEN** the maximum width is 512

#### Scenario: Rival indicator from config.ini

- **WHEN** `SNESRECOMP_WS_RIVAL` is unset and `config.ini` has
  `[Widescreen] RivalIndicator = 0`
- **THEN** the indicator is off and its source is `config`
