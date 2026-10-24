## ADDED Requirements

### Requirement: Pre-boot launcher ownership

The pre-boot launcher SHALL be an allowed writer of the host-owned keys it
exposes: `[Graphics]` (fullscreen, display aspect, linear filtering, window
scale, renderer, vsync, frame blend, run-ahead), `[Sound]` (volume and audio
rate), `[KeyMap]` and `[GamepadMap]`. The game's settings writer SHALL NOT
overwrite those keys. The launcher SHALL NOT write the port's `[Widescreen]`,
`[Video] MiniMap` or `[Emulation] UnlockAllTours` keys. When the in-game menu
opens after a launcher change, its host-owned rows SHALL show the current
values.

#### Scenario: Fullscreen changed before boot

- **WHEN** the player sets fullscreen to borderless in the pre-boot launcher
- **THEN** the game boots borderless
- **AND** the F10 fullscreen row shows `COMPLETA` when the menu opens

#### Scenario: Volume changed before boot

- **WHEN** the player sets the volume to 40 in the pre-boot launcher
- **THEN** the game boots at 40
- **AND** the F10 volume row shows `40%` when the menu opens

#### Scenario: Port options stay out

- **WHEN** the pre-boot launcher is open
- **THEN** it offers no `Aspect`, `MaxWidth`, `Margin`, `Hud`, `Cull`, `MiniMap`
  or `UnlockAllTours` control

#### Scenario: Menu save leaves host keys alone

- **WHEN** the player presses *Save* in the F10 menu
- **THEN** `[Graphics]`, `[Sound]`, `[KeyMap]` and `[GamepadMap]` are left as
  the host wrote them

### Requirement: Widescreen on by default

A fresh configuration with no `[Graphics] Widescreen` key in `config.ini` or `config.local.ini` and no `SNESRECOMP_WS` SHALL boot with widescreen on. An explicit `[Graphics] Widescreen = 0` or `SNESRECOMP_WS=0` SHALL keep it off, and a `config.local.ini` value SHALL be respected. The default SHALL apply only to the port's widescreen setting and SHALL NOT change the host's generic settings.

#### Scenario: Fresh configuration

- **WHEN** neither config file has a `[Graphics] Widescreen` key and
  `SNESRECOMP_WS` is unset
- **THEN** the first live race is drawn wide

#### Scenario: Explicit off

- **WHEN** `config.ini` has `[Graphics] Widescreen = 0`
- **THEN** the race is drawn at 256 px

#### Scenario: Environment off

- **WHEN** `SNESRECOMP_WS=0`
- **THEN** the race is drawn at 256 px even if the key is absent

#### Scenario: Local override

- **WHEN** `config.local.ini` has `[Graphics] Widescreen = 0`
- **THEN** the race is drawn at 256 px
