## Why

Two Hopper-tour problems appear only on a later tour:

- **PICK TRACK is widened when it should not be.** At the Hopper track selection the game leaves `$0300 = 0` while the PICK TRACK screen is still visible, and `$00B4 = 0x1F`. The live-race predicate accepts it, the wide frame and the BG1 margin hook run on the menu (mode 3, not the race BG1 map), and the PICK TOUR margins/`WARIO PAINT` name are corrupted.
- **The Hopper race track is cut at the widescreen sides until the player advances.** The source anchor needs the seen table. On Hopper the race-start overlay has no usable table words (`anchor wait samples=0`), and then the general search is ambiguous; on a measured run the anchor only appeared at frame 3552 (`ax=72 ay=-14`, latency 1077). The track and radar are therefore masked for many seconds. Tour 1 resolves in about 100 frames; Hopper is an unvalidated later tour (roadmap R1).

The user asks for the track to fill from the first usable frame (ideally the first frame), and the menu never to widen.

## What Changes

- **Cull-plausibility gate.** `WsInLiveRace()` SHALL also require a plausible race cull window (`(int16_t)$0421 <= 0 && (int16_t)$0423 >= 128`). The Hopper PICK TRACK transition has `$0421/$0423 = 0xB33C/0xBB3C` and is excluded; the race uses `0xFFCF/0x0100` (1P) or `0xFCF0/0x1000` (2P) and stays live. The helper is shared with the existing check in `WsApplyCull()`.
- **Start-seeded source anchor.** At the first live frame with a valid `$7F` header and scroll, the port SHALL derive the X anchor from the header start pair (`$7F:0002–0009`, big-endian table cells) and the viewport's rider screen X + scroll; it SHALL find the Y anchor from the first verified samples by table agreement, then keep the existing self-check and fallback to the general search. This is per viewport in 2P and uses the track header for every track, not a Hopper constant.
- **Measured target.** On Hopper the seed is expected to resolve at the first frame with usable track samples (frame 2582 in the measured run, about 107 frames after race start) instead of 3552. The start overlay may still mask the absolute first frames; the change records that limit. If the header seed disagrees with the self-check, it is dropped and the old search runs.
- **Validation.** A Hopper route with an unlocked scratch save checks the PICK TRACK transition is 256 px and artifact-free, and the race margins are filled at the first usable frame with zero wrong cells. A second later tour checks the seed is general. The existing 1P/2P frames stay unchanged; the 1P `logs` source/park diagnostics are re-recorded because the cull gate changes the live-start timing, with the frame hash and wrong-cell score unchanged.
- **Documentation.** `WIDESCREEN.md`, `TRACK_MARGIN.md`, `VALIDATION.md` and roadmap R1 describe the new gate, the start seed and the remaining later-tour coverage.

## Capabilities

### New Capabilities

- `recomp-ws-later-tour-start`: live-race cull plausibility, start-seeded source anchor, Hopper/later-tour validation.

### Modified Capabilities

- `recomp-ws-source`: the anchor can be seeded from the `$7F` header and the first verified samples, with the existing self-check/fallback.

## Impact

- `game/src/ws_frame.c`, `game/src/ws_cull.[ch]`: shared cull plausibility.
- `game/src/ws_ring.[ch]`: viewport scroll accessors for the seed.
- `game/src/ws_source.c`: header start seed and Y search.
- `game/tools/route-validate-2p-...`? New Hopper route under `game/tools/`.
- `game/tools/validate.sh`, `validate_refs.txt`: Hopper transition/race cases.
- `docs/WIDESCREEN.md`, `TRACK_MARGIN.md`, `VALIDATION.md`, `ROADMAP.md`.
- No new guest write; the cull change remains the only one.
