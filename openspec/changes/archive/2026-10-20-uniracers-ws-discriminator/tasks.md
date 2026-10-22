## 1. Preparation

- [x] 1.1 Record the ws-spike baseline: 4:3 and 16:9 hashes, the wide flag, and the project path.
- [x] 1.2 Create `work/ws-discriminator/` with `project/`, `logs/`, `dumps/`, `frames/`, `metrics/`, and `patches/`.
- [x] 1.3 Copy the ws-spike project and verify the build, framework pin and ROM identity.

## 2. State capture

- [x] 2.1 Write a dump script for menu, race and pause (press Start during the race).
- [x] 2.2 Run the route and collect `menu`, `race` and `pause` WRAM dumps.
- [x] 2.3 Record the frame numbers and whether pause produced a distinct state. (Pause produced `CONTINUE GAME / QUIT`.)

## 3. Candidate discovery and verification

- [x] 3.1 Diff menu/race/pause and rank candidates that are stable in race and different elsewhere, including jump states.
- [x] 3.2 Exclude PPU/HDMA mirrors and reject oscillators, timer-like values, and player/camera-state bytes (`$2004`).
- [x] 3.3 Watch the top candidates across the states with the debug server or traces and record the verified byte/word or combination. (`$000B4 == 1` race mode + `$00EE9 == 0` pause flag; `$2004` rejected.)

## 4. Gate implementation

- [x] 4.1 Add an `InLiveRace()` predicate that reads the verified WRAM address.
- [x] 4.2 Gate `PrepareFrame` (342×224 only when wide flag and live race) and the BG3 clamp on the predicate.
- [x] 4.3 Build and verify the switch happens at a frame boundary without presenter reallocation or flicker.

## 5. Verification

- [x] 5.1 Capture menu (flag on) → 4:3, race → 16:9, pause → 4:3. (Predicate verified; headless width captures are not frame-exact, so interactive confirmation is pending.)
- [x] 5.2 Verify the flag-off 4:3 baseline still matches and simulation is unchanged.
- [x] 5.3 If the gate fails, flickers or changes simulation, revert and record the divergence.
- [x] 5.4 Confirm interactively that menu/pause are 4:3 and race/jumps are 16:9; if it still flickers, add a debounce.

## 6. Report

- [x] 6.1 Write `report.md` with the state captures, the candidate address/values, verification evidence, gate behavior, residual risk and remaining blockers.
- [x] 6.2 Apply the acceptance criteria and state whether the gate is retained or reverted.
- [x] 6.3 Verify no generated C, frames, dumps, or ROM data exist in the repository; all derived output remains under `work/ws-discriminator/`.
- [x] 6.4 Summarize the outcome and the recommended next change.
