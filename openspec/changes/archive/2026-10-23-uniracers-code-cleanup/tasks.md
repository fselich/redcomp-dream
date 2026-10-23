## 1. Validation gate (before any code change)

- [x] 1.1 Write `game/tools/validate.sh`: copy the executable into a scratch directory with an empty `saves/`, run the cases headless (`SDL_VIDEODRIVER=dummy`, `--no-launcher`, `SNESRECOMP_RUN_FRAMES`, `SNESRECOMP_SCREENSHOT*`), hash the frames and logs, and compare against `game/tools/validate_refs.txt`
- [x] 1.2 Add the cases from design D7: native, 16:9, 21:9, 512 px; `wrap`/`ring`/`clamp`/`ghost` at 16:9; Monster 16:9; Zoom Zoo 512 px; the source, offwindow, park and radar logs
- [x] 1.3 Implement `--record` with the double run; non-deterministic cases are listed and fall back to the `world_key_check.py` score
- [x] 1.4 Check that a run leaves `game/build-local/saves/` and `game/build-local/config.ini` unchanged (content and mtime)
- [x] 1.5 Record the references on the current commit; confirm native = `3417f91a…df44` and 16:9 = `4b138431…e357`; note the commit hash in `validate_refs.txt`

## 2. Remove the research code

- [x] 2.1 Delete `WsTestGen`, `WsTestStage2`, `WsTestStage2Scan{,2,3,4}`, `WsTestStage2Keys` and their calls in `GameRunOneFrame`
- [x] 2.2 Delete `WsTrackRepeatEnabled`, `WsRepeatBegin`, `WsRepeatEnd` and their calls in `GameDrawPpuFrame`
- [x] 2.3 Note in the change report the last commit that still has them; run `validate.sh`

## 3. Names, predicate and context

- [x] 3.1 Fill `variables.h` with `kRam*` constants (riders, camera, cull window, `$03ED`, race-state bytes, 2P flag, `$7F` table header/grid/library) and the `static inline` 16-bit read/write helpers; replace the literals in `game_rtl.c`
- [x] 3.2 Create `ws_frame.[ch]`: `WsWideEnabled`, `WsInLiveRace`, `WsExtra`, `WsFrameCtx`, `WsEnvFlag`/`WsEnvInt`/`WsEnvStr` with per-switch caching, `WsParseBool`
- [x] 3.3 Make `main.c` use `ws_frame` and delete its `WideEnabled`/`InLiveRace` copies
- [x] 3.4 Replace every `getenv` in the port with the cached helpers (keep `g_config` reads live); run `validate.sh`
- [x] 3.5 Build `WsFrameCtx` once at the top of `GameDrawPpuFrame` and use it in every branch there; run `validate.sh`

## 4. Split into modules (pure moves)

- [x] 4.1 Add the new sources to `game/CMakeLists.txt`
- [x] 4.2 Move lazy log opening into `ws_debug.[ch]` (`WsLogOpen`) without changing headers or formats; run `validate.sh`
- [x] 4.3 Move the cull window into `ws_cull.[ch]` with an accessor for the ghost lane; header states it is the only guest-memory writer; run `validate.sh`
- [x] 4.4 Move the HUD band into `ws_hud.[ch]` (elastic band + clamp band from `GameDrawPpuFrame`); run `validate.sh`
- [x] 4.5 Move the OAM X-high/parked fix and `WsInjectTestOam` into `ws_oam_fix.[ch]`; run `validate.sh`
- [x] 4.6 Move the policy parsing, ring observation, line hook and margin tile hook into `ws_ring.[ch]`, with accessors for the unwrapped scroll and scene generation; run `validate.sh`
- [x] 4.7 Move the `$7F` table reader and anchor search into `ws_source.[ch]`, with accessors for the anchors; run `validate.sh`
- [x] 4.8 Move the radar and its `MiniMap` config read into `ws_radar.[ch]`; run `validate.sh`
- [x] 4.9 Move the ghost lane into `ws_ghost_lane.[ch]`; rename `GameGhostAfterRunFrame` to `GameAfterRunFrame` in `game_rtl.[ch]` and `main.c`; run `validate.sh` including the `ghost` case

## 5. Deduplicate and tidy

- [x] 5.1 `WsWrapDelta10` replaces the three 10-bit delta copies
- [x] 5.2 One ring-line walk shared by the real and ghost lanes
- [x] 5.3 `WsOamSlot` helpers for the three OAM loops
- [x] 5.4 Single `static WsCullState`; drop the loose `s_cull_*` statics
- [x] 5.5 Fix the stale radar comment (64×32 panel, 128×64-cell window), the indentation at the old `game_rtl.c:987`, the unused `line` parameter of the line-scroll helper; use `uint16_t`/`int16_t` throughout the port
- [x] 5.6 Replace the scaffold header of `game_rtl.c` with one describing this port and pointing at the modules; give each module a header saying what it owns and whether it touches guest state
- [x] 5.7 Run `validate.sh`; build with `-DSNESRECOMP_ENABLE_TRACE=ON` too, to check that the trace-only path still compiles

## 6. Close

- [x] 6.1 Confirm no `getenv` remains outside `ws_frame.c` and no `g_ram` write remains outside `ws_cull.c` (the ghost lane's snapshot restore excepted)
- [x] 6.2 Update `README.md` / `docs/UNIRACERS_MEJORAS.md` (A3, A4, A8, D14 done or partly done) and mention `validate.sh` in the build section
- [x] 6.3 Write `report.md`: line counts before/after, the `validate.sh` result table and the last commit with the removed probes
