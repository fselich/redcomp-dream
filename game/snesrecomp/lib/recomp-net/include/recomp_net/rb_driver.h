#ifndef RECOMP_NET_RB_DRIVER_H
#define RECOMP_NET_RB_DRIVER_H

/*
 * The rollback EPISODE DRIVER -- the code that actually runs an episode.
 *
 * RNetRbSession (rollback.h) is the passive half: it stores the phase, the
 * correction tuple, the sealed rows and the resolved-through watermark, and
 * the only host callback it ever invokes is get_input_row. Everything that
 * decides WHEN to rewind, talks to the peer about it, replays, verifies and
 * recovers lived in each engine's host until this driver was lifted out of
 * snesrecomp's snes_netplay_rb.c (2026-09-24). An engine now supplies only
 * what is genuinely its own -- snapshots, one tick of simulation, digests,
 * pad layout and presentation suppression -- through RNetRbHost, and inherits
 * every fix to the rest.
 *
 * What the driver owns:
 *   - live admit: local tip, remote rows, hold-last invent, the admission
 *     scheduler (sched.h) and every gate it asks for;
 *   - reconcile: late wire against predicted history -> open an episode,
 *     tip-extend, or log the loss (never silently);
 *   - the episode FSM: open / follow / dual-initiation arbitration / NACK,
 *     seal-row exchange, baseline digest gate, replay, POST verify, commit,
 *     tip-hold and tip-extend, abort classes and mirrored cooldowns, a
 *     watchdog on every stage that waits on a peer;
 *   - snapshot policy: interval, floor search, the baseline fork cap;
 *   - the hash chain (FRAME_COMMIT), the boot-digest gate, the mod-set and
 *     identity handshakes, lockstep degrade, the advisory chain-fork report;
 *   - cold reset on start: the struct is wiped and what survives is named.
 *
 * What the host owns (RNetRbHost): snapshot storage, publishing a tick's rows
 * and running one tick, digests, pad decode / sanitize / neutral per seat,
 * resim begin/end, boot diagnostics, a log sink and a clock.
 *
 * Two replay shapes, chosen by RNetRbDriverConfig.replay_mode:
 *
 *   INLINE       the driver loads the baseline and runs every replayed tick
 *                inside one poll_admit call through host->run_tick. Right when
 *                a tick is cheap and returns (SNES RtlRunFrame).
 *   INCREMENTAL  poll_admit publishes ONE replayed tick and returns
 *                RNET_RB_ADMIT_REPLAY; the host runs it with the same per-tick
 *                function it uses for Live, then calls finish_frame, then may
 *                pump / present / service audio before polling again. Right
 *                when a tick is expensive or must run in the host's own loop
 *                (an N64 field). Episode wire is not drained mid-replay in
 *                either shape, so the two are behaviourally identical.
 *
 * Seats: 1..RNET_RB_MAX_SLOTS. Epoch ids carry the initiator's seat in their
 * low RNET_RB_EPOCH_SLOT_BITS bits, so dual initiation is arbitrated by seat
 * (lower wins) without a wire change, and BASELINE / POST / COMMIT, the
 * FRAME_COMMIT hash chain, the boot digest, identity and the mod-set answer
 * are all tracked per peer (rnet_session_rb_last_take_from). With two seats
 * every behaviour is the one snesrecomp shipped. Three and four seats have
 * run: rb_driver_test (forked peers over the session's LAN hub) and n64lle's
 * tools/rb_loopback.sh (RB_LOOPBACK_SEATS); see docs/rollback.md, "More than
 * two peers", for what that took and what it has not covered.
 *
 * The admission scheduler is process-global (rnet_sched_bind), so one driver
 * may be started per process at a time.
 *
 * Single-threaded, like the session: every call on the host's sim thread.
 */

#include <stddef.h>
#include <stdint.h>

#include "recomp_net/input.h"
#include "recomp_net/rollback.h"
#include "recomp_net/session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Epoch ids: (sequence << RNET_RB_EPOCH_SLOT_BITS) | initiator seat. */
#define RNET_RB_EPOCH_SLOT_BITS 3u
#define RNET_RB_EPOCH_SLOT_MASK ((1u << RNET_RB_EPOCH_SLOT_BITS) - 1u)
static inline uint32_t rnet_rb_epoch_make(uint32_t seq, uint32_t initiator_slot)
{
    return (seq << RNET_RB_EPOCH_SLOT_BITS) | (initiator_slot & RNET_RB_EPOCH_SLOT_MASK);
}
static inline uint32_t rnet_rb_epoch_initiator(uint32_t epoch)
{
    return epoch & RNET_RB_EPOCH_SLOT_MASK;
}

typedef enum RNetRbReplayMode
{
    RNET_RB_REPLAY_INLINE = 0,
    RNET_RB_REPLAY_INCREMENTAL = 1
} RNetRbReplayMode;

typedef enum RNetRbAdmit
{
    /* Nothing to run this iteration: present the held frame, pump, retry. */
    RNET_RB_ADMIT_STALL = 0,
    /* Rows for the live tick were published: run one tick, then finish_frame. */
    RNET_RB_ADMIT_LIVE = 1,
    /* INCREMENTAL only: rows for one REPLAYED tick were published (inside
     * resim_begin/resim_end). Run it exactly as a live tick, then
     * finish_frame. Presentation is the host's to suppress. */
    RNET_RB_ADMIT_REPLAY = 2
} RNetRbAdmit;

/* Master digest plus the three partitions the BASELINE wire carries. A
 * baseline fork names the first partition that differs (part_names). */
typedef struct RNetRbDigestParts
{
    uint32_t master;
    uint32_t part[3];
} RNetRbDigestParts;

/*
 * Engine-specific pieces. REQUIRED unless marked optional; start() refuses,
 * naming the missing callback, when one is absent.
 */
typedef struct RNetRbHost
{
    void *ctx;

    /* ── snapshots (the host owns storage; recomp-net never sees bytes) ──
     * A snapshot keyed T is the state BEFORE tick T runs, so loading T and
     * replaying T..target re-runs the mismatch tick itself. */
    int (*snap_save)(void *ctx, uint32_t tick);
    int (*snap_load)(void *ctx, uint32_t tick);
    int (*snap_has)(void *ctx, uint32_t tick);
    /* 1 and *oldest set when the store holds anything; 0 when empty. */
    int (*snap_oldest)(void *ctx, uint32_t *oldest);
    /* Drop every snapshot keyed after `tick` (a replay re-keyed the timeline). */
    void (*snap_drop_after)(void *ctx, uint32_t tick);

    /* ── one tick ──
     * publish: the rows every seat simulates at `tick` (sanitized). Called
     * for every live tick from poll_admit, and for every replayed tick. */
    void (*publish)(void *ctx, uint32_t tick, const RNetRbFrame *rows, int slots, int replay);
    /* INLINE mode only (required there, ignored otherwise): run one tick of
     * simulation on the rows last published. Return 0 on failure. */
    int (*run_tick)(void *ctx, uint32_t tick);
    /* Bracket a replay: suppress presentation / rewind audio production so
     * ticks the player already saw and heard are not repeated. */
    void (*resim_begin)(void *ctx);
    void (*resim_end)(void *ctx);

    /* ── digests (simulation state only; identical across peers) ── */
    uint32_t (*digest_master)(void *ctx);
    void (*digest_parts)(void *ctx, RNetRbDigestParts *out);

    /* ── pads ──
     * decode: fill buttons / sticks / analog of `out` from an input sample.
     * tick, is_valid (1) and is_predicted (0) are already set. */
    void (*decode_sample)(void *ctx, int slot, const RNetInputSample *in, RNetRbFrame *out);
    /* Optional: force a row into the engine's legal domain (mask bits, clear
     * sticks on a digital pad, map a foreign neutral). NULL = rows as-is. */
    void (*sanitize_row)(void *ctx, int slot, RNetRbFrame *row);
    /* The row a seat holds when nothing is pressed. Active-high pads (SNES,
     * N64) and active-low pads (PSX) differ; the driver never assumes. */
    void (*neutral_row)(void *ctx, int slot, RNetRbFrame *out);
    /* Optional: the sample a LIVE admit took a seat's row from, for side data
     * that rides the pad bytes (SNES sync bytes). Never called from replay,
     * seal or reconcile. */
    void (*admit_sample)(void *ctx, int slot, uint32_t tick, const RNetInputSample *in);

    /* ── session control ── */
    /* Optional: tick 0's digest was just latched -- log whatever explains a
     * boot mismatch (partitions, frame counters). */
    void (*boot_digest_noted)(void *ctx);
    /* The match is REFUSED (boot fork, mod-set refusal): end it and go back
     * to the lobby. Called once per match; the reason is
     * rnet_rb_driver_refusal(). From the refusal on the driver admits no Live
     * tick, so a host that is slow to act stalls rather than plays a match
     * that was declared unplayable -- but LEAVING is the host's job: tear the
     * session down and soft-return (a host that never consumes this request
     * leaves its players on a frozen frame; NETPLAY.md section 4). */
    void (*request_return_to_lobby)(void *ctx);

    /* Optional log sink: one complete line (with '\n') per call. NULL writes
     * to stderr. The wording is parsed by harnesses (rb_loopback.sh ledger);
     * change a line only together with every script that reads it. */
    void (*log)(void *ctx, const char *line);
    /* Monotonic milliseconds. */
    uint32_t (*now_ms)(void *ctx);
} RNetRbHost;

typedef int (*RNetRbModSetCheckFn)(const char *want, char *reason, uint32_t cap);
typedef int (*RNetRbModSetAdoptFn)(const char *want, char *reason, uint32_t cap);

typedef struct RNetRbDriverConfig
{
    /* Live pointers into the host's session state, exactly as the admission
     * scheduler takes them (sched.h RNetSchedBridge). session is required. */
    RNetSession **session;
    int *local_slot;       /* NULL -> 0 */
    int *slot_count;       /* NULL -> 2 */
    int *input_delay;      /* NULL -> 2 */
    /* Session-settled prediction cap P (recomp-ui: P = 4 + D). NULL or < 2 ->
     * the same rule applied locally, clamped 6..16. Env overrides. */
    int *input_prediction;
    int force_turn;
    /* Seats that take part (bit i = seat i has a peer). 0 = every seat in
     * [0, slot_count). A sparse room must clear empty seats, or every episode
     * waits on a POST nobody will send. */
    uint32_t occupied_mask;

    RNetRbReplayMode replay_mode;
    /* Names of RNetRbDigestParts.part[0..2], for fork reports. */
    const char *part_names[3];
    /* Reporting only (the host owns the store): ring depth in slots, printed
     * in the start banner as reach = depth x snap interval. */
    uint32_t snap_depth;

    /* Tagged log lines read "<log_prefix>: ...". NULL -> "rnet_rb". */
    const char *log_prefix;
    /* Environment knobs are read as RNET_RB_<NAME>; when env_alias is set,
     * "<env_alias>_<NAME>" is consulted FIRST, so an engine's existing names
     * (SNES_RB_*) keep working and a harness that pins "<alias>_X=0" for one
     * peer cannot be overridden by a generic name leaking from the parent.
     * NAME: PREDICTION SNAP_INTERVAL EPISODE_TIMEOUT_MS TIP_RUNWAY
     * LOCKSTEP LOCKSTEP_TICKS FORCE_FORK FORCE_MISPREDICT FORCE_BOOT_FORK
     * FORCE_MOD_MISMATCH FORCE_MODSET ALLOW_BOOT_FORK ALLOW_MOD_MISMATCH. */
    const char *env_alias;
    /* Validation only (FORCE_MISPREDICT): the button bits flipped in an
     * invented row so the prediction is guaranteed wrong. 0 = 0x0040, the bit
     * snesrecomp's injector always flipped. It must be a bit the engine's pad
     * layer passes to the guest: n64lle's n64_si_set_pad masks 0x0040 as
     * unmodeled, so there the historical bit opened episodes whose mispredicted
     * fields the guest never saw -- a rollback whose load restored nothing
     * would have passed them. Appended last so a zeroed config keeps the old
     * behaviour. */
    uint16_t inject_flip_bits;
} RNetRbDriverConfig;

typedef struct RNetRbDriver RNetRbDriver;

RNetRbDriver *rnet_rb_driver_create(void);
void rnet_rb_driver_destroy(RNetRbDriver *d);

/* Identity and mod set are properties of the PROCESS, not of a match: they
 * survive start()'s cold reset. Set them before start(). Fingerprints are
 * opaque and compared for equality; 0 = not supplied. */
void rnet_rb_driver_set_identity(RNetRbDriver *d, uint32_t build_fp, uint32_t content_fp);
void rnet_rb_driver_set_modset(RNetRbDriver *d, const char *text, RNetRbModSetCheckFn check,
                               RNetRbModSetAdoptFn adopt);

/*
 * Start a match. Cold reset: everything from a previous match is wiped except
 * identity and mod set, then cfg/host are taken, the session and snapshot
 * policy built, the admission scheduler bound and a neutral row seeded per
 * seat. Returns 1 on success; 0 with a logged reason otherwise.
 */
int rnet_rb_driver_start(RNetRbDriver *d, const RNetRbDriverConfig *cfg, const RNetRbHost *host);
void rnet_rb_driver_shutdown(RNetRbDriver *d);

/*
 * The host loop, both shapes:
 *
 *   for (;;) {
 *       RNetRbAdmit a = rnet_rb_driver_poll_admit(d);
 *       if (a != RNET_RB_ADMIT_STALL) {
 *           run_one_tick();                 // the SAME function for both
 *           rnet_rb_driver_finish_frame(d);
 *       }
 *       present_and_pump();                 // held frame during replay
 *   }
 */
RNetRbAdmit rnet_rb_driver_poll_admit(RNetRbDriver *d);
void rnet_rb_driver_finish_frame(RNetRbDriver *d);

/*
 * Coordinated stop: drain, then tell the host it may exit.
 *
 * A harness that simply kills both peers at a wall-clock deadline cannot tell
 * an episode that was in flight at the kill from one that was lost: the
 * initiator logged its BEGIN, the follower died before reading it, and the
 * ledger reports an unanswered episode that nothing was wrong with (measured
 * on SNES: the runway-4 cell failed 1 of 1 in a sweep and passed 4 of 4 on
 * repeat). Tolerating that residual would put the coin flip back into the
 * verdict, so the stop is made exact instead.
 *
 * After request_quiesce this peer opens no new episode -- a mispredict found
 * while draining is logged ("RB drain: correction not opened") and counted,
 * never silently dropped -- no local tip-extend, and the validation injector
 * stops. It still FOLLOWS a peer's BEGIN, and every episode already open
 * finishes (commit, tip-hold, or an abort that says why). Once idle it tells
 * each peer so (RNET_RB_SYNC_OP_QUIESCE: "I will open no more episodes"). A
 * peer that receives that marker starts draining too, so asking one side
 * drains the match. The state reaches DRAINED when this peer is idle and holds
 * every peer's marker; by then no episode either side opened can still be
 * unanswered, so the two ledgers balance by construction. Live keeps running
 * throughout: the host keeps calling poll_admit / finish_frame until DRAINED.
 *
 * Keep calling poll_admit while draining EVEN IF the session is no longer
 * running: a peer that finished first leaves, its BYE stops our session, and
 * its last marker may still be queued. poll_admit reads it and finishes (or
 * times out) the drain from there; a host that stops calling on a dead session
 * never reaches DRAINED.
 *
 * Bounded: TIMED_OUT after RNET_RB_QUIESCE_TIMEOUT_MS with a line naming what
 * was still outstanding (a peer that vanished, or predates the marker).
 * Idempotent. Harnesses send it on a signal (snesrecomp: SIGUSR1); the host
 * decides what triggers it.
 */
typedef enum RNetRbQuiesce
{
    RNET_RB_QUIESCE_NONE = 0,      /* not requested */
    RNET_RB_QUIESCE_DRAINING = 1,  /* requested; episodes or peers outstanding */
    RNET_RB_QUIESCE_DRAINED = 2,   /* nothing in flight on either side: exit */
    RNET_RB_QUIESCE_TIMED_OUT = 3  /* bound expired: exit, ledger not guaranteed */
} RNetRbQuiesce;

#define RNET_RB_QUIESCE_TIMEOUT_MS 10000u

void rnet_rb_driver_request_quiesce(RNetRbDriver *d);
RNetRbQuiesce rnet_rb_driver_quiesce_state(const RNetRbDriver *d);

/*
 * Session hold: every peer stops on the same tick (a paused menu).
 *
 * set_hold(tick): Live admits no tick at or past `tick` and nothing is
 * predicted while the hold is set, so the local pad is not sealed for any
 * tick while held. Episodes, the wire and frame commits keep running. Set it
 * on every peer with the same tick, far enough ahead (input delay plus the
 * prediction window) that no peer has already passed it.
 *
 * hold_settled(): 1 once the sim stands at the hold tick, no episode is
 * open and no remote row before it is predicted -- the state there is final
 * and every peer that also settled holds the same one.
 *
 * clear_hold(): resume. Live continues from the hold tick with the rows the
 * peers had already published, in the same epoch.
 */
void rnet_rb_driver_set_hold(RNetRbDriver *d, uint32_t tick);
void rnet_rb_driver_clear_hold(RNetRbDriver *d);
int rnet_rb_driver_hold_settled(const RNetRbDriver *d);

/* Diagnostics. */
uint32_t rnet_rb_driver_sim_tick(const RNetRbDriver *d);
uint32_t rnet_rb_driver_episode_count(const RNetRbDriver *d);
uint32_t rnet_rb_driver_invent_count(const RNetRbDriver *d);
uint32_t rnet_rb_driver_promote_count(const RNetRbDriver *d);
uint64_t rnet_rb_driver_resim_ticks(const RNetRbDriver *d);
uint32_t rnet_rb_driver_desync_count(const RNetRbDriver *d);
/* POST-handshake RTT EMA in ms; 0 until an episode has round-tripped. */
uint32_t rnet_rb_driver_rtt_estimate_ms(const RNetRbDriver *d);
/* Digest-agreed watermark, and how far the local sim runs past it. */
uint32_t rnet_rb_driver_confirmed_through(const RNetRbDriver *d);
uint32_t rnet_rb_driver_confirmed_remaining(const RNetRbDriver *d);
int rnet_rb_driver_episode_active(const RNetRbDriver *d);
/* Bounded snapshot for a stalled match report. */
void rnet_rb_driver_debug_state(const RNetRbDriver *d, char *out, size_t cap);
/* 1 between resim_begin and resim_end. */
int rnet_rb_driver_in_resim(const RNetRbDriver *d);
const char *rnet_rb_driver_stall_tag(const RNetRbDriver *d);
/* Why this match was refused, NULL if it was not: "boot_digest_mismatch" (the
 * peers did not start from the same state), "mod_set_mismatch" (the peers'
 * content fingerprints differ), "mod_set_not_agreed" (the host's set could not
 * be confirmed or honoured). Stable codes, suitable for a launcher's
 * last_error. Latched until the next start. */
const char *rnet_rb_driver_refusal(const RNetRbDriver *d);
/* Last digest fork: tick + partition name, and both peers' master digests. */
int rnet_rb_driver_last_fork(const RNetRbDriver *d, uint32_t *tick, const char **partition);
int rnet_rb_driver_fork_digests(const RNetRbDriver *d, uint32_t *mine, uint32_t *theirs);

#ifdef __cplusplus
}
#endif

#endif /* RECOMP_NET_RB_DRIVER_H */
