## 1. Measurement

- [x] 1.1 Record the Hopper PICK TRACK rest and transition state: `$00B4=0x1F`, `$0300=0x2200` at rest then `0`, cull `0xB33C/0xBB3C`; race cull `0xFFA4/0x012B`
- [x] 1.2 Record the Hopper source log: `anchor wait samples=0` until frame 2582, retries until frame 3552, `ax=72 ay=-14`, grid `4096x64`, latency 1077
- [x] 1.3 Record the `$7F:0002–0009` header pairs on Dragster and Hopper and compare them with the rider table cell and the resolved anchor; confirm the big-endian cell interpretation and the X formula
- [x] 1.4 Repeat on a second later tour (Jumper): the general search finds `ax=119 ay=-5` at frame 2741 (latency 99); the track and radar fill, the 21-frame probe has no wrong cells and is unverifiable for most margin cells because the capture is short

## 2. Cull guard

- [x] 2.1 Add `WsCullPairLooksLive()`/`WsCullLooksLive()` to `ws_cull.[ch]` (`(int16_t)l <= 0 && (int16_t)r >= 128`)
- [x] 2.2 Reuse the pair helper in `WsApplyCull()` and require `WsCullLooksLive()` in `WsInLiveRace()`
- [x] 2.3 Verify 1P race, 2P race, Hopper PICK TOUR/PICK TRACK and the transition; the menu stays 256 and the race still widens

## 3. Source start seed

- [x] 3.1 Add per-viewport scroll accessors (`WsRingScrollValidVp`, `WsRingScrollHVp`, `WsRingScrollVVp`) and keep the viewport-0 wrappers
- [x] 3.2 Read the header start pair as big-endian table cells and compute the seeded `ax` from the rider/camera world X captured at the race scene reset (not the moving camera)
- [x] 3.3 Implement the current-frame samples Y search at the seeded `ax` after 10 failed general searches; install the seed only when it agrees; fall back to `WsSrcSearch`
- [x] 3.4 Verify Dragster and Hopper reproduce `ax=52` and `ax=72`; Hopper seeds `ay=-14` at frame 2586, the self-check drops it on the race scene reset, it re-seeds at 2598 (before the race frame 2764) and the margins/radar fill; no wrong margin cells
- [x] 3.5 Verify 1P frames and 2P widths unchanged; the cull gate changes the 1P `logs` source/park diagnostic timing, so those references were re-recorded while the frame hash and wrong-cell score stayed unchanged

## 4. Routes and validation

- [x] 4.1 Add `game/tools/route-validate-hopper.txt`; it uses `SNESRECOMP_UNLOCK_TOURS=1` on the empty validation save (no player save is touched)
- [x] 4.2 Add `hopper-menu` (frame 1960, 256 px, no artifact) and `hopper-16x9` (frame 3200, 342 px, track/radar) to `validate.sh`; record refs (no NONDET)
- [x] 4.3 Run the BG1 probe on Hopper at frames 2790–2810 (first race frame 2764): wrong cells 0, conflicting cells 0, both margins served; the radar log is present
- [x] 4.4 Run the Jumper route: the general search resolves at 2741, the track/radar fill; the seed is not needed on that track, but the seed formula is per-track and does not break it
- [x] 4.5 Run the full gate: 18/18 PASS (the 16 previous cases plus the two Hopper cases); the 1P frame hashes and wrong-cell score are unchanged

## 5. Docs and archive

- [x] 5.1 Update `docs/WIDESCREEN.md`, `docs/TRACK_MARGIN.md`, `docs/VALIDATION.md`, `docs/CONFIG.md` if needed and `docs/ROADMAP.md` R1
- [x] 5.2 Write `report.md` with the Hopper state, source log, header table, seed formula, first-usable-frame limit, validation and decision
- [x] 5.3 Archive the change and commit
