## ADDED Requirements

### Requirement: Host title-menu hook

The snesrecomp host SHALL let a title register one modal overlay menu through `SnesDesktopHostGame`. While that menu is open, the host SHALL freeze the guest, pause audio, send key presses to the menu instead of the keymap, send pad navigation to the menu, filter the guest's input, and present the frozen frame with the menu's panel on top, as it does for the save-state browser. The title menu and the save-state browser SHALL NOT be open at the same time. A title that does not register a menu SHALL see no change in behaviour. The patch SHALL be carried in `game/patches-snesrecomp-local.diff`.

#### Scenario: Guest frozen

- **WHEN** the menu is open during a race
- **THEN** the guest runs no frames and audio is paused until the menu closes

#### Scenario: Input does not reach the game

- **WHEN** the player presses A or a direction while the menu is open
- **THEN** the game receives no input from it, and a direction held at close does not stick in the guest

#### Scenario: Exclusive with the save-state browser

- **WHEN** the options menu is open and the player presses F11 or Select+R
- **THEN** the save-state browser does not open

#### Scenario: Title without a menu

- **WHEN** the host runs a title that leaves the title-menu field empty
- **THEN** F10 keeps its keymap meaning and no menu opens

### Requirement: Opening the menu

The options menu SHALL open with F10 on the keyboard and with Select+L on the pad, in races, in the game's menus and on the title screen. F10 SHALL open the menu instead of loading save-state slot 10. B, Escape and F10 SHALL close it. The menu SHALL NOT open while a netplay session is active.

#### Scenario: Keyboard

- **WHEN** the player presses F10 during a race
- **THEN** the options menu opens and no save state is loaded

#### Scenario: Pad

- **WHEN** the player presses Select+L
- **THEN** the options menu opens

#### Scenario: Netplay

- **WHEN** a netplay session is active and the player presses F10 or Select+L
- **THEN** the menu does not open

### Requirement: Rows and values

The menu SHALL list one row per option of the settings model (widescreen, aspect, maximum width, track margin, wide HUD, cull window, fullscreen, pixel aspect, audio volume, minimap, unlock all tours), followed by *Save* and *Close*. Up and down SHALL move between rows; left and right SHALL change the selected option's value through its list or range; A or Enter SHALL activate *Save* or *Close*. Held directions SHALL auto-repeat. Labels SHALL be uppercase ASCII Spanish, drawable by the host's 8×8 font.

#### Scenario: Change a value

- **WHEN** the aspect row is selected and the player presses right
- **THEN** the aspect moves to the next value in `auto, 16:9, 21:9, 32:9`

#### Scenario: Value not in the list

- **WHEN** `config.ini` has `Aspect = 2.0`
- **THEN** the row shows `2.0`, and pressing right moves to the first listed value

### Requirement: Live apply

A value changed in the menu SHALL apply according to its class: `live` options take effect when the guest resumes, without a restart. Frame-width options SHALL NOT resize the frozen frame while the menu is open.

#### Scenario: Turn on widescreen in a race

- **WHEN** widescreen is turned on in the menu during a race and the menu closes
- **THEN** the next race frame is wide

#### Scenario: Fullscreen from the menu

- **WHEN** the player sets fullscreen to borderless with the menu open
- **THEN** the window switches to borderless at once and the menu stays usable at the new size

#### Scenario: Pixel aspect from the menu

- **WHEN** the player changes the pixel aspect from CRT 4:3 to 8:7 and closes the menu
- **THEN** the picture uses square pixels, and a window that has no custom size is resized to match

#### Scenario: Cull off mid-race

- **WHEN** the cull window is turned off during a race and the menu closes
- **THEN** `$0421/$0423` hold their original values and the race continues without visual errors

### Requirement: Save to config.ini

*Save* SHALL write every unlocked option to `config.ini` through the settings writer. The widescreen, fullscreen and pixel-aspect rows SHALL be saved through the host's setters. Closing without saving SHALL keep the changes for the session, and the panel SHALL show an unsaved mark while values differ from the file.

#### Scenario: Save and restart

- **WHEN** the player sets the aspect to 21:9, saves, quits and starts the game again
- **THEN** the aspect is 21:9

#### Scenario: Close without saving

- **WHEN** the player changes the minimap and closes without saving
- **THEN** the change stays for the session, and `config.ini` is unchanged

### Requirement: Locked options

An option whose source is the environment or `config.local.ini` SHALL be shown with its value and a `[ENV]` or `[LOCAL]` mark. Left and right SHALL NOT change it, and *Save* SHALL NOT write it. The footer SHALL name the variable or file that sets it.

#### Scenario: Locked by environment

- **WHEN** the game runs with `SNESRECOMP_WS_MINIMAP=0` and the minimap row is selected
- **THEN** the row shows `OFF [ENV]`, right does nothing, and the footer names `SNESRECOMP_WS_MINIMAP`

### Requirement: Warnings

When the selected option is not `live`, the footer SHALL say how it applies: `next_race` (next race), `restart` (requires a restart) or `permanent_note` (the option's own note). Unlock all tours SHALL warn that it is permanent for the save and that turning it off does not hide the tours again.

#### Scenario: Unlock tours note

- **WHEN** the unlock row is selected
- **THEN** the footer says the change is permanent for the save

#### Scenario: Unlock on

- **WHEN** unlock all tours is turned on in the menu and the menu closes
- **THEN** `$10D3` is raised to 3 in a valid save on the next frame, as with the config option
