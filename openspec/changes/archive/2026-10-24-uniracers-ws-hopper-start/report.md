## Status

Implemented and validated with the local ROM and a scratch copy of the unlocked save under `/tmp/opencode/hopper-bug`. The player's save and config were not touched.

## 1. Hopper PICK TRACK transition

The Hopper route was measured with `SNESRECOMP_UNLOCK_TOURS=1` on an empty scratch save; the PICK TOUR shows all nine tours and three downs select Hopper. At the PICK TRACK screen (frame 1958) the state is `$00B4=0x1F`, `$0300=0x2200`, `$0EE9=0`, cull `$B33C/$BB3C`; the frame is native. Selecting the track drops `$0300` to 0 while the PICK TRACK frame is still displayed; the old `WsInLiveRace()` then accepted the frame and the margin hook ran on the menu BG1 (mode 3, base `0x0200`), showing the PICK TOUR margins and the `WARIO PAINT` artifact.

The new gate requires `(int16_t)$0421 <= 0 && (int16_t)$0423 >= 128` through the shared `WsCullPairLooksLive()`/`WsCullLooksLive()` helper. The race cull `$FFA4/$012B` (1P) and `$FCF0/$1000` (2P) pass; the transition `$B33C/$BB3C` does not. Transition screenshots at frames 1960, 1965, 1970 and 1980 are 256×224; the validation case `hopper-menu` records frame 1960. The race still widens to 342×224.

## 2. Hopper source anchor

The old source log on Hopper: `anchor wait samples=0` until frame 2582, retries with overlay-ish words (`1b24`, `1c00`) from 2582 to 3551, and only `anchor ok ax=72 ay=-14` at frame 3552 (grid `4096x64`, latency 1077). The native track was visible earlier, but the seen table still contained race-start words that did not agree with the table; the general search was stuck.

The header measurements:

| Track | `$7F:0002–0009` | Resolved anchor | Rider table cell | Header X formula |
|---|---|---|---|---|
| Dragster | `0x4400 0x3200` (big-endian x=68, y=50) | ax=52 ay=34 | x=68, y=53 | 68 - 16 = 52 |
| Hopper | `0x5800 0x0200` (big-endian x=88, y=2) | ax=72 ay=-14 | x=88, y=8 | 88 - 16 = 72 |

The header X is the rider/start-line table X. The header Y is the start line, not the rider row (the rider starts 3 cells after on Dragster and 6 on Hopper), so it is not used for the anchor.

Implemented:

- `WsRingScrollValidVp`/`WsRingScrollHVp`/`WsRingScrollVVp` per viewport; the old names remain vp0 wrappers.
- At the race scene reset, `WsSrcFrame` captures the viewport's start world X from `WsRingScrollHVp(vp) + (riderX - camX)`; the seed uses that fixed value, never the moving camera.
- `WsSrcTrySeed` reads the header X as big-endian cells, computes `ax = header_x - start_world_x`, and chooses Y by table agreement over the current frame's verified samples.
- The seed runs only after 10 failed general searches, so a fast track such as Dragster is not preempted; `WsSrcTrySeed` logs `seed try` and, when accepted, `anchor ok seed`.
- The existing self-check still validates the seed; the general `WsSrcSearch` is the fallback.

Measured Hopper result: the first current samples appear at frame 2581; the seed agreement reaches 95 % at frame 2586 (`ax=72 ay=-14`); the race scene reset drops that seed at 2587 and it re-seeds at frame 2598. The race screenshot is at frame 2764, so the margins and radar are filled when the race starts. The source log confirms no retries after 2598. The 16:9 BG1 probe at frames 2790–2810 reports `native_cells=525`, `conflicting_cells=0`, `margin E correct=13565 wrong=0`, `margin W correct=13393 wrong=0`; the radar log has 404 rows.

The absolute first frames remain masked while the race-start overlay has no usable track word (`samples=0` until 2581); that is the game's data limit and is documented. The seed never serves a wrong cell; when it is ambiguous or fails, the old search runs.

## 3. Second later tour

Jumper (one more down on PICK TOUR) resolves with the general search at frame 2741 (`ax=119 ay=-5`, grid `512x512`, latency 99). The screenshot at 2800 is 342×224 and the track and radar are filled. The 21-frame probe at 2790–2810 has zero conflicting/wrong cells but most margin cells are unverifiable because the capture does not include their native ground truth; the visual and radar evidence confirm the fill. The seed is not needed on Jumper but is per-track and does not break it.

## 4. Validation gate

- Full `game/tools/validate.sh`: 18/18 PASS. The 16 previous cases plus:
  - `hopper-menu` (route `game/tools/route-validate-hopper.txt`, frame 1960): 256×224, no artifact;
  - `hopper-16x9` (same route, frame 3200): track/radar filled.
- Both Hopper cases are deterministic; `--record` has no `NONDET`.
- The 1P and 2P frame hashes are unchanged. The cull gate changes the 1P `logs` source/park diagnostic timing, so those two log references were re-recorded; the `logs` frame hash and wrong-cell score (0 conflicts, 0 wrong in both margins) are unchanged.
- The `2p-native` and wide 2P/VS frames are unchanged.

## 5. Residual limits

- The race-start overlay frames before the first usable track samples stay masked; the seed cannot invent a track word. Hopper fills at 2598, before the race screen at 2764, but a capture inside the pre-race overlay still shows the native cut.
- The header Y is not used; the rider start offset is not guessed. Only the X is seeded.
- The second later tour (Jumper) uses the general search, not the seed. Later tours beyond Hopper/Jumper still need the R1 probe.
- The Hopper unlock in validation uses `SNESRECOMP_UNLOCK_TOURS=1` on the scratch save only; the player's save is untouched.

## 6. Decision

**Accept.** The Hopper PICK TRACK transition no longer widens or corrupts the track name, the Hopper race margins and radar fill before the race screen (frame 2598 vs race 2764), the BG1 probe is clean, and the full gate passes 18/18 with the 1P/2P frame hashes unchanged. The only revised references are the 1P source/park diagnostic logs, whose timing changed with the cull gate.
