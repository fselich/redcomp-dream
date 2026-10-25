## MODIFIED Requirements

### Requirement: Rows and values

The menu SHALL list one row per option of the settings model (widescreen,
aspect, maximum width, track margin, wide HUD, cull window, fullscreen, pixel
aspect, audio volume, rival indicator, minimap, unlock all tours), followed by
*Save* and *Close*. Up and down SHALL move between rows; left and right SHALL
change the selected option's value through its list or range; A or Enter SHALL
activate *Save* or *Close*. Held directions SHALL auto-repeat. Labels SHALL be
uppercase ASCII English, drawable by the host's 8×8 font. Every row and the
footer SHALL remain inside the 512×448 panel.

#### Scenario: Change a value

- **WHEN** the aspect row is selected and the player presses right
- **THEN** the aspect moves to the next value in `auto, 16:9, 21:9, 32:9`

#### Scenario: Value not in the list

- **WHEN** `config.ini` has `Aspect = 2.0`
- **THEN** the row shows `2.0`, and pressing right moves to the first listed
  value

#### Scenario: Rival indicator row

- **WHEN** the player selects the rival indicator row and presses right
- **THEN** the value toggles between `ON` and `OFF`

#### Scenario: All rows fit

- **WHEN** the menu lists all twelve settings plus *Save* and *Close*
- **THEN** the last row is above the footer and no text is clipped
