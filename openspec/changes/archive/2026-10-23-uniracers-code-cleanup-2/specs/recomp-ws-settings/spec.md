## MODIFIED Requirements

### Requirement: Option keys

The options SHALL use these keys and environment variables:

| Option | Key | Environment | Default |
|---|---|---|---|
| Widescreen | `[Graphics] Widescreen` | `SNESRECOMP_WS` | host default |
| Aspect | `[Widescreen] Aspect` | `SNESRECOMP_WS_ASPECT` | `auto` |
| Maximum width | `[Widescreen] MaxWidth` | — | 512 |
| Track margin | `[Widescreen] Margin` (`source`, `clamp`) | `SNESRECOMP_WS_OFFWINDOW`, legacy `SNESRECOMP_WS_TRACK_CLAMP` | `source` |
| Wide HUD | `[Widescreen] Hud` | `SNESRECOMP_WS_HUD` | on |
| Cull window | `[Widescreen] Cull` | `SNESRECOMP_WS_CULL` | on |
| Fullscreen | `[Graphics] Fullscreen` (0 windowed, 1 borderless, 2 exclusive) | — | host default |
| Pixel aspect | `[Graphics] DisplayAspect` (CRT 4:3, 8:7, 1:1) | — | host default |
| Audio volume | `[Sound] Volume` (0..100) | — | host default |
| Minimap | `[Video] MiniMap` | `SNESRECOMP_WS_MINIMAP` | on |
| Unlock all tours | `[Emulation] UnlockAllTours` | `SNESRECOMP_UNLOCK_TOURS` | off |

The track margin SHALL have exactly the two values `source` and `clamp`; values of removed policies SHALL be mapped as `recomp-ws-source-default` specifies, so the menu and the writer only see the two canonical values.

`[Graphics] Widescreen`, `[Graphics] Fullscreen`, `[Graphics] DisplayAspect` and `[Sound] Volume` SHALL remain owned by the host: the game reads them from the host's parsed configuration and changes them only through the host's setters. The host SHALL expose a display setter that applies fullscreen mode and pixel aspect live, by the same path its launcher uses, a volume setter that applies `[Sound] Volume` live, and both SHALL write to `config.ini` only when asked.

#### Scenario: Existing configuration keeps working

- **WHEN** a `config.ini` written before this change has `[Video] MiniMap = 0` and `[Emulation] UnlockAllTours = 1`
- **THEN** the minimap is off and the tours are unlocked, as before

#### Scenario: Legacy clamp variable

- **WHEN** only `SNESRECOMP_WS_TRACK_CLAMP` is set
- **THEN** the track margin is `clamp` and its source is `env`

#### Scenario: Margin cycle

- **WHEN** the menu cycles the track margin from `source`
- **THEN** it shows `clamp`, and cycling again shows `source`

#### Scenario: Mapped legacy value not rewritten

- **WHEN** `config.ini` has `[Widescreen] Margin = wrap` and the player saves from the menu without changing the margin
- **THEN** the `Margin` line is left as it is

#### Scenario: Fullscreen applied live

- **WHEN** the fullscreen mode changes from windowed to borderless through the display setter without persisting
- **THEN** the window becomes borderless fullscreen at once and `config.ini` is unchanged

#### Scenario: Width out of range

- **WHEN** `[Widescreen] MaxWidth = 900`
- **THEN** the maximum width is 512
