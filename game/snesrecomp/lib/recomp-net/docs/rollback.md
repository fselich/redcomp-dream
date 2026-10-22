# Rollback mode

recomp-net carries delay-sync (`RNetSession`) and, beside it, a shared
rollback stack that hosts opt into without changing delay-sync behaviour. (This
file used to say rollback lived on a `feat/rollback` branch; it is on `main`,
where `rollback_episode_test` was failing, so the sentence was stale.)

## Layers

| Layer | Status | Location |
|-------|--------|----------|
| Portable input contract | Landed | [`include/recomp_net/input_contract.h`](../include/recomp_net/input_contract.h), [`src/input/rnet_input_contract.c`](../src/input/rnet_input_contract.c) |
| Rollback episode orchestration | Landed | [`include/recomp_net/rollback.h`](../include/recomp_net/rollback.h), [`src/rollback/rnet_rollback.c`](../src/rollback/rnet_rollback.c) |
| Rollback wire protocol | Landed | `RNET_PKT_RB_*` (opcodes 20–25) in [`src/protocol/rnet_protocol.{h,c}`](../src/protocol/rnet_protocol.h) |
| Admission scheduler | Moved from retcomm-rbengine | [`include/recomp_net/sched.h`](../include/recomp_net/sched.h), [`src/sched/rnet_sched.c`](../src/sched/rnet_sched.c) |
| Input history (invent / promote) | Moved from retcomm-rbengine | [`include/recomp_net/input_hist.h`](../include/recomp_net/input_hist.h), [`src/input/rnet_input_hist.c`](../src/input/rnet_input_hist.c) |
| Hash-confirm watermark | Moved from retcomm-rbengine | [`include/recomp_net/hash_confirm.h`](../include/recomp_net/hash_confirm.h), [`src/rollback/rnet_hash_confirm.c`](../src/rollback/rnet_hash_confirm.c) |
| RB_POST tip filter | Moved from retcomm-rbengine | [`include/recomp_net/rb_post.h`](../include/recomp_net/rb_post.h) |
| **Episode driver** | Lifted from snesrecomp (2026-09-24) | [`include/recomp_net/rb_driver.h`](../include/recomp_net/rb_driver.h), [`src/rollback/rnet_rb_driver.c`](../src/rollback/rnet_rb_driver.c) |

The four moved modules were MotK host policy lifted into retcomm-rbengine. Every
decision they make is about peers, so they belong beside the session and the
episode FSM. retcomm-rbengine keeps what works with netplay compiled out — the
snapshot ring and a monotonic clock — and neither library depends on the other.

## Rollback wire protocol

Additive, non-colliding opcode range for rollback-mode sessions. Delay-sync
hosts never emit or parse these. Seal rows are fixed 7-byte frames
(`RNetRbWireFrame`: buttons 2 + sticks 2 + source + predicted + valid) with the
tick derived from `row_begin + index`.

| Opcode | Packet | Payload |
|--------|--------|---------|
| 20 | `RB_SYNC` | correction tuple `(epoch, mismatch, load, target, slot, op, flags)` |
| 21 | `RB_SEAL_ROWS` | peer-authority sealed rows chunk (tuple + rows) |
| 22 | `RB_BASELINE` | post-load digests (master + 3 partitions) for the baseline gate |
| 23 | `RB_POST` | post-replay digests + match flag (commit / deepen / abort) |
| 24 | `RB_FRAME_COMMIT` | state/master-hash watermark agreement token |
| 25 | `RB_RESOLVED` | resolved-through / shared frontier advertise |

`RB_SYNC` carries an op code (the byte historically named `initiator`) and a
flags byte (`recomp_net/session.h`):

- `OP_BEGIN` — open or tip-extend an episode. Flags are
  initiator-authoritative episode attributes the follower adopts verbatim:
  `FLAG_LIGHT_TIP` (episode class; keeps baseline burst counts symmetric) and
  `FLAG_REREPLAY` (tip-extend requires re-replay from load — the follower
  mirrors the initiator's decision instead of re-deriving from its own tip).
- `OP_NACK` — follower refuses/cannot follow. The target field carries the
  follower's confirmed frontier so the initiator demotes its watermark to a
  mutually provable tick instead of guessing `load-1`.
- `OP_ABORT` — sender tore its episode down. The mismatch field carries the
  shared `RNET_RB_ABORT_CLASS_*` cooldown class, the load field the sender's
  realign tick; the receiver mirrors teardown + cooldown so both peers re-arm
  on the same schedule.

Hosts should partition epoch ids by initiator slot
(`epoch = counter << k | slot`) so concurrent dual initiation never collides
and a deterministic tie-break (lower initiator slot wins; the loser yields and
follows) can derive any episode's initiator from its id. The episode driver
uses `k = RNET_RB_EPOCH_SLOT_BITS` (3, all eight seats):
`rnet_rb_epoch_make` / `rnet_rb_epoch_initiator`.

`RB_SEAL_ROWS` carries at most `RNET_RB_SEAL_ROWS_CHUNK_MAX` (24) rows; the
send path truncates a larger chunk, so a host that chunks wider posts a partial
span and waits forever for the rest. `row_begin` is an OFFSET into the sealed
span and the `mismatch` field carries the seal base (the LOAD tick) -- sending
ticks in either posts nothing. `rnet_session_rb_last_take_from` names the
sender of the rb_* message just taken (FRAME_COMMIT and the mod-set ACK
included), which an episode with more than one peer needs to count answers
per peer.

`RB_FRAME_COMMIT` is sent after every live tick and, since 2026-09-25, for
every tick of a completed replay (the replayed digest replaces the live one;
see "Episode driver"). The session keeps each commit's sender; the driver
keeps one chain per peer.

Hosts map their existing wire onto these when aligning transports (BattleShip's
soak-hardened `SYNETPEER_*` format stays authoritative for live matches); new
recomp-net rollback hosts may use `RNET_PKT_RB_*` natively. Transport/ICE
unification is a follow-up — the delay-sync ICE path is unchanged.

## Rollback episode orchestration

`RNetRbSession` holds the episode state (`Live → SealInputs →
AwaitingBaseline → Replay → Verify → TipHold → Commit|Abort`), the correction
tuple, the sealed input table, and the resolved-through (shared frontier)
watermark. It is PASSIVE: `rnet_rb_set_phase` validates nothing, the event
queue has no producer inside the library, and the only host callback it ever
invokes is `get_input_row`. What runs an episode is the driver below; a host
that does not use it must do all of that itself.

TipHold is the stage after a POST match: the sealed rows stay open for
`tip_runway` ticks while Live runs, so a late edge can tip-extend the same
episode instead of opening a second one. `rnet_rb_enter_tip_hold` IS the
post-match transition (it promotes the sealed rows) and requires Verify --
calling `rnet_rb_on_post_match` first moves the phase to Commit and makes it
fail on every episode (measured on SNES: 0 tip-hold entries in 281 episodes).
`on_post_match` is the other branch, never a preceding step.

### Tip-extend / edge coalesce

`rnet_rb_extend_target` grows `target_tick` and appends seal rows so a late
wire edge (typical digital press then release) stays in the **same** episode
instead of a second seal/baseline handshake. Verify→extend drops to Replay;
TipHold→extend **stays TipHold** (host invent-caps Live past tip and
schedules a short rereplay only when sim already invented past the prior
tip). `rnet_rb_resign_slot_range` refreshes sealed rows after the host
promotes wire into history. `apply_peer_seal_rows` accepts
`target >= corr.target` and auto-extends; it also refuses to let a
**predicted** peer row overwrite a non-predicted sealed row (mask bit is
still credited) so tip-extend invent-idle exports cannot clobber a
wire-promoted resign. `rnet_rb_suggest_target` adds
`cfg.tip_seal_slack` past live tip when opening an episode.
`resolved_through` survives `session_reset`.

### Light tip

When `(target - load) <= cfg.light_tip_max_depth` (0 → the library default
`RNET_RB_LIGHT_TIP_MAX_DEPTH`, 16) and `load` is at/after `resolved_through`,
`begin_episode` sets `RNET_RB_CORR_LIGHT_TIP`. Hosts may skip the ready-ACK
RTT (digests still compared) and shrink baseline bursts.

Hosts that widen `cfg.tip_runway` for TipHold coalescing (see above) should
also raise `cfg.light_tip_max_depth` to match — a coalesced episode's
eventual depth (load..target after one or more `tip_extend` calls) can
approach `tip_runway`, and if that exceeds the light-tip ceiling the episode
silently falls back to the full ready-ACK round trip even though nothing
actually went wrong. A MotK soak with `tip_runway=24` and the untouched
library default (`16`) lost the light-tip fast path on ~19% of episodes for
exactly this reason before `light_tip_max_depth` was added and set to match.
`rnet_rb_is_light_tip_candidate_ex(load, target, resolved_through, max_depth)`
lets a host precompute the flag with an explicit ceiling before a session
exists (or before `corr` is populated); `rnet_rb_is_light_tip_candidate` is a
thin wrapper using the plain library default for hosts that don't override
`light_tip_max_depth`.

`rnet_rb_demote_resolved_through(s, tick)` pulls the shared frontier down
when a follow-NACK refuses a unilateral tip. `rnet_rb_set_peer_convergence`
only advances, and `session_reset` keeps `resolved_through` — without an
explicit demote the refused tip stays as the library watermark and the next
light-tip / host HC advance reopens it. Hosts that demote their own agreed
watermark on NACK should demote the library watermark in the same step.

`RNetRollbackVTable`: `rnet_rb_create` refuses a vtable without `load_state`,
`advance_sim` or `get_input_row`. Of the six callbacks the core itself calls
only `get_input_row`; `save_state`, `load_state`, `advance_sim`,
`state_digest` and `hash_confirm_through` exist for the host's (or the
driver's) own replay loop and are never invoked from inside the library.
(This section and `rollback.h` previously listed save/load/advance/digest as
required and called by the library; neither was true.) Host stick gates ride
through `stick_gates` and feed `rnet_rb_decide_stick_replace`.

Minimal host loop during an episode, if not using the driver. Seal from the
LOAD tick: the replay publishes a sealed row for every tick it re-runs, and
`load..mismatch-1` are among them -- sealing from the mismatch leaves the
first replayed ticks without a row.

```c
rnet_rb_begin_episode(s, &corr);                 /* mismatch identified */
rnet_rb_seal_inputs(s, corr.load_tick, corr.target_tick, corr.slot);
/* exchange peer seal rows via host transport: rnet_rb_export_seal_rows_chunk /
 * rnet_rb_apply_peer_seal_rows until rnet_rb_all_peer_seal_rows_complete */
rnet_rb_set_phase(s, nRNetRbPhaseAwaitingBaseline);
vt.load_state(vt.ctx, corr.load_tick);
for (t = corr.load_tick; t <= corr.target_tick; ++t)
    vt.advance_sim(vt.ctx, t);                   /* reads sealed rows */
/* compare vt.state_digest against peer; then, on a match: */
if (!rnet_rb_enter_tip_hold(s))                  /* TipHold, rows promoted */
    rnet_rb_on_post_match(s);                    /* runway 0: straight to Commit */
/* on a mismatch: rnet_rb_on_post_diverge(s) and abort */
```

The core is transport-agnostic; the driver speaks `RNET_PKT_RB_*` through
`RNetSession`.

## Episode driver

`recomp_net/rb_driver.h` is the code that runs an episode, lifted out of
snesrecomp's `snes_netplay_rb.c` (36d6ce5) by owner ruling on 2026-09-24 so
every engine binds one driver instead of writing its own. It owns live admit
(local tip, remote rows, hold-last invent, the scheduler and every gate it
asks for), reconcile (late wire against predicted history), the whole episode
FSM (open, follow, dual-initiation arbitration, NACK, seal-row exchange,
baseline gate, replay, POST verify, commit, tip-hold, tip-extend, abort
classes and mirrored cooldowns, a watchdog on every stage that waits on a
peer), snapshot policy (interval, floor, the baseline fork cap), the
FRAME_COMMIT hash chain, the boot-digest gate, the mod-set and identity
handshakes, lockstep degrade, the advisory chain-stall report, and a cold
reset on every start.

Rules it adds over what snesrecomp shipped. The first two were found by
`tests/rb_driver_test.c`'s toy engine (which folds every seat's row into its
state, so a correction that never lands is a measurable divergence):

- **A correction is owed until a replay covering it completes.** Reconcile
  promotes the true row the moment it decides to rewind; if the episode never
  replays it (NACK, abort before the load, watchdog, dual-initiation yield, a
  stage that cannot take it, the initiator cooldown) the tick is re-opened
  later ("RB correction retried"), or declared "RB correction LOST" once no
  snapshot reaches it. A completed replay pays it even if the episode then
  fails to commit: the span was re-run on authoritative rows.
- **An abort after the baseline load restores the live tip** ("RB tip
  restored"), instead of leaving the engine on the load tick while sim stays
  at the old tip.
- **An answer that beats its BEGIN is held, not dropped** (2026-09-25). An
  initiator that can seal from its own confirmed history sends BEGIN,
  BASELINE and POST in one poll; under jitter the POST can arrive first. It
  was discarded, and the follower waited out the 2 s episode budget and
  aborted "timed out waiting for peer POST" on an epoch the initiator had
  committed -- every POST timeout in snesrecomp's 200/300 ms sweep cells.
  POST and BASELINE for a peer epoch newer than any BEGIN seen from that seat
  are held per seat and taken when the BEGIN opens the episode.
  SEAL_ROWS chunks are held the same way (2026-09-25), and an answer held for
  another epoch that can still open survives a follow of a different one.
- **A follower short of the load tick follows it** (2026-09-25). It used to
  refuse ("no snapshot at load tick"); see "The follower behind the load
  tick" below. At `sim == load` the live state IS the snapshot keyed `load`
  and is taken on the spot ("RB follow at our live tip"); short of it, the
  BEGIN is held ("RB follow deferred ... span=") and followed when sim gets
  there ("RB follow resumed ... after N ms"), bounded by half the episode
  budget (then "RB follow refused ... did not reach it within N ms" and a
  NACK, before the initiator's own watchdog). While one is held this peer
  opens no episode of its own (the correction is owed and re-opened after),
  and a second BEGIN is arbitrated like one for an open episode.
- **A completed replay rewrites the chain.** Each replayed tick's digest is
  kept and, when the replay completes, written into the hash chain and sent
  as a FRAME_COMMIT. A commit re-primes the chain anyway; an episode that
  ended any other way after its replay left the live, mispredicted digests
  behind, and the chain stalled on a tick the replay had already fixed. A
  replay undone by a tip restore leaves no trace (nothing is written until it
  completes). The advisory "RB chain stall" is also not reported at or after
  a correction this peer still owes: that mismatch is explained until the
  retry replays it.
- **POST and BEGIN are re-sent until answered** (2026-09-25). Each used to go
  out once, and one lost datagram held the peer that needed it for the whole
  2 s episode budget ("timed out waiting for peer POST"). A commit needs N-1
  POSTs at each of N peers, so the chance one is lost grows with the seats:
  about 4 % of episodes at 2 % loss with two, about 22 % with four. POST is
  re-sent through Verifying and TipHold, the initiator's BEGIN (with the
  current target) through Sealing and Verifying until every peer has answered
  the epoch, each once per round trip and at least 50 ms apart. A peer
  answers each BEGIN once (a ring of epochs seen); a repeat of one it refused
  repeats the NACK, since that is what was lost; one overtaken on the link by
  a newer BEGIN from the same seat is refused. The drain line counts both
  ("POSTs re-sent", "BEGINs re-sent"). Measured on n64lle, same day, before
  (5492a31) and after (bb8096a): the two-seat sweep's loss cells (2 %, 5 %,
  2 % + 200 ms) went from 3, 6 and 1 watchdogs to 0, 0 and 0, and four seats
  at 2 % from 12 and 9 to 2 and 2 (below). snesrecomp's sweep (Gundam, built
  against this branch; snesrecomp `feat/netplay-integration` 00d1566, its own
  `tools/rb_sweep.sh`): 13/13 gating PASS and 0 forks on both 5492a31 and
  bb8096a; its loss cells' aborts went from 3, 8 and 1 to 0, 0 and 0.
- **A newer BEGIN from the episode's initiator releases the follower**
  ("RB follow released"): an initiator runs one episode at a time, so it has
  left the one we hold. A lost COMMIT used to keep the follower in tip-hold
  and NACK the next episode.
- **A correction that can no longer be replayed says so** ("RB correction
  LOST"): once its replay would exceed the 64-row seal mask, or when it ages
  out of the input history. The second used to happen silently, and the
  drain line of a peer that had forked reported "still owed: 0".

### Integrating a host (n64lle, psxrecomp, the next engine)

The host implements `RNetRbHost`; `start()` names any required callback left
NULL and refuses.

| Callback | What the host supplies |
|---|---|
| `snap_save/load/has/oldest/drop_after` | Snapshot storage keyed by tick. Key T = the state BEFORE tick T runs. The host owns the bytes (rbengine's ring works; recomp-net does not depend on it). |
| `publish(tick, rows, slots, replay)` | The rows every seat simulates at `tick`, sanitized. Called for every live tick and every replayed tick. |
| `run_tick(tick)` | INLINE shape only: run one tick on the rows last published. |
| `resim_begin / resim_end` | Suppress presentation and audio production for a replay (NETPLAY.md §1). |
| `digest_master` / `digest_parts` | Simulation-state digest (FRAME_COMMIT, POST) and master + three named partitions (BASELINE); `cfg.part_names` names them. The digested domain must equal the snapshotted domain. |
| `decode_sample` / `sanitize_row` / `neutral_row` | Pad layout: wire bytes to a row, force a row legal, and what "nothing held" is per seat. Active-high pads (SNES, N64) and active-low (PSX) differ; the driver assumes neither. |
| `admit_sample` (opt.) | Side data riding the pad bytes, from the sample a LIVE admit used. |
| `boot_digest_noted` (opt.) | Log what explains a boot mismatch (partitions, frame counters). |
| `request_return_to_lobby` | The match is refused (boot fork, mod-set refusal): leave it. Called once; the reason is `rnet_rb_driver_refusal()` (`boot_digest_mismatch`, `mod_set_mismatch`, `mod_set_not_agreed`). From the refusal on `poll_admit` admits no Live tick, so a refused match cannot play on while the host gets round to it -- but tearing the session down and returning to the lobby is the host's job. A host that never consumes it freezes its players on the last frame. |
| `log` (opt.), `now_ms` | Line sink (NULL = stderr) and a monotonic clock. |

`RNetRbDriverConfig` takes live pointers to the session, seat, seat count,
delay and prediction exactly as the scheduler bridge does, plus the replay
shape, partition names, the store's depth (reported only), a log tag, and an
environment alias (knobs are `RNET_RB_<NAME>`; `<alias>_<NAME>` is read first,
so snesrecomp's `SNES_RB_*` names keep working).
`inject_flip_bits` names the button bits the `FORCE_MISPREDICT` validation
injector flips in an invented row (0 = the historical `0x0040`). It must be a
bit the engine's pad layer hands the guest: n64lle masks `0x0040` as
unmodeled, so there the default opened episodes whose mispredicted fields the
guest never saw, and a replay that restored nothing would have passed them.

The loop is the same in both shapes:

```c
for (;;) {
    RNetRbAdmit a = rnet_rb_driver_poll_admit(d);
    if (a != RNET_RB_ADMIT_STALL) {
        run_one_tick();                  /* the SAME function, live or replay */
        rnet_rb_driver_finish_frame(d);
    }
    present_and_pump();                  /* the held frame during a replay */
}
```

- **INLINE** (`RNET_RB_REPLAY_INLINE`): the driver loads the baseline and
  runs the whole replay inside one `poll_admit` through `host->run_tick`;
  `poll_admit` only ever returns STALL or LIVE. Right when a tick is cheap and
  returns (SNES `RtlRunFrame`).
- **INCREMENTAL** (`RNET_RB_REPLAY_INCREMENTAL`): each `poll_admit` during a
  replay publishes ONE replayed tick and returns `RNET_RB_ADMIT_REPLAY`; the
  host runs it with its live per-tick function, calls `finish_frame`, and may
  pump, present and service audio before the next poll. No FRAME_COMMIT is
  sent for a replayed tick. Right when a tick is expensive or must run in the
  host's own loop -- an N64 field. `run_tick` may be NULL.

Neither shape drains episode wire mid-replay, so both see the same messages
at the same point of an episode; `rb_driver_test` runs a mixed pair (one peer
inline, one incremental) to hold that. The session keeps receiving meanwhile;
a queue that fills is logged (`rnet_session_rb_ctrl_dropped`).

The scheduler is process-global (`rnet_sched_bind`): one started driver per
process.

### Coordinated stop (for harnesses)

A two-process harness that kills both peers at a deadline cannot tell an
episode in flight at the kill from a lost one, so its ledger fails on an
end-of-run race (snesrecomp's runway-4 cell: 1 of 1 in a sweep, 4 of 4 clean
on repeat). `rnet_rb_driver_request_quiesce` makes the stop exact instead of
tolerated:

- from the request on, this peer opens no episode (a mispredict found while
  draining logs `RB drain: correction not opened` and is counted), no local
  tip-extend, and the validation injector stops. It still follows a peer's
  BEGIN, and every open episode finishes or aborts with its usual line;
- once idle it sends `RNET_RB_SYNC_OP_QUIESCE` ("I will open no more
  episodes, and have none open"), re-sent every 50 ms; a peer that receives
  one starts draining too, so asking either side drains the match;
- `rnet_rb_driver_quiesce_state` reaches `RNET_RB_QUIESCE_DRAINED` when this
  peer is idle and holds every peer's marker (`RB quiesced ... Opened here: I
  as initiator, F as follower`). Nothing either side opened can then be
  unanswered, so `initiated - followed - refused == 0` with no loss. The
  host keeps running live ticks until then and exits after;
- bounded: `RNET_RB_QUIESCE_TIMED_OUT` after `RNET_RB_QUIESCE_TIMEOUT_MS`
  with a line naming what was outstanding (a vanished peer, or one that
  predates the op -- it ignores the unknown op, so the drain ends on the
  bound rather than hanging).

The trigger is the host's: snesrecomp maps SIGUSR1 to it and
`tools/rb_loopback.sh` sends SIGUSR1 to both peers at the deadline, then
grades the ledger only if both logged `RB quiesced`. A port of that harness
(n64lle) needs the same two pieces: a host trigger that calls
`request_quiesce`, and an exit once the state is DRAINED or TIMED_OUT.

Tip-hold now logs every exit with how long it actually held:
`RB tip-hold ended epoch=E held=N ticks (runway R) — <why>` (runway spent,
every peer committed, tip-extend, aborted, peer aborted, yielded, watchdog
expired). The runway is a ceiling, not a duration.

### The follower behind the load tick, and the chain after it (fixed 2026-09-25)

Measured on n64lle's two-process sweep (n64lle `docs/NETPLAY.md` §5, the
incremental shape, Pokemon Stadium) and filed here as not yet fixed: a
follower that had not yet simulated the load tick REFUSED the episode ("RB
follow refused ... no snapshot at load tick") -- 2 of 57 episodes at 0 ms in
one run, 24 of 56 in a rerun of the same cell -- and after that abort the
initiator's chain stalled ("RB chain stall", advisory) on the very tick of the
NACKed episode.

**Root cause (MISSING, not WRONG).** Nothing wrote a bad snapshot: the
snapshot keyed `load` did not exist yet on the follower, because its producer
-- the follower's own live admit of tick `load` -- had not run. Every refusal
line's ring window put the newest snapshot at `load-1` to `load-4` (e.g.
"span=810..810 ... ring oldest=767" at depth 40): the follower was one to four
fields behind the initiator, which detects an injected mispredict at `t+1`
and loads `t`. Which peer leads is a matter of whose fields ran slower, so the
rate swung run to run with machine load. The follower had nothing to correct
-- it would simulate that tick on the true rows anyway -- but its NACK aborted
an episode the initiator had already replayed. The stall was a second MISSING
effect: the replay corrected the tick, but only a commit re-primed the chain,
so the chain kept the live, mispredicted digest that no replay path wrote over.

**Fix** (both in the driver, so every engine inherits it; see the rules
above): the follower takes the load snapshot at its live tip when `sim ==
load`, holds the BEGIN until it gets there when `sim < load`, and a completed
replay writes its digests into the chain and sends them. Considered and not
built: the initiator refusing to open until the slowest peer's confirmed
frontier passes the load tick. It learns that frontier only from FRAME_COMMITs,
half a round trip after the fact, so it would delay every episode by at least
what the follower-side hold costs plus that trip, and with more seats wait for
the slowest peer every time; the follower knows its own tick exactly.

**Measured** (n64lle `feat/rollback-nseat`, `tools/rb_loopback.sh`, 0 ms, 45 s,
D=8 P=12, injector every 45 remote rows, homeserver, other sessions' runs
beside it). Before, same machine and day, three runs of the unchanged build:
5 of 32, 8 of 29 and 0 of 55 episodes refused, with 7, 10 and 0 chain-stall
lines. After, five runs: **0 refused of 236** (56, 54, 55, 36, 35 episodes;
ledger residual 0 and 0 forks in each), **0 chain stalls**. The follower was
at or short of the load tick in 12 of those episodes (1, 3, 0, 7, 1 per run --
most in the two runs on a loaded machine, live fields 16-20 ms at the median);
7 of the 12 waited for it, 22 to 114 ms (median 55 ms), and all 12 followed.
Again with the re-sends below (bb8096a), five runs: 0 refused of 271, 0
chain stalls; the follower was at or short of the load tick 30 times, 16 of
them waited (12 to 92 ms), all followed. And on 93e7d5a (the IDENT pacing),
five more: 0 refused of 276, 0 stalls, 74 at or short of the load tick, 9
waited (33-34 ms), all followed. n64lle's 14-cell
sweep on it: 14/14, 0 forks, 0 NACKs in every cell. `rb_driver_test`'s
follower-behind cells (the lagging seat stalls 50 ms every 10 ticks) hold it:
on e06b75f they refuse 7 and 7 episodes and stall 6 and 7 times; here 0 and 0.

**Also seen before the fix, through n64lle's lobby** (filed from branch
feat/n64-lobby-notes, kept as the record):

- **Measured again through n64lle's lobby (2026-09-25, `tools/rb_lobby.sh`,
  online through a local recomp-net-server's input relay, 0 ms): the stall
  does not always age out.** In one of fourteen two-process lobby matches
  the follower refused the episode at tick 810 ("no snapshot at load tick",
  ring oldest 770), the chain stalled there, and then the follower refused
  EVERY later episode too (16 refusals in all, each "ring oldest = load -
  40", i.e. one tick short), and `confirmed_through` stayed at 809 to the end of the match
  (sim 1501, 691 ticks later) on both peers. No fork, the match drained, the
  initiator's 33 corrections all changed its guest -- but for the last 45 % of
  that match nothing past tick 809 was ever confirmed. The other thirteen
  matches' stalls (0-4 per match) all cleared. So "waits for the tick to age
  out" is not a bound: once the follower sits exactly one tick behind every
  load tick, nothing ages. (n64lle `docs/NETPLAY.md` §7.)

Same root cause -- the follower one tick short of every load tick -- which the
hold above now covers. The lobby case has NOT been re-run on the fixed driver.

### More than two peers

**Run, 2026-09-25**, on two harnesses: `rb_driver_test` (three and four forked
toy-engine peers) and n64lle's `tools/rb_loopback.sh` with `RB_LOOPBACK_SEATS`
(three and four Pokemon Stadium processes). Transport: the session's LAN hub
(`rnet_session_start_lan_hub`) -- seat 0 relays, every other seat dials it.
The first run of each found what "built, not exercised" had hidden:

| found | was | now |
|---|---|---|
| hash chain | one peer ring; every peer's FRAME_COMMIT landed in it, so a tick was "confirmed" against whichever peer spoke last | one chain per peer (the session records each commit's sender); a tick is confirmed when every peer's chain has it; stalls name the seat |
| boot-digest gate | read tick 0 from that shared ring: the first peer to land decided | waits for and compares every peer's tick 0 |
| mod-set handshake | the session kept one ACK for all seats, and the host settled on the first | one ACK per seat; the host settles when every peer has confirmed; a guest answers every copy |
| identity | one peer slot, and the sender stopped once any peer's was in | per seat; sent until every peer's is in |
| invent cap and pacing | the scheduler read the session's highest remote tip -- the MAX over seats -- so a peer that stopped sending hid behind the others: three toy peers ran 1,000 ticks ahead of a fourth stuck at 221, which never caught up | pacing reads the slowest peer's tip, the invent decision each seat's own (`rnet_session_remote_tip`) |
| dual initiation | the rule compared our own seat, right only when we are an initiator; a follower of the losing episode NACKed the winner and both died | compares the held episode's initiator, so a follower switches too (two seats: unchanged) |
| early answers | opening a follow discarded answers held for any other epoch; a follower that briefly followed the losing episode threw away the winner's POST and timed out | kept while their episode can still open |
| rb traffic filter | chosen by seat count | by the expected set (a four-seat room with one other occupant has one peer) |
| coordinated stop | one peer's BYE stopped our markers to everyone | only with a single peer |
| control queues | 8 per kind, FRAME_COMMIT 64 | 32, and 256 (four peers' commits fill 64 in ~20 ticks of an incremental replay) |

Loss scales with the seats (see "POST and BEGIN are re-sent"): before the
re-sends, four seats at 2 % loss froze on 9 and 11 POST watchdogs in 45 s and
`rb_driver_test`'s 4-seat 2 % cell forked (a lost BEGIN, then a correction
that could never reopen).

**Measured** (n64lle, Pokemon Stadium attract scene, recomp-net bb8096a -- the
IDENT pacing of 93e7d5a came after these runs and was not re-run here -- 45 s,
D=8 P=12, seat 1 injecting every 45 remote rows -- every 15 ticks with four
seats -- two runs per cell, homeserver, other sessions' runs beside it):

| cell | episodes | answers owed / followed / refused | watchdogs | forks | drained | deferred / at tip |
|---|---|---|---|---|---|---|
| 3 seats, 0 ms | 106, 106 | 212/212/0, 212/212/0 | 0, 0 | 0 | all | 4 / 28, 6 / 11 |
| 3 seats, 200 ms | 66, 65 | 132/132/0, 130/130/0 | 0, 0 | 0 | all | 0 / 0 |
| 4 seats, 0 ms | 146, 148 | 438/438/0, 444/444/0 | 0, 0 | 0 | all | 5 / 28, 4 / 20 |
| 4 seats, 200 ms | 79, 78 | 237/237/0, 234/234/0 | 0, 0 | 0 | all | 0 / 0 |
| 4 seats, 2 % loss | 122, 126 | 366/366/0, 378/378/0 | 2, 2 | 0 | all | 9 / 23, 12 / 28 |

Residual 0 in every run, graded per initiator/follower pair by epoch. The
same 4-seat 2 % cell before the re-sends (recomp-net 5492a31): 30 and 67
episodes, 12 and 9 watchdogs, one residual covered by a watchdog, 17
corrections LOST in one run (to a port the scene never reads, so nothing
forked). Per-peer counts are in n64lle `docs/NETPLAY.md` §5.

**A lost START (found 2026-09-25, final regression of the merged branches).**
`rb_driver_test`'s 4seat-loss2 cell failed 2 of 10 full-suite runs (0 of 12
run alone): seat 2 never reached RUNNING, so no seat simulated a tick. Cause:
the session sent `START` exactly once; a seat's 2 % receive loss can drop it,
and nothing else moves a seat from READY to RUNNING (the injected loss is
seeded per seat, so whether the dropped datagram is START depends on arrival
order -- hence the suite-vs-alone difference). Forcing one START drop at seat
2 reproduced the exact failure (13 identical FAIL lines). The authority now
answers a READY that arrives after it started with START again (`architecture.md`,
session phases); with the same forced drop the cell passes. Two seats were
exposed the same way: one forced START drop at mixed-rtt60's seat 1 fails
unfixed (seat 1 never RUNNING, sim 0 on both) and passes fixed.

Seen in the same regression and NOT this defect: 4seat-rtt200's "no chain
stall" check failed 1 of 8 runs alone on the unfixed tree (one seat stalls=1
after a dual-initiation abort, 0 % loss) and 1 of 4 full runs on the fixed one.
Open; not root-caused.

**Sparse rooms (found 2026-09-25).** A room whose occupied seats are not
`0..n-1` -- seats 0 and 2 of 4 -- forked in `rb_driver_test`'s new
`sparse-0+2-of-4` cell 3 of 8 runs (60 ms RTT, incremental). The injector
invented a row for empty seat 3; the correction episode then waited on seat
3's SEAL_ROWS, which nobody sends: the core's
`rnet_rb_all_peer_seal_rows_complete` waited on every seat below `slot_count`,
and the driver sealed seat 3 from its history ring, which still held the
invented row. Watchdog, "correction LOST", POST fork. Now
`RNetRbConfig.occupied_mask` (0 = every seat, as before) limits the wait to
occupied seats (`rnet_rb_expected_peer_mask`; the driver passes its own
`occupied_mask`), and the driver seals an empty seat with the zero sample
every peer's session synthesizes for it. Measured apart: the wait set alone
turned the timeout into "sealed row missing mid-replay slot=3" (3 of 6 runs
failed); the seal source alone passed 6 of 6. Both halves are kept -- the
core must never invent the row, and does not (`rollback_episode_test`,
`test_sparse_room`). After both: 8 of 8 clean, 0 ms and 60 ms cells.

**Not covered.** One injecting seat: organic episodes from several seats at
once (true dual and triple initiation under a real game) have run only in
`rb_driver_test`. The relay is seat 0's own process, so a relay that is slow
or leaves takes the room with it; no relay-less mesh exists. Guest-to-guest
traffic crosses the relay's poll, which adds up to one of its fields of
latency. A seat leaving mid-match is untested with more than two. Loopback,
not a network; nobody has played it.

### Observers (spectators), fixed 2026-09-25

An observer is a session whose `local_slot == slot_count` (rollback.h): it
simulates every seat from the wire and owns none. Until this date the driver
had no observer path beyond the seal export, and an observer behaved like a
player with a seat nobody reads: it PREDICTED remote rows, opened episodes
that no player answers (a spectator relay drops everything it sends), refused
every player's BEGIN ("no local row at tick" -- for its sentinel seat), and fell
behind. Measured in nesrecomp's `rb_lobby.sh` (4 seats + 1 spectator on
recomp-net-server's relay): 403 invents, 6 episodes opened, every BEGIN NACKed,
600 ticks behind, and a rematch that never connected.

Now: an observer pins lockstep (`lockstep_env` reads `observer`), so it only
admits a tick on every seat's wire row and never has anything to roll back;
it ignores peer BEGINs (logged "RB observer ignores episode", counted), and
`rb_local_rows_cover` / `rb_local_slot` treat the sentinel -- including 8 in a
full 8-seat room, which used to clamp to seat 0 -- as owning no rows. It still
checks every player's FRAME_COMMIT chain and the boot digest, so a spectator
that forks says so. `rb_driver_test 4seat-observer-rtt60`: the observer opens,
follows and refuses nothing, keeps up (sim 436 vs seat 0's 431), drains and
agrees with seat 0's timeline; on 03ee1b1 the same cell fails (2 opened, 17
refused, 21 chain stalls).

## Portable input contract

Pure decision core (no engine includes) for "published row vs late authoritative
row → rewind or promote?". All game-specific behavior enters through
`RNetInputContractParams` (numeric thresholds) and `RNetInputContractHostGates`
(optional host callbacks queried lazily in decision order).

Master entry point:

```c
RNetInputContractDecision d = rnet_input_contract_stick_replace_decide(
    &published, &wire, completed_sim, &params, &gates);
if (rnet_input_contract_decision_is_rewind(d)) { /* queue rollback */ }
```

Host-binding guidance (master-hash + savestate recomp host): bind only
`hash_confirm_promote` (= "peer master hash agreed through tick") and leave the
rest NULL (portable defaults).

## Invariants (soak-derived; do not relax without a new soak)

- Predicted rows never get a bare deadband promote — only `hash_confirm` or a
  host protect.
- Release always rewinds on both completed-sim and runway paths.
- Dash-gate X disagree blocks all same-intent promotes.
- `hash_confirm_promote` fails closed when NULL.

## Admission scheduler

`rnet_sched_bind` takes live pointers into the host session (`RNetSchedBridge`)
and game-specific gates (`RNetSchedGates`). Minimum for non-media digital
titles (SNES / NES):

| Gate | Required? | Notes |
|------|-----------|--------|
| `now_ms` | **yes** | Monotonic; retcomm-rbengine's `rbe_mono_ms` is suitable |
| `rtt_ms` | recommended | ICE/POST sample; 0 = synth from D |
| `episode_active` | recommended | 1 during Seal/Replay/Verify |
| `tip_holding` | recommended | TipHold Live invent-cap |
| media / lockstep | optional | MotK FMV only |

NULL media gates → never lockstep-stall invent; auto-D always samples.

Env knobs keep the `RBE_` names they had in retcomm-rbengine so existing tuning
and soak scripts keep working; MotK-era `PSX_RB_*` /
`PSX_NETPLAY_CROSS_OS_PACING_DIAG` names are honoured when the `RBE_*` one is
unset.

| Variable | Effect |
|----------|--------|
| `RBE_RB_ZERO_DELAY=1` | Legacy consume wire=`sim+D` (no cushion) |
| `RBE_RB_INVENT_GRACE_MS` | Floor ms before invent (default 8, clamped 0–200) |
| `RBE_RB_GAP1_GRACE_MS` | Flat gap=1 grace override |
| `RBE_RB_GAP1_INVENT=0` | Wait for tip-stale instead of gap1 invent |
| `RBE_RB_TIMESYNC=0` | Disable mispredict pacing debt |
| `RBE_RB_AUTO_DELAY=0` | Disable arrival-driven D controller |
| `RBE_RB_ADAPT_DELAY=0` | Disable pcap-freeze D bumps |
| `RBE_CROSS_OS_PACING_DIAG=1` | 1 Hz pacing diag line |

## Host guarantees for rollback mode

- Deterministic `advance_sim` for a given input set.
- Snapshot save/load at any sim tick the library requests.
- State digests suitable for agreement comparison across peers.
- Single-thread session ownership (same as delay-sync).

## Not in this library

- Automatch / lobby server (see `recomp-net-server`).
- Game-specific pad layouts, snapshots, or determinism fixes.
- Delay-sync behavior changes (existing `RNetSession` is untouched).
