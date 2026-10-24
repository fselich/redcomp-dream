## Why

The first cleanup (`uniracers-code-cleanup`) split the port into modules but kept every experiment alive: five BG1 margin policies (`wrap`, `ring`, `source`, `ghost`, `clamp`), the speculative ghost lane and the research instrumentation in the local framework patch. Only `source` (the default) and `clamp` (the native-width fallback) are worth keeping; the rest is code nobody runs, accessors that exist only for the ghost lane, a hot-path read watch in the CPU core and a larger framework patch to carry on every upstream refresh.

## What Changes

- **BREAKING** `[Widescreen] Margin` and `SNESRECOMP_WS_OFFWINDOW` accept only `source` and `clamp`. Old values are mapped instead of rejected: `ring` and `ghost` resolve to `source`, `wrap`, `0` and `off` resolve to `clamp`. `SNESRECOMP_WS_TRACK_CLAMP` keeps meaning `clamp`.
- Remove the `ring` and `wrap` policies as selectable modes. The ring observation (seen table, scroll unwrap) stays: it is the evidence `source` anchors on.
- Remove the ghost lane: `ws_ghost_lane.[ch]`, its call in `GameAfterRunFrame`, `WsCullGet`/`WsCullSet`, the `SNESRECOMP_WS_GHOST_*` switches and the environment-only `ghost` value.
- Remove the research instrumentation from the framework patch: the `$7F` read watch in `cpu_state.c` (`SNESRECOMP_READ_WATCH_*`), archived as `tools/patches/research/read-watch.diff` so the documented experiments stay reproducible. The BG pixel-source probe in `ppu.c` (`SNESRECOMP_BGPROBE_*`) stays: the validation gate uses it to score wrong margin cells, so it is a validation tool, not research code.
- Remove the framework hooks that only the ghost lane used: `snes_beam_frame_driver_get_exec`/`set_exec` and `ppu_rasterStateSize`/`Save`/`Load`. Regenerate `game/patches-snesrecomp-local.diff`.
- Restructure the margin code: the policy, the per-frame hook installation (today in `game_rtl.c`) and the tile hook move into one margin module; `ws_ring` keeps only the ring observation. `WsOffwindow*`/`WsOw*`/`WS_OW_*` names become margin names.
- Delete dead code found in the audit: `WsConfigBool`, `WsSourceAnchorCount`, the write-only source counters, the ghost-only ring accessors, the private `Trim` copy in `ws_settings.c`, `WsEnvInt` once the ghost lane is gone.
- Update comments that point at `openspec/changes/<name>` to the archived path or to `docs/`.
- Validation gate: drop the `wrap`, `ring` and `ghost` cases; every remaining case keeps its recorded reference. The references are re-recorded once on the starting commit: the committed ones were all `NONDET`, while this host now runs every case deterministically.
- Prune `tools/`: keep what is used for current development (routes, save tool, settings test, track-decode tools, source validation, frame capture helpers); delete per-phase patches already merged, `*.before`/`*.orig` copies, captured logs and ghost-lane scripts.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `recomp-ws-source-default`: the alternative policies shrink to `clamp`; legacy values are mapped.
- `recomp-ws-settings`: the `Margin` option has two values.
- `recomp-code-structure`: the refactor gate covers `source` and `clamp` only; the research-probe removal extends to the ghost lane and the framework instrumentation; the framework patch carries only port hooks.
- `recomp-ws-ghost-lane`: all requirements removed.
- `recomp-ws-leading-edge`: the verified-ring rule applies inside `source`; the dedicated switch no longer offers `ring`.
- `recomp-ws-source`: the fallback when no anchor exists is the mask, no longer described as "`ring` mode"; cost and guest-state comparisons are made against `clamp`.

## Impact

- Code: `game/src/game_rtl.c`, `ws_ring.[ch]`, `ws_source.[ch]`, `ws_settings.[ch]`, `ws_frame.[ch]`, `ws_cull.[ch]`, `ws_menu.c` (two-value margin row), `game/CMakeLists.txt`; new margin module; `ws_ghost_lane.[ch]` deleted.
- Framework: `game/snesrecomp/runner/src/{cpu_state.c, snes/ppu.c, snes/ppu.h, beam_frame_driver.[ch]}` and `game/patches-snesrecomp-local.diff`.
- Tooling: `game/tools/validate.sh`, `game/tools/validate_refs.txt`, `tools/`.
- Users: a `config.ini` with `Margin = ring` or `wrap` keeps working through the mapping; the menu shows two values.
- Open change `uniracers-rival-edge-indicator` touches `ws_settings`, `ws_menu` and `game_rtl.c`: implement one after the other, not in parallel.
- Docs are rewritten afterwards in `uniracers-docs-overhaul`; this change only fixes code comments.
