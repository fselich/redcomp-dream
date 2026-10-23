## Why

`game/src/game_rtl.c` has grown to 2055 lines through a dozen research
changes. About 700 of them are finished research (map-decode probes, the
track-repeat experiment). The rest mixes seven concerns behind unnamed WRAM
addresses, per-frame `getenv` lookups and copy-pasted helpers, and
`InLiveRace` is duplicated in `main.c`. Before building on it — settings in
`config.ini` (A6), the in-game menu (A6b), new presentation features — the
port needs a structure where each concern lives in one place and a
validation gate that proves a refactor changes nothing
(`docs/UNIRACERS_MEJORAS.md` A3, A4, A8, D14).

## What Changes

- Split `game_rtl.c` into modules by concern: `ws_frame`, `ws_hud`,
  `ws_cull`, `ws_oam_fix`, `ws_ring`, `ws_source`, `ws_radar`,
  `ws_ghost_lane` and `ws_debug`. `game_rtl.c` keeps the frame loop, the
  draw orchestration and `kGameInfo`.
- Name every WRAM address the port reads or writes in `variables.h`
  (rider X/Y, camera, cull window, `$03ED`, race-state bytes, 2P flag, the
  `$7F` track table), each with a one-line explanation.
- One shared `InLiveRace` / `WideEnabled` (`ws_frame`), used by `main.c` and
  every module.
- A per-frame context (`WsFrameCtx`: ppu, wide, live, margin, 2P) computed
  once at the top of `GameDrawPpuFrame` and passed to the modules instead of
  re-evaluating the same predicates in every branch.
- Environment switches and log paths are read once and cached. `g_config`
  values are still read live.
- **Removal (debug-only):** the seven `WsTest*` stage-1/2 probes
  (`SNESRECOMP_WS_TEST_GEN`, `_TEST_GEN_FRAME`, `_TEST_STAGE2*`) and the
  track-repeat experiment (`SNESRECOMP_WS_TRACK_REPEAT`). No spec requires
  them; their findings live in `docs/` and the archived changes, and git keeps
  the code. `SNESRECOMP_WS_TEST_OAM` stays, trace builds only.
- Deduplicate: the 10-bit scroll wrap delta, the ring-line observation (real
  and ghost lanes), OAM slot decode, the cull state (one `WsCullState`
  instead of loose statics plus a mirror struct) and lazy log opening.
- Tidy: stale comments (the radar is 64×32 over a 128×64-cell window), the
  misleading indentation at `game_rtl.c:987`, an unused parameter, consistent
  `uint16_t`/`int16_t`, `GameGhostAfterRunFrame` renamed to
  `GameAfterRunFrame`, a header that describes this port instead of the
  scaffold.
- Add `game/tools/validate.sh`: runs a copy of the executable in a scratch
  directory (never `build-local/saves/`) across native, 16:9, 21:9 and
  512 px, and compares frame hashes and source-mode scores against stored
  references. It runs before the refactor to record the baseline and after
  each step.
- Every policy and switch stays: `wrap`, `ring`, `source`, `ghost`, `clamp`,
  `SNESRECOMP_WS_TRACK_CLAMP`, `_HUD`, `_CULL`, `_OAM_XHIGH`, `_MINIMAP`,
  `_ASPECT`, the ghost-lane tuning and every `*_LOG`.

## Capabilities

### New Capabilities

- `recomp-code-structure`: output-identical refactors, module boundaries,
  named WRAM addresses, the single live-race predicate, and the
  `validate.sh` gate that keeps the user's saves untouched.

### Modified Capabilities

None. Every existing requirement holds unchanged; the removed switches are
not part of any spec.

## Impact

- `game/src/`: `game_rtl.c` split into new `ws_*.c` / `ws_*.h` files;
  `main.c` and `variables.h` updated; `game/CMakeLists.txt` lists the new
  sources.
- New `game/tools/validate.sh` and its reference file.
- No change under `game/snesrecomp/`, no guest-memory behaviour change, no
  `config.ini` change.
- Out of scope: `UniracersSettings` and a `[Widescreen]` section in
  `config.ini` (follow-up change), removing the ghost lane or `ring`, the
  45-track matrix (A1), and pruning process-only specs.
