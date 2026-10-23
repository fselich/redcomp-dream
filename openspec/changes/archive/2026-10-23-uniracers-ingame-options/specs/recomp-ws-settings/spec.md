## ADDED Requirements

### Requirement: Single settings model

The game SHALL hold every user-facing option in one settings model: widescreen on/off, aspect, maximum width, track margin, wide HUD, cull window, fullscreen mode, pixel aspect, audio volume, minimap and unlock all tours. Each option SHALL have a `config.ini` section and key, an optional environment variable, a default, a range or list of values, and an apply class (`live`, `next_race`, `restart` or `permanent_note`). Modules SHALL read these options from the model and SHALL NOT call `getenv` or read `config.ini` for them directly.

#### Scenario: Module reads an option

- **WHEN** the HUD, cull, radar, margin, frame-width or unlock code needs its option
- **THEN** it reads the value from the settings model

#### Scenario: Debug variables untouched

- **WHEN** a debug variable (`*_LOG`, `*_TEST_*`, `SNESRECOMP_WS_GHOST_AHEAD`, `SNESRECOMP_WS_GHOST_MAXSTEPS`, `SNESRECOMP_WS_GHOST_RESYNC`, `SNESRECOMP_WS_OAM_XHIGH`) is set
- **THEN** it behaves as before this change and is not part of the settings model

### Requirement: Source precedence

Each option's value SHALL be resolved as: the environment variable when set and non-empty, else `config.local.ini`, else `config.ini`, else the default. The model SHALL record which source supplied the value.

#### Scenario: Environment wins

- **WHEN** `config.ini` has `[Video] MiniMap = 1` and `SNESRECOMP_WS_MINIMAP=0` is set
- **THEN** the minimap is off and its source is `env`

#### Scenario: Local file wins over config.ini

- **WHEN** `config.ini` has `[Widescreen] Hud = 1` and `config.local.ini` has `[Widescreen] Hud = 0`, and no environment variable is set
- **THEN** the wide HUD is off and its source is `local`

#### Scenario: Default

- **WHEN** no source sets `[Widescreen] Aspect`
- **THEN** the aspect is `auto` and its source is `default`

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
| Minimap | `[Video] MiniMap` | `SNESRECOMP_WS_MINIMAP` | on |
| Unlock all tours | `[Emulation] UnlockAllTours` | `SNESRECOMP_UNLOCK_TOURS` | off |

`[Graphics] Widescreen`, `[Graphics] Fullscreen`, `[Graphics] DisplayAspect` and `[Sound] Volume` SHALL remain owned by the host: the game reads them from the host's parsed configuration and changes them only through the host's setters. The host SHALL expose a display setter that applies fullscreen mode and pixel aspect live, by the same path its launcher uses, a volume setter that applies `[Sound] Volume` live, and both SHALL write to `config.ini` only when asked.

#### Scenario: Existing configuration keeps working

- **WHEN** a `config.ini` written before this change has `[Video] MiniMap = 0` and `[Emulation] UnlockAllTours = 1`
- **THEN** the minimap is off and the tours are unlocked, as before

#### Scenario: Legacy clamp variable

- **WHEN** only `SNESRECOMP_WS_TRACK_CLAMP` is set
- **THEN** the track margin is `clamp` and its source is `env`

#### Scenario: Fullscreen applied live

- **WHEN** the fullscreen mode changes from windowed to borderless through the display setter without persisting
- **THEN** the window becomes borderless fullscreen at once and `config.ini` is unchanged

#### Scenario: Width out of range

- **WHEN** `[Widescreen] MaxWidth = 900`
- **THEN** the maximum width is 512

### Requirement: In-place config writer

The game SHALL write its options to `config.ini` in place. It SHALL update existing `key = value` lines in their section, add missing keys at the end of their section, and create the `[Widescreen]` section when it is missing. Every other line (comments, blank lines, unknown keys, host sections) SHALL stay byte-for-byte unchanged. The write SHALL go to a temporary file that then replaces `config.ini`. It SHALL NOT write options whose source is `env` or `local`, and it SHALL NOT write the host-owned `[Graphics]` keys itself.

#### Scenario: Round trip

- **WHEN** a `config.ini` with comments, a `[KeyMap]` section and an unknown key under `[Video]` is saved after changing only the minimap
- **THEN** the only changed line is `MiniMap = …`

#### Scenario: New section

- **WHEN** `config.ini` has no `[Widescreen]` section and the aspect is saved as `21:9`
- **THEN** the file ends with a `[Widescreen]` section that contains `Aspect = 21:9`, and the rest of the file is unchanged

#### Scenario: Host write afterwards

- **WHEN** the host later rewrites its own keys (volume, fullscreen, widescreen)
- **THEN** the `[Widescreen]` section written by the game is still present and unchanged

### Requirement: Live re-application

A change to an option of class `live` SHALL take effect no later than the next presented race frame, without a restart. Options cached at start-up (radar switch, unlock switch, margin mode) SHALL read the model each time instead of caching a copy.

#### Scenario: Margin switch

- **WHEN** the track margin changes from `source` to `clamp` during a race
- **THEN** the next race frame uses `clamp`, and the margin ring state is reset

#### Scenario: Minimap switch

- **WHEN** the minimap is turned off during a race
- **THEN** the radar is no longer drawn from the next frame on
