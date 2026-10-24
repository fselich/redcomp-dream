# recomp-ws-two-player Specification

## Purpose
TBD - created by archiving change uniracers-ws-two-player. Update Purpose after archive.
## Requirements
### Requirement: Two-player live-race detection

The system SHALL treat a 2P or VS split-screen race as a live race when `$0DDB != 0`, `$0EE9 == 0` and `$0300 <= 1`, independently of `$00B4`. It SHALL NOT treat menus, PICK screens, tour/track selection, the attract mode, pause or the finish as a live race. The 1P result of `WsInLiveRace()` SHALL be unchanged.

#### Scenario: 2P race

- **WHEN** `$0DDB` is nonzero, `$00B4` is zero, `$0EE9` is zero and `$0300` is `0` or `1`
- **THEN** `WsInLiveRace()` is true and the widescreen path is eligible

#### Scenario: VS race

- **WHEN** the game is in VS split screen and the same words hold
- **THEN** `WsInLiveRace()` is true by the same branch

#### Scenario: 2P menu

- **WHEN** `$0DDB` is zero and `$0300` is a menu value such as `$4C`
- **THEN** `WsInLiveRace()` is false and the frame stays 256 px

#### Scenario: Pause or finish

- **WHEN** `$0EE9` is nonzero
- **THEN** `WsInLiveRace()` is false and the native frame is restored

#### Scenario: 1P unchanged

- **WHEN** `$0DDB` is zero
- **THEN** `WsInLiveRace()` returns the same value as before this change for every frame of the 1P validation routes

### Requirement: Per-half frame width

The system SHALL widen each half of a live 2P/VS race by the same `E` on each side, SHALL keep the frame height at 224 and the split at line 112, and SHALL derive `E` from the existing aspect and maximum-width settings.

#### Scenario: 16:9

- **WHEN** the target aspect is 16:9 and the race is live
- **THEN** the frame is 342×224 and each half is 342×112

#### Scenario: 21:9

- **WHEN** the target aspect is 21:9
- **THEN** the frame is 448×224 and each half is 448×112

#### Scenario: 512 px

- **WHEN** the target needs 512 px
- **THEN** the frame is 512×224 and each half is 512×112
- **AND** the 9-bit OAM X cap is not exceeded

#### Scenario: Widescreen off

- **WHEN** `[Graphics] Widescreen` is `0`
- **THEN** the 2P frame is the native 256×224

#### Scenario: Non-live screen

- **WHEN** a 2P menu or selection screen is shown
- **THEN** the frame is 256×224

### Requirement: Scanline viewports

The system SHALL assign each scanline of a live race to a viewport: viewport 0 for every line in 1P; in 2P/VS viewport 0 for the top half, viewport 1 for the bottom half, and no viewport for the divider lines. The line boundaries SHALL be the measured lines on which each half's BG1 state is in effect, named as constants, and SHALL be recorded in the change report.

#### Scenario: 1P

- **WHEN** `$0DDB` is zero
- **THEN** every line maps to viewport 0

#### Scenario: 2P top and bottom

- **WHEN** `$0DDB` is nonzero
- **THEN** lines of the top half map to viewport 0, lines of the bottom half to viewport 1, and divider lines to no viewport

#### Scenario: Diagnostic log

- **WHEN** `SNESRECOMP_WS_2P_LINE_LOG` names a file in a live 2P race
- **THEN** each drawn line is logged with frame, line, viewport, BG1 map base, BG mode, BG1 scroll and main-screen layers

### Requirement: BG1 margins in both halves

The system SHALL apply the configured BG1 margin policy (`source` or `clamp`) to both halves. With `source`, ring observation, scroll unwrap, scene reset, seen table and anchor SHALL be kept per viewport, and each margin sample SHALL be decided with the state of its line's viewport. It SHALL NOT draw a margin cell that differs from the track-table ground truth, and SHALL mask cells whose source is not valid.

#### Scenario: Both halves served

- **WHEN** `source` is active and both halves are scored with the BG1 probe per viewport
- **THEN** wrong served cells are zero in the left and right margin of each half
- **AND** masked counts and anchor latency are reported per viewport

#### Scenario: Cameras far apart

- **WHEN** the two cameras are more than 512 px apart in X or Y
- **THEN** each viewport keeps its own unwrapped scroll and anchor
- **AND** no bottom-half observation changes a top-half seen entry or anchor, and vice versa

#### Scenario: Bottom half on another map base

- **WHEN** the bottom half's BG1 map base differs from the top half's
- **THEN** neither viewport's scene is reset by the other's map base
- **AND** the bottom margins pass the map check against the bottom viewport's map base

#### Scenario: Divider lines

- **WHEN** a line belongs to no viewport
- **THEN** it is neither observed nor remapped by the margin hooks

#### Scenario: Clamp

- **WHEN** the policy is `clamp`
- **THEN** BG1 stays native-width in both halves

#### Scenario: Native track

- **WHEN** widescreen is off
- **THEN** BG1 matches the native 256-px output

### Requirement: HUD in both halves

The system SHALL move the top and bottom `RACE` groups 4 px inside the left edge and the timers and player names to the right edge, SHALL stretch the split divider across the widened frame, and SHALL keep the centred prompt rows in place, with every glyph copied 1:1 and no BG3 content drawn in the margins outside those groups.

#### Scenario: Top HUD

- **WHEN** the wide 2P frame is drawn
- **THEN** the top `RACE` group starts 4 px inside the left edge and the top timer ends at the right edge
- **AND** no glyph is resampled or clipped

#### Scenario: Bottom HUD

- **WHEN** the wide 2P frame is drawn
- **THEN** the bottom `RACE` group and bottom timer move by the same amounts, with the +112 vertical offset preserved

#### Scenario: Player names

- **WHEN** a player name is drawn in either half
- **THEN** it is copied 1:1 at its native x plus `E`

#### Scenario: Divider

- **WHEN** the split divider is drawn
- **THEN** it spans `[-E, 256+E)` with no seam or gap

#### Scenario: Centred prompts

- **WHEN** a centred prompt is drawn
- **THEN** it stays at its native x, which is still the centre of the half

#### Scenario: HUD switch off

- **WHEN** `[Widescreen] Hud` is `0`
- **THEN** BG3 is clamped to the native 256 px in both halves
- **AND** the cull widening stays active

### Requirement: Rider OAM in both halves

The system SHALL draw each real rider at its true screen X in either widened margin for all four split-screen rider slots (96 P2 bottom, 97 P1 bottom, 98 P1 top, 99 P2 top), using the camera of the slot's half. A frame-level pass SHALL hide the per-half parking sentinels and fix the missing X high bit. A per-line high-table pass SHALL set the line's half rider slots to the big size bit and the true X high bit before the line is drawn, because the single end-of-frame high table stages the bottom entries small. Both passes SHALL modify only the PPU OAM copy and SHALL NOT apply the 1P parked-pair or `$170,$70` sentinel rules in 2P.

#### Scenario: P1 in the top right margin

- **WHEN** P1's true top-half screen X is in `[256, 256+E)` and slot 98 was uploaded without the X high bit
- **THEN** P1's rider is drawn at that X in the top half's right margin
- **AND** no rider appears at `X-256` on the left

#### Scenario: P2 in the bottom right margin

- **WHEN** P2's true bottom-half screen X is in `[256, 256+E)`
- **THEN** P2's rider is drawn at that X in the bottom half's right margin

#### Scenario: Bottom half draws its rider

- **WHEN** the bottom half is drawn with its own rider inside the bottom camera's window
- **THEN** the bottom rider is drawn big at its true X/Y
- **AND** it is not missing because the end-of-frame high table staged it small

#### Scenario: Per-line high table

- **WHEN** a bottom line is drawn and a bottom slot is a real rider
- **THEN** the PPU high table's size bit for that slot is 1 before the line is drawn
- **AND** the X high bit matches the true WRAM X
- **AND** no guest memory is written

#### Scenario: Rider in the left margin

- **WHEN** a real rider's screen X is in `[-E, 0)`
- **THEN** the game's high bit is preserved and the rider is drawn at the true X

#### Scenario: Culled rider

- **WHEN** a slot carries its half's sentinel (`$70,$70` top, `$30,$30` bottom, with the measured high bit)
- **THEN** that slot is moved below the field in the PPU copy without consulting WRAM
- **AND** no part of it is drawn in either margin at any `E` up to 128

#### Scenario: Rider seen by the other camera

- **WHEN** P2 is inside the top camera's widened window
- **THEN** slot 99 is treated as a real rider in the top half, with the top camera

#### Scenario: Unrecognised slot

- **WHEN** a slot's tile and attribute do not match a rider entry
- **THEN** the slot is left unchanged and logged

### Requirement: Cull widening reuse

The system SHALL widen the shared rider cull window by `E << $03ED` on each side in a live 2P/VS race and SHALL restore the game's values when widescreen or the race ends.

#### Scenario: 2P cull

- **WHEN** `$03ED` is 4 and `E` is 43
- **THEN** the cull window covers `[-92, 299]` screen pixels

#### Scenario: Cull off

- **WHEN** `[Widescreen] Cull` is `0`
- **THEN** the native cull window is used
- **AND** the frame may still be wide

#### Scenario: Race ends

- **WHEN** the race stops being live
- **THEN** the native cull values are restored

### Requirement: Two-player gating and radar

The system SHALL apply the existing `Widescreen`, `Hud`, `Margin` and `Cull` switches independently in 2P/VS and SHALL NOT draw the radar in 2P/VS. It SHALL NOT add a new user option for this feature.

#### Scenario: Radar configured on

- **WHEN** `[Video] MiniMap` is `1` and the game is in 2P split screen
- **THEN** no radar panel is drawn

#### Scenario: Independent switches

- **WHEN** `Hud` is off and `Cull` is on in 2P
- **THEN** the wide frame and rider cull stay active
- **AND** BG3 is native-clamped

#### Scenario: No new option

- **WHEN** the settings model and menu are inspected
- **THEN** no 2P-specific option row or key exists

### Requirement: 1P, 4:3, menus and simulation preserved

The system SHALL keep 1P widescreen output identical to the references recorded before this change, SHALL keep 1P native 4:3, menus, PICK screens, NOW PLAYING, countdown, pause and finish identical to the baseline, and SHALL NOT change RNG, progression, collision, camera or input. In native 2P 4:3 the bottom half SHALL show its rider (the pre-change port dropped it because the bottom OAM entries were staged small); the native 2P reference is re-recorded for that fix.

#### Scenario: 1P references

- **WHEN** `game/tools/validate.sh` runs the existing 1P cases
- **THEN** every frame hash equals the reference recorded before this change

#### Scenario: Native 4:3

- **WHEN** widescreen is off
- **THEN** the 2P frame matches the re-recorded native 2P reference
- **AND** the bottom half shows its rider, not an empty window
- **AND** every 1P native frame still matches its pre-change reference

#### Scenario: Guest state

- **WHEN** a 2P frame is captured with widescreen on and off
- **THEN** WRAM, VRAM and CGRAM are identical except the cull words
- **AND** SRAM is unchanged

#### Scenario: Simulation

- **WHEN** the same 2P input route runs with widescreen on and off
- **THEN** rider positions, cameras, RNG and progression match

### Requirement: Two-player validation

The system SHALL validate 2P at native, 16:9, 21:9 and 512 px and with `clamp`, and VS at 16:9, on the tour-1 tracks plus Zoom Zoo and one looping track, and SHALL produce a report with the measured viewport lines, the BG1 wrong-cell score per viewport, HUD and OAM evidence, and an accept/partial/revert decision.

#### Scenario: Width matrix

- **WHEN** the 2P cases run at the four widths
- **THEN** every case captures both halves
- **AND** the frame widths match the expected `E+256+E`

#### Scenario: Wrong cells

- **WHEN** the BG1 probe is scored per viewport for a 2P wide case
- **THEN** wrong served cells are zero in each half
- **AND** residual masked cells are recorded as evidence, not failure

#### Scenario: OAM evidence

- **WHEN** the rider OAM log is reviewed
- **THEN** real riders are visible in the margins of both halves
- **AND** no sentinel or ghost is visible

#### Scenario: Report

- **WHEN** the matrix finishes
- **THEN** the report states accept, partial or revert
- **AND** names any residual limitation such as anchor latency or a VS difference

### Requirement: Two-player documentation

The system SHALL document the feature in `docs/TWO_PLAYER.md`: detection, layout, per-viewport BG1, HUD bands, the four-slot OAM policy, cull, how each part was measured, decisions, rejected alternatives and limits. The document SHALL be linked from `docs/README.md`, `docs/WIDESCREEN.md` and `docs/TRACK_MARGIN.md`, SHALL contain no ROM-derived data, and SHALL pass the docs link checker.

#### Scenario: Entry points

- **WHEN** a reader opens `docs/README.md`
- **THEN** `TWO_PLAYER.md` is listed and the "not adapted" limitation is gone

#### Scenario: Link check

- **WHEN** `tools/scripts/docs/check_links.py` runs
- **THEN** it reports no broken link

