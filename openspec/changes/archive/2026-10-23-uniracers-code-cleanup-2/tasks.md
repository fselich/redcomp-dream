## 1. Baseline

- [x] 1.1 Build the starting commit and run `game/tools/validate.sh`; every case must pass against the current `validate_refs.txt`, then re-record the references on that commit (design D5)
- [x] 1.2 Measure CPU time for 2,500 Dragster frames in `source` and in `clamp` on the starting commit; keep the figures for the report (design D8)
- [x] 1.3 Note the starting commit hash in the change report as the last commit with the ghost lane, the five policies and the framework probes

## 2. Remove the ghost lane

- [x] 2.1 Delete `game/src/ws_ghost_lane.[ch]`, remove them from `game/CMakeLists.txt` and the module list in `game_rtl.c`
- [x] 2.2 Remove `WsGhostAfterRunFrame` from `GameAfterRunFrame` (keep `SaveUnlockTick` and its comment, reworded without the ghost lane)
- [x] 2.3 Remove the `WS_OW_GHOST` branch from the tile hook and `WsOffwindowHooked`, and the `allow_ghost` path from `Canonicalize`/`Store` in `ws_settings.c`
- [x] 2.4 Delete `WsCullGet`/`WsCullSet`/`WsCullState` and the ghost-only ring accessors (`WsRingSceneValid`, `WsRingMapBase`, `WsRingPrevBgMode`, `WsRingScrollSeed`)
- [x] 2.5 Remove `snes_beam_frame_driver_get_exec`/`set_exec` and `ppu_rasterStateSize`/`Save`/`Load` from the framework tree
- [x] 2.6 Drop the `ghost` row from `validate.sh` and `validate_refs.txt`; run the gate

## 3. Remove the framework research probe

- [x] 3.1 Save the current `read_watch_note` hunks of `cpu_state.c` as `tools/patches/research/read-watch.diff`, with a header naming the experiments that used it (track format, native-map spike) and how to apply it
- [x] 3.2 Remove `read_watch_note` and its calls from `cpu_state.c`, restoring the upstream read paths
- [x] 3.3 Keep the BG pixel-source probe in `ppu.c` (used by the gate's `logs` case, design D3)
- [x] 3.4 Check that `read-watch.diff` applies cleanly to the cleaned tree and builds (then revert)
- [x] 3.5 Run the gate

## 4. Two policies

- [x] 4.1 Reduce the margin values in `ws_settings.c` to `source`/`clamp` with labels `SOURCE`/`CLAMP`, and implement the mapping of design D1 in `Canonicalize`
- [x] 4.2 Remove `WS_OW_RING` and `WS_OW_WRAP`; the policy function returns `source` or `clamp`
- [x] 4.3 Check that saving from the menu does not rewrite a mapped `Margin = wrap` line when the margin was not changed
- [x] 4.4 Drop the `wrap` and `ring` rows from `validate.sh`/`validate_refs.txt`; add the mapping cases (`OFFWINDOW=wrap` hashes as `clamp`, `OFFWINDOW=ring` as `source`)
- [x] 4.5 Update `ws_menu.c` help text if it lists the margin values; run `tools/scripts/test_ws_settings_roundtrip.sh` and the gate

## 5. Margin module

- [x] 5.1 Create `ws_margin.[ch]` with `WsMarginPolicy`, `WsMarginFrameBegin(const WsFrameCtx *)`, `WsMarginFrameEnd`, `WsMarginReset`, the tile hook, the mask word and the scene reset (design D2)
- [x] 5.2 Move the hook install/remove and the BG1 clamp out of `GameDrawPpuFrame` into the frame-begin/end calls
- [x] 5.3 Leave `ws_ring.[ch]` with the seen table, scroll unwrap and line observation; make `WsRingWalkLine` write the seen table directly (no callback)
- [x] 5.4 Rename `WsOffwindow*`, `WsOw*`, `WS_OW_*` to margin names; keep the `[ws-ow]` stderr text, the `SNESRECOMP_WS_OFFWINDOW*` variable names and the CSV format unchanged
- [x] 5.5 Update `WsSettingsOnChange` to call `WsMarginReset`; run the gate

## 6. Dead code and comments

- [x] 6.1 Delete `WsConfigBool`, `WsSourceAnchorCount`, `WsSourceNoteServed` and the write-only `s_src_*` counters, `WsEnvInt` if unused, and the private `Trim` in `ws_settings.c` (use `WsTrim`)
- [x] 6.2 Rewrite module header comments to describe what each module does now (no "test-only prototype", no `Option A`, no removed policies)
- [x] 6.3 Replace every `openspec/changes/<name>` reference in `game/src/`, `game/tools/` and `tools/` with `openspec/changes/archive/<date>-<name>` or a `docs/` path
- [x] 6.4 Update the module list at the top of `game_rtl.c`; run the gate

## 7. Framework patch

- [x] 7.1 Regenerate `game/patches-snesrecomp-local.diff` from `game/snesrecomp/` against the pin in `framework_pins.txt`
- [x] 7.2 Check that it contains none of `snes_beam_frame_driver_get_exec`, `ppu_rasterState`, `read_watch_note`

## 8. tools/

- [x] 8.1 Delete `tools/patches/ws-*` (merged per-phase diffs, `*.before`, `ppu.c.orig`), keeping `tools/patches/research/`
- [x] 8.2 Delete `tools/scripts/ws-ghost-lane/`, `tools/scripts/ws-ghost-offscreen/` and the captured `.log` files; remove ghost-only scripts from `ws-leading-edge/`
- [x] 8.3 Check every kept script and route against design D6 (used by the gate, a documented command, or planned work); update `ws-source/validate_all.sh` to the two policies
- [x] 8.4 Rewrite `tools/README.md` for the kept set, one row per directory with its purpose

## 9. Close

- [x] 9.1 Final gate run; measure `source` vs `clamp` CPU time and record it against the < 0.5 ms budget
- [x] 9.2 Update the root `README.md` margin line (`Margin = source ; source, clamp`) and the options list; leave the rest of the docs to `uniracers-docs-overhaul`
- [x] 9.3 Write `report.md`: removed code with the last commit that has it, line counts before/after for `game/src/` and the framework patch, gate results, CPU measurement
