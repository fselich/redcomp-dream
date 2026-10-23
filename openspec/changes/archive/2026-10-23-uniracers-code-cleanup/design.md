## Context

`game/src/game_rtl.c` (2055 lines) is the whole port: frame loop, HUD band,
cull window, OAM fixes, BG1 ring observation, the `source` anchor, the radar,
the ghost lane, seven research probes and a track-repeat experiment.
`main.c` carries its own copy of `WideEnabled` / `InLiveRace` for
`PrepareFrame`. `variables.h` was meant to name WRAM addresses and is empty.

Current per-frame shape of `GameDrawPpuFrame`:

```
GameDrawPpuFrame
  WsInjectTestOam / WsFixRiderOamXHigh / WsRadarUpdate      each: WideEnabled() && InLiveRace()
  policy = g_ppu && WideEnabled() && InLiveRace() ? mode : WRAP
  verbose live/inactive log                                  WideEnabled(), InLiveRace()
  HUD clamp band                                             WideEnabled() && InLiveRace() && g_ppu
  BG1 clamp                                                  WideEnabled() && InLiveRace() && g_ppu
  track repeat                                               WideEnabled() && InLiveRace() && g_ppu
  WsApplyHud                                                 WideEnabled() && InLiveRace()
  install hooks → draw → WsSrcFrame / invalidate → WsRepeatEnd
```

Every `WideEnabled()` call does a `getenv`, and so do most feature gates and
log helpers, some inside per-slot loops.

Constraints:
- The output must be identical byte for byte: same frames, same WRAM writes,
  same log content, same stderr lines.
- `game/snesrecomp/` is not touched.
- Specs that bind behaviour: `recomp-ws-hud`, `-objects`, `-radar`,
  `-source`, `-source-default` (all five policies stay selectable),
  `-ultrawide`, `-offwindow`, `-leading-edge`, `-ghost-lane`.

## Goals / Non-Goals

**Goals:**
- One file per concern, each with a header comment saying what it owns and
  whether it touches guest state.
- Named WRAM addresses, so a reader sees `kRamRider1X` and not `0x0411`.
- A single live-race predicate and a per-frame context.
- A repeatable gate that proves "no output change" and never touches the
  player's saves.

**Non-Goals:**
- Settings struct / `[Widescreen]` in `config.ini` (follow-up change). This
  change only makes that follow-up easy: env lookups are already centralised.
- Behaviour fixes, performance work that changes results (e.g. caching
  `WsOwFindBlankChar`), removing a policy.
- Restyling the framework or the generated code.

## Decisions

### D1. Module layout

```
src/
  main.c            host shim; PrepareFrame uses ws_frame
  variables.h       WRAM addresses (kRam*) + ram_read16/ram_write16, ram7f_read16
  game_rtl.c        GameRunOneFrame, GameDrawPpuFrame, GameAfterConfig,
                    GameAfterRunFrame, GameSessionReset, kGameInfo
  ws_frame.[ch]     WideEnabled, InLiveRace, WsExtra, WsFrameCtx, env helpers
  ws_hud.[ch]       BG3 elastic band + clamp band
  ws_cull.[ch]      $0421/$0423 widening — the only guest-memory writer
  ws_oam_fix.[ch]   X-high bit, parked sentinel, parked 2P pair (+ TEST_OAM)
  ws_ring.[ch]      scroll unwrap, world-cell math, seen table, line hook,
                    margin tile hook, policy parsing
  ws_source.[ch]    $7F table reader, anchor search and self-check
  ws_radar.[ch]     radar panel + MiniMap config read
  ws_ghost_lane.[ch] speculative lane (policy `ghost`)
  ws_debug.[ch]     lazy CSV/text log files keyed by env var
```

Dependency direction: `game_rtl → ws_* → ws_frame, variables.h`;
`ws_radar → ws_source → ws_ring`; `ws_ghost_lane → ws_ring, ws_cull`.
No module includes `game_rtl.h` except `main.c`.

Alternative considered: two files (`ws_core.c` + `ws_debug.c`). Rejected —
`source` and the ghost lane are each 300–400 lines with their own state, and
the follow-up menu needs per-feature entry points.

State stays `static` inside each module. Cross-module reads (the ring's
unwrapped scroll for the radar, the anchors for the radar and margin hook,
the cull state for the ghost lane) go through small accessor functions, not
`extern` globals.

### D2. Named addresses in `variables.h`

`variables.h` is the framework's designated place for game WRAM names and is
included by `snes/cpu.c`, so names must not clash with framework symbols:
`enum` constants with a `kRam` prefix (`kRamRider1X = 0x0411`,
`kRamCamX = 0x0419`, `kRamCullLeft = 0x0421`, `kRamCullZoomShift = 0x03ED`,
`kRamRaceMode = 0x00B4`, `kRamPauseFinish = 0x0EE9`, `kRamScreenMode =
0x0300`, `kRamTwoPlayer = 0x0DDB`, `kRam7fTrack* `…). The 16-bit read/write
helpers become `static inline` there. Addresses only touched by the deleted
probes are not added.

Alternative: a new `uniracers_ram.h` (as A8 suggests). Rejected only because
`variables.h` already exists for exactly this and is otherwise empty.

### D3. One predicate, one context

`ws_frame.c` owns `WsWideEnabled()` and `WsInLiveRace()`; `main.c` drops its
copies. `GameDrawPpuFrame` builds once:

```c
typedef struct {
    Ppu *ppu;      /* may be NULL */
    int wide;      /* WsWideEnabled() */
    int live;      /* WsInLiveRace() */
    int active;    /* ppu && wide && live */
    int extra;     /* WsExtra() */
    int two_player;
} WsFrameCtx;
```

and passes `const WsFrameCtx *` to the HUD, OAM fix, radar and margin setup.
`GameRunOneFrame` keeps calling the cull pass twice (before and after the
frame) with a fresh predicate each time, because WRAM changes in between and
the second call catches a base written during the frame. The ghost lane keeps
its own eligibility check for the same reason.

The context must be equivalent to the current calls: every predicate is read
from WRAM/`g_config`/`g_ws_extra` before any of the presentation code runs,
and none of the presentation code writes those inputs. The one exception to
check is the verbose `[ws-ow] ACTIVE/inactive` log, which prints `live` — it
reads the same value.

### D4. Environment lookups cached once

A helper `WsEnvFlag(name, default)` / `WsEnvInt(...)` / `WsEnvStr(name)`
caches the first lookup per switch in a static. The environment is fixed for
the life of the process (nothing in the port or the framework calls
`setenv`), so a cached value equals a live one. `g_config.widescreen` is
**not** cached: it can change when the launcher reapplies the config.

Alternative: leave `getenv` as is. Rejected — it is the main obstacle to the
settings follow-up, and the inconsistency (some cached, some not) is a trap.

### D5. Removals

- `WsTestGen`, `WsTestStage2`, `WsTestStage2Scan{,2,3,4}`, `WsTestStage2Keys`
  and their calls in `GameRunOneFrame`. They are inert unless their env var
  is set, so removing them does not change default output. Their findings
  are in `docs/UNIRACERS_TRACK_FORMAT.md`, `docs/workshop/ws-map-decode/` and
  the archived `uniracers-native-map-spike` / `-ws-map-decode` changes.
- `WsRepeatBegin` / `WsRepeatEnd` / `SNESRECOMP_WS_TRACK_REPEAT`: a visual
  experiment superseded by `source`, not in any spec.
- `tools/patches/*/game_rtl.c.before` / `main.c.before` snapshots stay: they
  are referenced from archived reports, and pruning `tools/` is its own task.

### D6. Deduplication specifics

- `WsWrapDelta10(now, prev)` replaces the three copies of the 10-bit signed
  delta.
- `WsRingObserveLine(ppu, map_base, base_h, base_v, raw_h, raw_v, line, sink)`
  walks the visible cells once; the real lane writes `s_ow_seen`, the ghost
  lane `s_gh`. The real lane must keep recording `frame`; the sink is a small
  callback or a two-case switch — whichever generates the simpler code.
- `WsOamSlot { int lo, y, hi, bit; }` read/write helpers for the three OAM
  loops.
- One `static WsCullState s_cull`; `WsCullGet/Set` become struct copies.
- `WsLogOpen(env_name, header)` in `ws_debug.c` returns a cached `FILE *`
  (or NULL) for all six lazy log files; formats and headers stay identical.
- `WsRadarTrim` / `WsRadarCiEq` / `WsRadarBool` move to `ws_frame` as
  generic helpers (`WsParseBool`), since the settings follow-up reuses them.

### D7. The validation gate (`game/tools/validate.sh`)

```
validate.sh [--record] [--build DIR]
  1. copy UniracersSNESRecomp into $TMP/uniracers-validate/  (never build-local/saves)
  2. for each case: fresh empty saves/, headless run, screenshot at frame N,
     source log + BG probe where the case asks for it
  3. sha256 each frame; score source logs with world_key_check.py
  4. --record: write game/tools/validate_refs.txt
     default : diff against validate_refs.txt, exit 1 on any difference
```

Cases (Dragster route unless noted, frame 2500):

| Case | Env | Known reference |
|---|---|---|
| native | `SNESRECOMP_WS=0` | `3417f91a…df44` |
| 16:9 | `SNESRECOMP_WS=1 SNESRECOMP_WS_ASPECT=16:9` | `4b138431…e357` |
| 21:9 | `… ASPECT=21:9` | `b1faaddc…` (pre right-edge fix) |
| 512 px | `… ASPECT=32:9` | `a3c57650…` (pre right-edge fix) |
| each policy | `SNESRECOMP_WS_OFFWINDOW=wrap/ring/clamp/ghost` at 16:9 | recorded |
| Monster 16:9 | Monster route, frame 2300 | `30af0020…` (`source`) |
| Zoom Zoo 512 | Zoom Zoo route, frame 3505 (`$03ED ≠ 0`) | recorded |
| logs | `SNESRECOMP_WS_SOURCE_LOG`, `_OFFWINDOW_LOG`, `_PARK_LOG`, `_RADAR_LOG` on 16:9 | sha256 of each log |

References are recorded from the pre-refactor commit, not copied from old
reports: the right-edge fix changed the 512-px output after those reports.
The historic hashes are only a sanity check for native and 16:9.

The archived discriminator report warns that headless `SNESRECOMP_SCREENSHOT`
grabs the first present at or after the frame. `--record` therefore runs
each case twice and refuses to write a reference that differs between the
two runs; such a case is reported as non-deterministic and compared by its
source-log score only.

Alternative: extend `tools/scripts/ws-source/validate_all.sh`. Rejected — it
deletes `build-local/saves/save.srm` (A4) and writes into `work/`. The new
script lives next to `regen.sh` and can later grow into A1/A2.

### D8. Order of work

Each step builds and passes `validate.sh` before the next:

1. `validate.sh --record` on the current commit.
2. Remove probes and track repeat.
3. `variables.h` names + `ws_frame` (predicate, env cache, context); `main.c`
   uses it.
4. Move code into modules one at a time (debug → cull → hud → oam_fix → ring
   → source → radar → ghost lane), pure moves first.
5. Deduplication and tidy-ups, inside the now-small files.

Pure moves before rewrites keeps every diff reviewable: a move diff should
be all deletions in `game_rtl.c` and the same lines added elsewhere.

## Risks / Trade-offs

- [Headless captures not frame-exact] → double-run check in `--record`;
  non-deterministic cases fall back to log scores.
- [The context hides a predicate change mid-draw] → D3 argument plus the
  verbose-log case in the gate; the cull and ghost passes keep their own
  live evaluation.
- [A caching helper changes when an env var is read relative to log file
  creation] → log files are still opened lazily on first write, so their
  creation time and content do not move.
- [Splitting static state across files changes initialisation order] → all
  statics are zero- or constant-initialised; there is no constructor order.
- [The ghost lane is slow and rarely run, so regressions there go unseen] →
  the gate includes a `ghost` case.
- [Removing probes loses a research tool] → they stay in git history; the
  change's report names the last commit that has them.

## Migration Plan

No user-facing migration: same binary behaviour, same `config.ini`, same
switches except the two removed debug ones. Rollback is a git revert;
`validate_refs.txt` stays valid because it describes the pre-refactor output.

## Open Questions

- Whether `SNESRECOMP_WS_TEST_OAM` (trace builds only) is still worth keeping.
  Default: keep, inside `ws_oam_fix.c`.
- Whether the Zoom Zoo 512 px capture is deterministic headless; decided when
  `--record` runs.
