# uniracers-code-cleanup — report

Date: 2026-10-23
Status: **done, go**. Output-identical refactor; the validation gate stays in
the tree.

## 1. What changed

`game/src/game_rtl.c` is now the port glue only (106 lines, from 2055):
frame boundary, draw orchestration, host hooks and `kGameInfo`. The features
live in modules by concern:

| Module | Owns | Guest state |
|---|---|---|
| `ws_frame` | predicates, margin, `WsFrameCtx`, env cache, parsers | reads only |
| `ws_cull` | `$0421/$0423` cull widening | **the only production WRAM writer** |
| `ws_hud` | BG3 elastic band + clamp bands | PPU config only |
| `ws_oam_fix` | X-high fix, parked sentinel, parked 2P pair, TEST_OAM | PPU OAM copy only |
| `ws_ring` | ring seen table, scroll unwrap, line/margin hooks, policies | reads only |
| `ws_source` | `$7F` table reader, anchor search/self-check | reads only |
| `ws_radar` | 64×32 panel + `[Video] MiniMap` | extra object only |
| `ws_ghost_lane` | run-ahead lane and world-keyed cells | snapshot/restore, no net change |
| `ws_debug` | lazy CSV/text logs | none |

`variables.h` names the WRAM addresses (`kRam*`) and owns
`ram_read16`/`ram_write16`/`ram7f_read16`. `main.c` uses the single
`WsWideEnabled`/`WsInLiveRace` from `ws_frame`.

Removed research code: `WsTestGen`, `WsTestStage2`, `WsTestStage2Scan{2,3,4}`,
`WsTestStage2Keys` and their calls; `WsTrackRepeatEnabled`, `WsRepeatBegin`,
`WsRepeatEnd` and `SNESRECOMP_WS_TRACK_REPEAT`. The last commit that still has
them is **`2ef7d61`** (`git show 2ef7d61:game/src/game_rtl.c`). Their findings
remain in `docs/` and the archived changes.

Deduplicated: one `WsWrapDelta10`, one ring-line walk (`WsRingWalkLine`) for
the real and ghost lanes, `WsOamSlot` read/hide/set-high helpers, one
`static WsCullState`, one `WsLogOpen` per log, one `W` predicate set.

## 2. Line counts

| File | Before | After |
|---|---:|---:|
| `game/src/game_rtl.c` | 2055 | 106 |
| `game/src/main.c` | 141 | 121 |
| `game/src/variables.h` | 14 | 65 |
| `ws_frame.[ch]` | — | 161 |
| `ws_cull.[ch]` | — | 97 |
| `ws_hud.[ch]` | — | 51 |
| `ws_oam_fix.[ch]` | — | 218 |
| `ws_ring.[ch]` | — | 417 |
| `ws_source.[ch]` | — | 359 |
| `ws_radar.[ch]` | — | 201 |
| `ws_ghost_lane.[ch]` | — | 426 |
| `ws_debug.[ch]` | — | 60 |

## 3. Validation gate

`game/tools/validate.sh` copies the executable into a scratch directory with
an empty `saves/` and a generated `config.ini`. It never reads or writes
`game/build-local/saves/` or the user's `config.ini`; the fingerprint guard is
metadata-only (`stat`) and only warns if an external process (e.g. the player)
changes them during a run. `DisableFrameDelay=1` keeps a run at ~8 s.

`--record` runs every case twice. On this host the headless menu/attract
timing is **nondeterministic**: even the pre-refactor binary reaches the same
frame number with a different game state, and a fixed `$0300 == 1` dump also
differs between runs. The gate therefore records `NONDET` for frame/log
artifacts rather than pretending they are stable. The stable fallback is the
BG1 wrong-cell score (`world_key_check.py --served`): zero wrong cells on both
margins for every wide log case. A default run reports `SKIP
(nondeterministic; score checked where available)` and `validation OK`.

This is a property of the host, not of this refactor: `game/build-before`
(pre-cleanup) shows the same double-run nondeterminism. On a deterministic
host the stored historic hashes are still there for comparison:
`native 3417f91a…`, `16:9 source fa228fef…`, `21:9 9a99be61…`,
`512 fb1fca82…`, `wrap 4b138431…`, `ring 7abf7e41…`, `clamp e09fd18d…`,
`ghost 4b138431…`, `monster aa3248d0…`, `zoomzoo512 a66b2a34…`.
The historic `4b138431…` is the `wrap` policy, not the current default;
native still matches `3417f91a…df44`.

### Direct pre/post evidence (single run, same starting SRAM)

Before the module split, the pre-cleanup and post-change binaries produced
exactly the same frames in targeted comparisons:

| Case | SHA-256 |
|---|---|
| native | `038b97c41517b646` |
| 16:9 | `6d5c44dc74fbef23` |
| 21:9 | `3ee4b0a89313f1f2` |
| 512 | `9df2fa9d42ffda1f` |
| `SNESRECOMP_WS_OAM_XHIGH=0` | `9ffa16f896246840` |
| WRAM / SRAM / VRAM / CGRAM | `ee891f…` / `d01674…` / `b1ca57…` / `486f88…` |

After the module split, one full single-run pass against the pre-cleanup
references matched all 11 cases; later runs are skipped by the `NONDET`
fallback described above. The trace build (`-DSNESRECOMP_ENABLE_TRACE=ON`)
compiles and links.

## 4. Audits

- `grep getenv game/src`: only `ws_frame.c` (the cache).
- `grep 'g_ram[...] ='`/`ram_write16`: only `ws_cull.c` (plus the inline
  helper in `variables.h`). The ghost lane uses rollback save/restore, not
  direct WRAM writes.
- 2P split screen, netplay and run-ahead guards are unchanged.
- All switches/policies survive: `wrap`, `ring`, `source`, `ghost`, `clamp`,
  `SNESRECOMP_WS_TRACK_CLAMP`, `_HUD`, `_CULL`, `_OAM_XHIGH`, `_MINIMAP`,
  `_ASPECT`, the ghost-lane tuning and every `*_LOG`.

## 5. Residual risks

- The `ghost` case is stable but currently produces the `wrap` hash in the
  gate (the lane is parked/too slow in practice). The case still proves the
  policy path builds and runs; a future ghost revival needs a dedicated check.
- The 45-track matrix (A1) and `UniracersSettings`/`[Widescreen]` (A6) remain
  follow-ups. This change centralizes env reads for that work.
- The host's headless runs are nondeterministic, so `validate.sh` cannot prove
  frame identity on this machine; it proves the margin score is 0 wrong and
  the code builds. Direct single-run pre/post comparisons above cover the
  frame identity observed during this change. A deterministic host (or a
  fixed save state) would let the historic hashes be enforced again.
- The gate is a presentation gate, not a full WRAM trace; the state dump
  remains available for deeper checks.
