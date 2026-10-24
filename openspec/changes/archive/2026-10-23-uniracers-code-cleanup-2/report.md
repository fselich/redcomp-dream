# uniracers-code-cleanup-2 — report

## Result

The BG1 margin has two policies, `source` (default) and `clamp`; one module (`ws_margin.[ch]`) owns them. The ghost lane, the `ring` and `wrap` policies, the framework hooks that only the ghost lane used, the `$7F` read watch and the dead code found in the audit are gone. Every gate case is bit-identical to the references recorded on the starting commit.

Last commit with the removed code: `05963f1` (ghost lane, five policies, read watch in `cpu_state.c`, per-phase patches and scripts in `tools/`).

## Validation

- The committed references were all `NONDET` (recorded on a host where headless menu timing varied). On `05963f1` three runs of the native case and a double `--record` of every case gave identical hashes, so the references were re-recorded there before any change (design D5). From then on every step compared bit-exactly.
- On `05963f1` the `ghost` case produced the same frame as `wrap` (`101564a8…`) on the validate route: the lane served nothing there.
- Final gate: `native`, `16x9`, `21x9`, `512`, `clamp`, `map-wrap`, `map-ring`, `monster`, `zoomzoo512`, `logs` all PASS. `logs` covers the source, offwindow, park, radar and BG-probe logs and the wrong-cell score.
- `map-wrap` (`SNESRECOMP_WS_OFFWINDOW=wrap`) equals the `clamp` frame and `map-ring` (`=ring`) equals the `16x9` frame.
- The `[ws-ow]` stderr lines of the `logs` case are identical to the starting binary's.
- `tools/scripts/test_ws_settings_roundtrip.sh` passes, with new checks: `ring` → `source`, `wrap` → `clamp`, a mapped `Margin = wrap` line is not rewritten when another option is saved, and the margin cycles `clamp` ↔ `source`.

## CPU (design D8)

2,500 Dragster frames at 16:9, user + system seconds, six runs each:

| Build | `source` mean | `clamp` mean | Difference per frame |
|---|---|---|---|
| `05963f1` | 9.89 s | 9.61 s | ≈ 0.11 ms |
| this change | 9.51 s | 9.35 s | ≈ 0.06 ms |

Both differences are within run-to-run noise (±0.3 s per run) and well under the 0.5 ms budget.

## Deviation from the proposal

The BG pixel-source probe (`SNESRECOMP_BGPROBE_*`) stays in the framework. `validate.sh` uses it in the `logs` case to score wrong margin cells with `world_key_check.py`, the only check that says whether `source` serves correct cells. Decided with the user during implementation; proposal, design D3, the `recomp-code-structure` delta and the tasks were updated, and the planned removal of the `recomp-ws-offwindow` "Pixel-source probe" requirement was dropped. Only the read watch was archived (`tools/patches/research/read-watch.diff`, checked to apply and build).

## Size

| | Before | After |
|---|---|---|
| `game/src` (`*.c`, `*.h`, excluding generated code) | 3,508 lines (426 in the ghost lane) | 3,012 lines |
| `game/patches-snesrecomp-local.diff` | 720 lines, 394 changed | 512 lines, 259 changed |

The regenerated patch was verified by applying it to the upstream files (reconstructed by reverse-applying the old patch) and comparing the result with the vendored tree: identical.

## Code changes

- Deleted `ws_ghost_lane.[ch]`, `WsCullGet`/`WsCullSet`/`WsCullState`, the ghost-only ring accessors, `WsConfigBool`, `WsEnvInt`, the unused `WsEnvSlot.set`, `WsSourceAnchorCount`, `WsSourceNoteServed` and the write-only source counters, the private `Trim` in `ws_settings.c`, the `allow_ghost` plumbing, the unused `snes_netplay_active` declaration in `game_rtl.c`.
- Framework: removed `snes_beam_frame_driver_get_exec`/`set_exec` (and the includes they needed), `ppu_rasterStateSize`/`Save`/`Load`, `read_watch_note`.
- `ws_ring.[ch]` now holds only the seen table, the scroll unwrap and the line observation (`WsRingLine`); `WsRingWalkLine` and its callback are gone.
- New `ws_margin.[ch]`: policy, hook install/remove, BG1 clamp, mask word, tile hook, logs. `GameDrawPpuFrame` calls `WsMarginFrameBegin`/`WsMarginFrameEnd`.
- `ws_settings.c`: margin values `source`/`clamp`; legacy values mapped in `Canonicalize` (design D1).
- Comments: every `openspec/changes/<name>` reference in `game/src` points at the archived path; module headers describe the current design.

## tools/

- Deleted: `tools/patches/ws-*` (merged per-phase diffs, `*.before`, `ppu.c.orig`), `scripts/ws-ghost-lane/`, the captured `.log` files in `scripts/ws-ghost-offscreen/`, `ws-leading-edge/lookahead_coverage.py` (ghost-lane analysis), `ws-leading-edge/capture_hist.py` (broken import path, superseded by `capture_frame.py`), the duplicate `ws-leading-edge/world_key_check.py`, and the native-map spike scripts (`break_*`, `capture_chunks`, `correlate_chunks`, `trace_stage2`, `trace_upload`, `stitch_bg1`, `dump_tables`, `capture_bg1_sequence`, `multi_frame_capture`, `same_run_capture`).
- Kept: routes, `dbg.py`, `capture_frame.py`, `capture_wram_series.py`, the track-decode tools, `world_key_check.py`, `ws-source/validate_all.sh`, the rival OAM recorder and gate scripts (planned use in `uniracers-rival-edge-indicator`), `dis.py`, the save tool and the settings test.
- Fixed: `scripts/ws-ghost-offscreen/run.sh` deleted `game/build-local/saves/save.srm` before each run; it now runs a copy of the executable in a scratch directory.
- `tools/README.md` rewritten for the kept set.

## Left for uniracers-docs-overhaul

- `docs/` still describes five policies, the ghost lane and the removed tools; the root `README.md` was updated only for the margin values.
- Code comments still cite `docs/UNIRACERS_TRACK_FORMAT.md` and `docs/UNIRACERS_SAVE_FORMAT.md`, which exist today and move in the docs change; its link check covers code comments.
- No commits were made; the steps of design D7 are in the working tree.

## Archive note

`recomp-ws-ghost-lane` was retired as a whole capability: `openspec/specs/recomp-ws-ghost-lane/` was deleted by hand because the CLI refuses to write a spec with no requirements. The REMOVED delta under `specs/recomp-ws-ghost-lane/` records the reasons and the migration.
