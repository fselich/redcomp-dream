## Why

On a 1P Hopper race (PICK TOUR three downs, WARIO PAINT), holding right and holding B at about one second leaves the rider airborne/stunting for roughly 4.5 seconds. During that state the BG1 self-check disagrees with the source anchor (`15/32`, then `0/32`) and the old first-failure drop removes the anchor at frame ~2841; the seed only re-converges at frame ~3113. While the anchor is gone the 21:9 right margin is masked from the 4:3 edge out, so the track visibly ends at the edge and the extra widescreen width is empty. The same track gap also produces a shorter ~19-frame disagreement at frame ~3095 without any B input; the 30-frame grace already covers that case, but it does not cover the airborne stunt.

## What Changes

- **Airborne hold.** While the game reports the rider airborne (`$7E:0BE9 != 0`, the state in which the jump moves the BG1 rows and the camera Y is negative), an anchor whose agreement falls below 95 % SHALL be kept and the source table SHALL continue to serve the margins. The hold is logged as `hold air`.
- **Ground grace stays.** When the rider is on the track, a below-95 % check is still held for up to 30 consecutive frames and dropped on the 30th, so a genuinely wrong anchor cannot persist. A passing check resets the counter. A BG1 map-base/BGMODE change, a non-live race or an invalid header still invalidates immediately.
- **Hopper jump validation.** The `hopper-jump` route holds right and then holds B for 30 frames at about one second; at 21:9 frame 3000 the no-hold build ends the track at the 4:3 edge while the fixed build fills the full width. The case records the source and off-window logs so a regression shows as a drop plus masked race cells.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `recomp-ws-source`: the continuous self-check holds an anchor through an airborne/stunt disagreement for the whole airborne state, then resumes the 30-frame grace on the ground.

## Impact

- `game/src/ws_source.c`: airborne hold branch and log.
- `game/src/variables.h`: `kRamAirborne` (`$7E:0BE9`).
- `game/tools/route-validate-hopper-jump.txt`, `game/tools/validate.sh`, `game/tools/validate_refs.txt`: the hold-B route and the frame-3000 case.
- `docs/TRACK_MARGIN.md`, `docs/VALIDATION.md`.
- No new guest write; the source is presentation-only.
