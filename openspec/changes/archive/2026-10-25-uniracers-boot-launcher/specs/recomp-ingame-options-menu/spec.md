## ADDED Requirements

### Requirement: Coexistence with the pre-boot launcher

The F10 menu SHALL remain the only in-game options menu. The project SHALL NOT
enable the host's in-game launcher and SHALL NOT bind `OpenLauncher`; the
pre-boot launcher SHALL NOT rebind, replace, or open F10. F10 SHALL keep its
rows, keys and panel unchanged. When a pre-boot launcher edit changes a
host-owned setting, F10 SHALL show the new value when it opens.

#### Scenario: F10 unchanged

- **WHEN** the player presses F10 or Select+L in a race
- **THEN** the same port options panel opens as before this change

#### Scenario: No second in-game launcher

- **WHEN** the player presses the host's launcher key or pad gesture in a
  running game
- **THEN** no launcher opens, because the project does not enable the in-game
  launcher

#### Scenario: Launcher edit visible in F10

- **WHEN** the pre-boot launcher changed the pixel aspect and the player later
  opens F10
- **THEN** the pixel-aspect row shows the value the launcher wrote

#### Scenario: Port options remain

- **WHEN** F10 is open
- **THEN** it still lists widescreen, aspect, maximum width, track margin, wide
  HUD, cull window, fullscreen, pixel aspect, audio volume, minimap and unlock
  all tours

#### Scenario: Expanded HUD label

- **WHEN** F10 is open
- **THEN** the wide-HUD row is labelled `HUD EXPANDIDO`
