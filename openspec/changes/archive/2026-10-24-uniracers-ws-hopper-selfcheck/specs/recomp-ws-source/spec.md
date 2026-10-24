## MODIFIED Requirements

### Requirement: Continuous self-check

The system SHALL compare verified native cells against the table every frame. While the rider is airborne (`$7E:0BE9 != 0`), an anchor whose agreement falls below 95 % SHALL be held and the margins SHALL continue to be served from the table, because the jump moves the BG1 rows and the disagreement does not mean the anchor is wrong. On the ground, an anchor whose agreement falls below 95 % SHALL be held for up to 30 consecutive failed checks and SHALL be dropped after the 30th. A passing check SHALL reset the counter. A BG1 map-base/BGMODE change, a non-live race or an invalid header SHALL invalidate the anchor immediately. Each airborne hold and each ground hold/drop SHALL be logged.

#### Scenario: Airborne stunt

- **WHEN** the rider is airborne (`$7E:0BE9` nonzero) and fewer than 95 % of the checked cells match an anchor
- **THEN** the anchor is kept for the whole airborne state and the margins keep serving the table
- **AND** each held frame is logged as `hold air`

#### Scenario: Ground transient

- **WHEN** the rider is on the ground and fewer than 95 % of the checked cells match an anchor for fewer than 30 consecutive frames
- **THEN** the anchor is kept and the margins keep serving the table
- **AND** each hold is logged

#### Scenario: Persistent ground disagreement

- **WHEN** the rider is on the ground and fewer than 95 % of the checked cells match an anchor for 30 consecutive frames
- **THEN** the anchor is dropped and the event is logged
- **AND** when no anchor remains, margins are masked and a re-anchor starts

#### Scenario: Recovery

- **WHEN** a held anchor passes the check again
- **THEN** its failure counter is reset
- **AND** the anchor stays live

#### Scenario: Scene change

- **WHEN** the BG1 map base or BGMODE changes, or the race stops being live
- **THEN** the anchor is invalidated immediately without a hold

#### Scenario: Hopper stunt transient

- **WHEN** a 1P Hopper race holds B in the first seconds and the rider is airborne for the measured ~273 frames
- **THEN** the anchor is held, the 21:9 right margin keeps serving the table track, and the frame's track reaches the right edge instead of stopping at the 4:3 edge
- **AND** no margin cell is served where the table has no track (the gap stays empty)
