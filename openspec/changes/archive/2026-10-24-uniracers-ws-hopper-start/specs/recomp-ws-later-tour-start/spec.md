## ADDED Requirements

### Requirement: Live-race cull plausibility

The system SHALL treat a frame as live only when the rider cull window is a plausible race window: `(int16_t)$0421 <= 0` and `(int16_t)$0423 >= 128`. The check SHALL be shared with the cull module's existing base wait and SHALL apply in 1P and 2P/VS.

#### Scenario: 1P race

- **WHEN** the race cull is `-49..256`
- **THEN** `WsInLiveRace()` is true and the frame is eligible for widening

#### Scenario: 2P race

- **WHEN** the 2P race cull is `-784..4096`
- **THEN** `WsInLiveRace()` is true and the frame is eligible for widening

#### Scenario: Hopper PICK TRACK transition

- **WHEN** the game leaves the PICK TRACK screen with `$00B4=0x1F`, `$0300=0` and `$0421/$0423=0xB33C/0xBB3C`
- **THEN** `WsInLiveRace()` is false and the frame stays 256 px
- **AND** the track-name artifact is not drawn

#### Scenario: PICK TOUR or NOW PLAYING

- **WHEN** the cull is `0x4C00/0x4C00` or `0x373C/0x4C3C`
- **THEN** the frame stays native

### Requirement: Start-seeded source anchor

The system SHALL seed the source X anchor from the `$7F` header start pair (`$7F:0002–0009`, big-endian table cells) and the viewport's rider/camera and unwrapped scroll, find the Y anchor from the first verified samples by table agreement, and keep the existing self-check and fallback. The seed SHALL be generic per track and per viewport; no track-specific constant is allowed.

#### Scenario: Header X seed

- **WHEN** the header start X is valid and the viewport scroll/rider are available
- **THEN** the seeded X equals `header_x - ((scroll + rider_screen_x) >> 4)`
- **AND** Dragster and Hopper reproduce their resolved anchors (`52` and `72`)

#### Scenario: First usable samples

- **WHEN** the start overlay has no usable table words and the first verified samples arrive
- **THEN** the Y anchor is chosen from the table agreement at the seeded X
- **AND** the margins and radar fill at that frame, earlier than the current general search

#### Scenario: Seed disagrees

- **WHEN** the seeded anchor fails the existing self-check or the Y search has no agreement
- **THEN** the seed is discarded and the general `WsSrcSearch` runs unchanged
- **AND** no wrong margin cell is served

#### Scenario: 2P viewports

- **WHEN** the race is 2P/VS
- **THEN** each viewport seeds from its own rider/camera and unwrapped scroll
- **AND** the 2P gate remains output-identical at its tested widths

#### Scenario: 1P unchanged

- **WHEN** the race is 1P on a tour-1 track
- **THEN** the output matches the existing 1P references
- **AND** the seed only changes the Hopper/later-tour latency

### Requirement: Later-tour validation

The system SHALL validate the Hopper PICK TRACK transition, the Hopper race at 16:9 and one second later tour, and SHALL record the first seeded-anchor frame, the BG1 wrong-cell score and the remaining start-overlay mask.

#### Scenario: Hopper menu

- **WHEN** the transition after selecting `WARIO PAINT` is captured at 16:9
- **THEN** the frame is 256 px and the track name has no artifact

#### Scenario: Hopper race

- **WHEN** the Hopper race is captured at the first usable frame and later
- **THEN** the track fills both widescreen sides and the radar appears
- **AND** BG1 wrong served cells are 0
- **AND** the first anchor frame and latency are recorded

#### Scenario: Second later tour

- **WHEN** another later-tour track runs the same route
- **THEN** the header X seed resolves with no wrong cells or the report records the track as an exception

#### Scenario: Existing gate

- **WHEN** `game/tools/validate.sh` runs
- **THEN** the existing 1P/2P frames pass unchanged
- **AND** the 1P `logs` source/park diagnostics may be re-recorded for the cull-gate timing, with its frame hash and wrong-cell score unchanged
- **AND** any new Hopper case is deterministic
