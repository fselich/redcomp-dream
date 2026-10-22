/*
 * The rollback episode driver. See include/recomp_net/rb_driver.h.
 *
 * Lifted from snesrecomp runner/src/netplay/snes_netplay_rb.c (36d6ce5,
 * 2026-09-24), where it was proven by a two-process harness over a link
 * simulator: 13 sweep cells, 0 forks and 0 ledger residual at 0/60/200/300 ms
 * RTT. The comments that explain WHY a line is the way it is came with it --
 * most of them record a measured failure, and a driver that forgets why it
 * does something is one refactor away from undoing it.
 *
 * Log wording is load-bearing: snesrecomp tools/rb_loopback.sh and
 * tools/rb_sweep.sh count lines by substring ('RESIM episode', 'RB abort',
 * 'FORK', 'RB follow refused', 'timed out waiting', 'RB tip-extend epoch',
 * 'RB chain stall', 'UNAPPLIED stage=', 'BOOT DIGEST MISMATCH', 'MOD SETS
 * DIFFER', 'MOD SET NOT AGREED', 'forced late row', 'tip-hold for', 'RB
 * tip-hold ended ... held=', 'RB quiesced', 'RB quiesce TIMED OUT', 'RB drain:
 * correction not opened', 'tip_runway='), and n64lle's tools/rb_loopback.sh
 * also reads 'RB follow deferred epoch=N span=', 'RB follow at our live tip'
 * and the trailing 'epoch=N' of the RESIM line (its per-pair ledger). Change a
 * line only with every script that parses it.
 */

#include "recomp_net/rb_driver.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_net/hash_confirm.h"
#include "recomp_net/input_hist.h"
#include "recomp_net/rb_post.h"
#include "recomp_net/sched.h"

#define RB_MAX_SLOTS RNET_RB_MAX_SLOTS

typedef enum RbEpisodeStage {
    kRbIdle = 0,
    kRbSealing,      /* rows sealed locally; waiting on peer seal rows */
    kRbReplaying,    /* baseline loaded; resim in progress */
    kRbVerifying,    /* POST sent; waiting for peer POST */
    kRbTipHold       /* POST matched; Live runs while seals stay open */
} RbEpisodeStage;

static const char *rb_stage_name(RbEpisodeStage st)
{
    switch (st) {
    case kRbIdle:      return "idle";
    case kRbSealing:   return "sealing";
    case kRbReplaying: return "replaying";
    case kRbVerifying: return "verifying";
    case kRbTipHold:   return "tiphold";
    }
    return "?";
}

struct RNetRbDriver {
    RNetRbDriverConfig cfg;
    RNetRbHost         host;
    RNetRbSession     *rb;
    RNetInputHist      ih;
    /* Late-wire corrections that arrived while an episode was open and were
     * therefore never resimulated, per stage. See rb_reconcile_wire. */
    uint32_t           drop_stage_n[kRbTipHold + 1];
    /* The FRAME_COMMIT hash chain, one per peer seat. With two seats only
     * the one peer's is used and every rule is the one snesrecomp shipped.
     * With more, a single ring took every peer's commits, so a tick was
     * "confirmed" against whichever peer spoke last and the boot gate read
     * whichever tick-0 digest landed first. A tick is confirmed when EVERY
     * peer's chain has it (rb_hc_resolved). */
    RNetHashConfirm    hc[RB_MAX_SLOTS];

    int      started;
    uint32_t sim;            /* authoritative local sim tick */
    RNetRbFrame rows[RB_MAX_SLOTS];   /* what each seat simulates this tick */

    /* Episode */
    RbEpisodeStage   stage;
    RNetRbCorrection corr;
    int              initiator;
    uint32_t         epoch_seq;
    uint32_t         local_post_digest;
    /* Per-peer answers. Bit i = seat i's reply for THIS episode is held. With
     * two seats there is one bit and every rule below is the one snesrecomp
     * shipped; with more, an episode needs every peer's BASELINE and POST, not
     * whichever arrived first. */
    uint32_t         peer_post_mask;
    uint32_t         peer_post_digest[RB_MAX_SLOTS];
    uint32_t         peer_post_target[RB_MAX_SLOTS];
    uint32_t         peer_commit_mask;
    /* Baseline digest exchange. Both sides must digest the SAME tick, and a
     * peer's digest routinely arrives before we have loaded our own baseline
     * snapshot, so it is buffered until ours exists. */
    RNetRbDigestParts local_base;
    int              local_base_valid;
    uint32_t         peer_base_mask;
    RNetRbDigestParts peer_base[RB_MAX_SLOTS];
    uint32_t         cooldown_until_tick;
    uint32_t         stage_entered_ms;
    uint32_t         seal_timeout_ms;

    uint32_t tip_prepared_for;
    int      tip_prepared_valid;

    /* Resim */
    int      in_resim;
    uint32_t replay_next;        /* INCREMENTAL: next tick to replay */
    int      pending_admit;      /* RNetRbAdmit handed out, awaiting finish */

    /* Counters / diagnostics */
    uint32_t episode_count;
    uint32_t desync_count;
    uint64_t resim_ticks;
    uint32_t fork_tick;
    int      fork_seen;
    const char *fork_partition;
    /* The two digests that disagreed, kept so a report can be CORROBORATED.
     * A fork says "these two peers differ", never "the other one cheated" --
     * the only way to tell those apart is to put both sides' numbers next to
     * each other, across many matches and many opponents. */
    uint32_t fork_mine;
    uint32_t fork_theirs;
    const char *stall_tag;
    uint32_t snap_interval;
    int      prediction_cap;
    int      force_invent_slot;  /* validation knob one-shot; -1 = idle */
    /* Poisoned-snapshot bound (psxrecomp g_bl_fork_cap, §83). 0 = none. */
    uint32_t fork_cap;
    /* Tip-extends spent on the CURRENT episode. Capped: each one re-opens the
     * seal/replay/verify cycle, and an edge storm could otherwise keep one
     * episode alive indefinitely, starving the fresh episode that would cover
     * the same ticks in a single span. */
    uint32_t tip_extends;
    /* Link RTT, EMA'd from the POST handshake. See rb_gate_rtt_ms. */
    uint32_t post_sent_ms;
    /* Our POST is re-sent while a peer may still be waiting for it: through
     * Verifying and TipHold, once per round trip (at least RB_POST_RESEND_MIN_MS
     * apart). It used to go out once, and a POST lost on the link left the
     * peer that needed it in Verifying for the whole 2 s budget -- with every
     * other peer already committed. That is the dominant watchdog under loss,
     * and it grows with the seat count: a commit needs (N-1) POSTs at each of
     * N peers, so at 2 % loss the chance that one of them is lost goes from
     * 4 % of episodes with two seats to 22 % with four (measured on n64lle,
     * four seats at 2 %: 9 and 11 "timed out waiting for peer POST" in 45 s).
     * A POST is idempotent on receipt; one for an episode that has ended is
     * dropped by epoch. */
    uint32_t post_resend_ms;
    uint32_t n_post_resent;
    /* The initiator re-sends its BEGIN, same rule, until every peer has
     * answered this epoch (a SEAL_ROWS, BASELINE or POST from it): a BEGIN
     * lost at one peer left every other peer waiting in the episode for the
     * whole 2 s budget, and a peer whose own correction came due in those two
     * seconds could not open it -- measured in rb_driver_test's 4-seat 2 %
     * cell: one lost BEGIN, then a correction that every retry found other
     * peers busy for, until its replay span outgrew the 64-row seal mask and
     * that peer forked. A peer answers each BEGIN once (begin_seen); a
     * repeat of one it refused repeats the NACK, since that is what was lost. */
    uint32_t begin_resend_ms;
    uint32_t frame_commit_request_ms;
    uint32_t answered_mask;
    uint32_t n_begin_resent;
#define RB_BEGIN_SEEN 32
    uint32_t begin_seen[RB_BEGIN_SEEN];   /* epochs of BEGINs processed, a ring */
    uint32_t begin_seen_n;
#define RB_NACKED 16
    struct {
        uint32_t epoch, mismatch, load, frontier;
        int      slot;
    } nacked[RB_NACKED];                  /* BEGINs refused, a ring */
    uint32_t nacked_n;
    uint32_t rtt_ema_ms;
    /* Identity (survives start). */
    uint32_t local_build_fp;     /* our build fingerprint, 0 = not supplied */
    uint32_t local_content_fp;   /* our content/mod fingerprint */
    uint32_t peer_build_fp[RB_MAX_SLOTS];
    uint32_t peer_content_fp[RB_MAX_SLOTS];
    uint32_t peer_ident_mask;    /* seats whose IDENT is in */
    /* Mod-set handshake. The host publishes and each peer confirms; nobody
     * simulates until it has settled, because a peer running a different set
     * is running a different game. */
    const char *local_modset;
    RNetRbModSetCheckFn modset_check;
    RNetRbModSetAdoptFn modset_adopt;
    uint8_t  modset_settled;    /* handshake finished, one way or the other */
    uint32_t modset_ack_mask;   /* host: seats that confirmed the set */
    uint8_t  modset_ok;         /* ...and it finished in agreement */
    uint8_t  modset_sent;
    uint32_t modset_since_ms;
    char     modset_reason[96];
    uint8_t  ident_sent;
    uint8_t  ident_owed;         /* a peer repeated its IDENT: it may lack ours */
    uint32_t ident_sent_ms;
    /* Per peer seat, like the chains they watch. */
    uint32_t chain_fork_tick[RB_MAX_SLOTS];    /* last frame-commit fork reported */
    uint32_t chain_pending_tick[RB_MAX_SLOTS]; /* mismatch under observation, 0 = none */
    uint32_t chain_pending_ms[RB_MAX_SLOTS];   /* when we first saw it */
    uint32_t boot_dig_local;
    uint32_t boot_dig_peer[RB_MAX_SLOTS];
    uint32_t boot_dig_hold_since_ms;
    uint8_t  boot_dig_local_valid;
    uint32_t boot_dig_peer_mask;        /* seats whose tick-0 digest is in */
    uint8_t  boot_dig_settled;
    uint8_t  boot_dig_waiting_logged;
    /* The match was REFUSED (boot digest, mod set): a stable code the host can
     * show a player, NULL = not refused. Latched until the next start. From
     * the refusal on, Live admits nothing -- see rb_refuse. */
    const char *refusal;
    /* Degrade mode: stop predicting remote input until this tick. */
    uint32_t lockstep_until;

    /* Corrections owed. Reconcile promotes the true row the moment it decides
     * to rewind, so the tick is never looked at again -- if the episode that
     * decision opened never REPLAYS it (NACK, abort before the load, timeout,
     * a yield to the peer's BEGIN, a cooldown refusal, a stage that could not
     * take it), the tick stays simulated on the wrong input for the rest of
     * the match. So every rewind decision is recorded here until a replay
     * covering the tick completes, and reconcile re-opens one for the oldest
     * while it is still in snapshot reach. Keyed tick % RNET_INPUT_HIST_DEPTH,
     * tagged. */
    uint32_t owed_tick[RNET_INPUT_HIST_DEPTH];
    int8_t   owed_slot[RNET_INPUT_HIST_DEPTH];   /* -1 = none owed */
    uint32_t owed_retry_after;   /* sim tick before which no retry is tried */

    /* Live-tip restore. Loading the baseline rewinds the engine; an abort
     * after that (baseline fork, missing row) used to leave it on the load
     * tick's state while sim stayed at the old tip. The tip is saved first
     * and put back on any such abort. */
    int      rewound;
    uint32_t tip_tick;

    /* Set by rb_load_sealed_rows when a row is missing, so the abort can name
     * the seat and tick instead of saying only that "a" row was absent. */
    int      missing_slot;
    uint32_t missing_tick;

    /* Session hold (rnet_rb_driver_set_hold): Live admits no tick at or past
     * hold_at, and nothing is predicted while the hold is set, so every row
     * before hold_at is a real one by the time the sim reaches it. */
    int      hold_active;
    uint32_t hold_at;
    int      hold_logged;

    /* Validation knobs, read once per start. */
    uint32_t lockstep_ticks;
    int      lockstep_pinned;
    char     lockstep_env[96];
    int      force_fork_every;
    unsigned long force_fork_n;
    int      force_mispredict_every;
    unsigned long force_mispredict_n;
    int      force_boot_fork;
    int      force_mod_mismatch;
    char     force_modset[1024];
    int      allow_boot_fork;
    char     allow_boot_fork_env[96];
    int      allow_mod_mismatch;
    char     allow_mod_mismatch_env[96];

    /* The injector counts remote ROWS, once per seat per tick, as its
     * interval is documented. It used to count polls, and a poll that stalls
     * on a missing remote row is retried at the same tick -- so under latency
     * the interval shrank with the stall rate (measured at 300 ms RTT: forced
     * rows 2 ticks apart at an interval of 45), and cells at different
     * latencies were injected at different rates. tick+1 of the last row
     * counted per seat; 0 = none. */
    uint32_t force_mispredict_row[RB_MAX_SLOTS];

    /* Episodes opened, by role. The harness counts log lines; these are the
     * same numbers from the other side, printed when a drain completes. */
    uint32_t ep_initiated;
    uint32_t ep_followed;

    /* Tip-hold duration. Entries alone said tip-hold was reachable; how long
     * it actually held is what says whether the runway did anything. */
    uint32_t    tiphold_enter_sim;
    const char *tiphold_exit_why;   /* set just before leaving; NULL = "cleared" */

    /* Answers that arrived before the BEGIN that opens their episode.
     *
     * An initiator whose corrected seat is already confirmed in its history
     * seals without the peer, so BEGIN, BASELINE and POST leave in ONE poll.
     * They travel as separate datagrams, and under jitter the POST can land a
     * frame before the BEGIN; it used to be dropped because no episode was
     * open. The follower then sat in Verifying for the whole episode budget
     * (2 s of frozen sim) and aborted "timed out waiting for peer POST" while
     * the initiator had already committed the same epoch -- every POST
     * timeout in the SNES sweeps at 200 and 300 ms RTT was one of these.
     * Held per sending seat, matched by exact epoch when the BEGIN arrives.
     * begin_seen_seq is the newest BEGIN sequence per initiator seat, so an
     * answer for an episode that already ended is told apart from an early
     * one (epochs only grow per seat). */
    uint32_t early_post_mask;
    uint32_t early_post_epoch[RB_MAX_SLOTS];
    uint32_t early_post_target[RB_MAX_SLOTS];
    uint32_t early_post_digest[RB_MAX_SLOTS];
    uint32_t early_base_mask;
    uint32_t early_base_epoch[RB_MAX_SLOTS];
    uint32_t early_base_load[RB_MAX_SLOTS];
    RNetRbDigestParts early_base[RB_MAX_SLOTS];
    uint32_t begin_seen_seq[RB_MAX_SLOTS];

    /* A BEGIN whose load tick we have not simulated yet (deferred follow).
     *
     * A follower that has not yet reached the load tick used to refuse the
     * episode ("no snapshot at load tick") although it had nothing to
     * correct: it would simulate that tick on the true rows anyway. The NACK
     * aborted the initiator's episode and cost a 30-tick cooldown and a retry.
     * Measured on n64lle at 0 ms: 2 of 57 episodes in one run, 24 of 56 in a
     * rerun of the same cell -- whichever peer's tick clock happened to lead.
     * The ring's newest snapshot was always load-1, i.e. the follower was
     * exactly at the load tick, one field behind the initiator.
     *
     * Now it is held until our sim reaches the load tick (the live state then
     * IS the snapshot keyed load -- see rb_begin_episode), bounded by half the
     * episode budget so a NACK still lands before the initiator's own
     * watchdog. Answers for the held epoch (BASELINE, POST, SEAL_ROWS) are
     * held with it (rb_epoch_is_early). One at a time; a second BEGIN is
     * arbitrated like any other (rb_defer_arbitrate). */
    int      defer_valid;
    uint32_t defer_epoch;
    uint32_t defer_mismatch;
    uint32_t defer_load;
    uint32_t defer_target;
    int      defer_slot;
    uint8_t  defer_flags;
    uint32_t defer_since_ms;
    /* Counted for the drain report: follows that waited for the load tick,
     * and follows that took the load snapshot at our own live tip. */
    uint32_t n_follow_deferred;
    uint32_t n_follow_at_tip;
    uint32_t n_observer_skipped;  /* BEGINs an observer ignored */

    /* SEAL_ROWS chunks for an epoch whose BEGIN has not opened here yet: a
     * deferred follow, or a chunk that overtook its BEGIN under jitter. Same
     * reasoning as early_post: rows that arrive early are still the rows. */
#define RB_EARLY_SEAL_MAX 16
    struct {
        uint32_t epoch, base, target, row_begin;
        int      slot;
        uint16_t count;
        RNetRbFrame rows[RNET_RB_SEAL_ROWS_CHUNK_MAX];
    } early_seal[RB_EARLY_SEAL_MAX];
    uint32_t early_seal_n;

    /* Each replayed tick's digest, kept until the replay completes and then
     * written into the hash chain and sent as FRAME_COMMITs (rb_replay_finish).
     * The chain used to keep the LIVE (mispredicted) digest of every replayed
     * tick; a commit re-primes the chain, but an episode that aborted after
     * its replay (a peer NACK) left the stale digest in place, and "RB chain
     * stall" then fired on the very tick the replay had already corrected --
     * every stall in n64lle's 0 ms sweep sat on a NACKed episode. Held until
     * the replay completes so that a replay undone by a tip restore leaves no
     * trace. Indexed by tick - load; a span never exceeds the 64-row seal mask. */
    uint32_t replay_dig[RNET_RB_PEER_SEAL_MASK_BITS];
    uint64_t replay_dig_mask;

    /* Coordinated stop (rnet_rb_driver_request_quiesce). */
    RNetRbQuiesce quiesce;
    uint32_t quiesce_req_ms;
    uint32_t quiesce_req_sim;
    const char *quiesce_origin;
    uint32_t quiesce_sent_ms;       /* last marker sent; 0 = never */
    uint32_t quiesce_all_ms;        /* when every peer's marker was first held */
    uint32_t peer_quiesce_mask;     /* peers that promised no more episodes */
    uint32_t peer_quiesce_ack_mask; /* ...and said they hold ours */
    uint32_t drain_unopened;        /* mispredicts found while draining, not opened */

    int      rollback_flag;      /* the scheduler bridge's live "rollback" */
};

/* ── log ─────────────────────────────────────────────────────────────── */

static void rb_vlog(RNetRbDriver *d, int tagged, const char *fmt, va_list ap)
{
    char line[4096];
    size_t n = 0;

    if (tagged) {
        int k = snprintf(line, sizeof(line), "%s: ",
                         d->cfg.log_prefix ? d->cfg.log_prefix : "rnet_rb");
        n = (k > 0 && (size_t)k < sizeof(line)) ? (size_t)k : 0;
    }
    vsnprintf(line + n, sizeof(line) - n, fmt, ap);
    if (d->host.log)
        d->host.log(d->host.ctx, line);
    else
        fputs(line, stderr);
}

/* "<prefix>: ..." -- the engine's own tag (snesrecomp: "snes_netplay"). */
static void rb_log(RNetRbDriver *d, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    rb_vlog(d, 1, fmt, ap);
    va_end(ap);
}

/* Lines that carried the rollback engine's own "rbe: " tag when they lived in
 * retcomm-rbengine's orbit. Kept verbatim: 'RESIM episode' and 'forced late
 * row' are counted by the harness. */
static void rb_log_raw(RNetRbDriver *d, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    rb_vlog(d, 0, fmt, ap);
    va_end(ap);
}

/* ── env ─────────────────────────────────────────────────────────────── */

/* "<alias>_<name>" first, then "RNET_RB_<name>". The name that answered is
 * copied out so a log line can quote the variable the operator actually set. */
static const char *rb_env_str(const RNetRbDriver *d, const char *name,
                              char *used, size_t used_cap)
{
    char key[96];
    const char *v;

    if (d->cfg.env_alias && d->cfg.env_alias[0]) {
        snprintf(key, sizeof(key), "%s_%s", d->cfg.env_alias, name);
        v = getenv(key);
        if (v) {
            if (used && used_cap)
                snprintf(used, used_cap, "%s", key);
            return v;
        }
    }
    snprintf(key, sizeof(key), "RNET_RB_%s", name);
    v = getenv(key);
    if (used && used_cap)
        snprintf(used, used_cap, "%s", key);
    return v;
}

/* Out of range falls back to the DEFAULT rather than clamping to the bound, so
 * an unsupported value is never silently turned into a different supported
 * one. */
static int rb_env_int(const RNetRbDriver *d, const char *name, int def, int lo, int hi)
{
    const char *v = rb_env_str(d, name, NULL, 0);
    long n;
    char *end;
    if (!v || !v[0])
        return def;
    n = strtol(v, &end, 10);
    if (end == v || *end != '\0' || n < lo || n > hi)
        return def;
    return (int)n;
}

/* ── seat helpers ────────────────────────────────────────────────────── */

static int rb_slot_count(const RNetRbDriver *d)
{
    int n = d->cfg.slot_count ? *d->cfg.slot_count : 2;
    if (n < 1) n = 1;
    if (n > RB_MAX_SLOTS) n = RB_MAX_SLOTS;
    return n;
}

static int rb_local_slot(const RNetRbDriver *d)
{
    int s = d->cfg.local_slot ? *d->cfg.local_slot : 0;
    /* RB_MAX_SLOTS itself is legal: the observer sentinel of a full room
     * (local_slot == slot_count, rollback.h). Clamping it to 0 would make a
     * spectator of an 8-seat room impersonate seat 0. */
    return (s >= 0 && s <= RB_MAX_SLOTS) ? s : 0;
}

static RNetSession *rb_session(const RNetRbDriver *d)
{
    return d->cfg.session ? *d->cfg.session : NULL;
}

static int rb_input_delay(const RNetRbDriver *d)
{
    int dl = d->cfg.input_delay ? *d->cfg.input_delay : 2;
    return dl < 0 ? 0 : dl;
}

/* Remote seats whose answer an episode waits for. */
static uint32_t rb_expect_mask(const RNetRbDriver *d)
{
    int n = rb_slot_count(d);
    int local = rb_local_slot(d);
    uint32_t seats = (n >= 32) ? 0xffffffffu : ((1u << n) - 1u);
    uint32_t mask = d->cfg.occupied_mask ? (d->cfg.occupied_mask & seats) : seats;
    if (local < n)
        mask &= ~(1u << local);
    return mask;
}

/* Which peer a just-taken message came from, as a bit in rb_expect_mask. With
 * one peer the answer is that peer whatever the header says -- the session
 * filters rb traffic to it -- so two-seat behaviour cannot depend on how a
 * relay numbers its senders. 0 = a seat that takes no part. */
static uint32_t rb_from_bit(const RNetRbDriver *d, int from)
{
    uint32_t expect = rb_expect_mask(d);
    if (expect != 0u && (expect & (expect - 1u)) == 0u)
        return expect;
    if (from >= 0 && from < RB_MAX_SLOTS && (expect & (1u << from)))
        return 1u << from;
    return 0u;
}

static int rb_bit_slot(uint32_t bit)
{
    int i;
    for (i = 0; i < RB_MAX_SLOTS; ++i)
        if (bit & (1u << i))
            return i;
    return -1;
}

static int rb_popcount(uint32_t m)
{
    int n = 0;
    while (m) {
        m &= m - 1u;
        n++;
    }
    return n;
}

/* ── hash chain, one per peer ────────────────────────────────────────── */

/* Our digest goes into every peer's chain; each peer's FRAME_COMMITs only
 * into its own. The watermark is the lowest of them: a tick is confirmed only
 * when every peer agrees with it. With one peer these are exactly the single
 * chain the driver always had. */
static void rb_hc_note_local(RNetRbDriver *d, uint32_t tick, uint32_t dig)
{
    uint32_t m = rb_expect_mask(d);
    int i;
    for (i = 0; i < RB_MAX_SLOTS; ++i)
        if (m & (1u << i))
            rnet_hc_note_local(&d->hc[i], tick, dig);
}

static uint32_t rb_hc_resolved(const RNetRbDriver *d)
{
    uint32_t m = rb_expect_mask(d);
    uint32_t best = 0u;
    int any = 0, i;
    for (i = 0; i < RB_MAX_SLOTS; ++i) {
        uint32_t r;
        if (!(m & (1u << i)))
            continue;
        r = rnet_hc_resolved_through(&d->hc[i]);
        if (!any || r < best)
            best = r;
        any = 1;
    }
    return best;
}

static uint8_t rb_hc_confirm_through(const RNetRbDriver *d, uint32_t tick)
{
    uint32_t m = rb_expect_mask(d);
    int i;
    if (!m)
        return 0u;
    for (i = 0; i < RB_MAX_SLOTS; ++i)
        if ((m & (1u << i)) && !rnet_hc_confirm_through(&d->hc[i], tick))
            return 0u;
    return 1u;
}

static void rb_hc_prime_after(RNetRbDriver *d, uint32_t last_ok)
{
    int i;
    for (i = 0; i < RB_MAX_SLOTS; ++i)
        rnet_hc_prime_after(&d->hc[i], last_ok);
}

static void rb_hc_heal(RNetRbDriver *d)
{
    uint32_t m = rb_expect_mask(d);
    int i;
    for (i = 0; i < RB_MAX_SLOTS; ++i)
        if (m & (1u << i))
            (void)rnet_hc_heal_stale_gap(&d->hc[i]);
}

static uint32_t rb_now(RNetRbDriver *d);

static void rb_request_missing_frame_commits(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);
    uint32_t now, mask;
    int i, sent = 0;
    if (!s || (d->stage != kRbIdle && d->stage != kRbTipHold))
        return;
    mask = rb_expect_mask(d);
    if (rb_popcount(mask) <= 1)
        return; /* retain two-seat wire traffic */
    now = rb_now(d);
    if ((uint32_t)(now - d->frame_commit_request_ms) < 50u)
        return;
    for (i = 0; i < RB_MAX_SLOTS; ++i) {
        uint32_t next;
        if (!(mask & (1u << i)))
            continue;
        next = d->hc[i].resolved_valid ? d->hc[i].resolved_through + 1u : 0u;
        if (next >= d->sim ||
            !rnet_hc_local_digest(&d->hc[i], next, NULL) ||
            rnet_hc_peer_digest(&d->hc[i], next, NULL))
            continue;
        rnet_session_send_rb_sync(s, 0u, next, 0u, 0u, (rnet_u8)i,
                                  RNET_RB_SYNC_OP_FRAME_COMMIT_REQUEST, 0u);
        sent = 1;
    }
    if (sent)
        d->frame_commit_request_ms = now;
}

/* ── row helpers ─────────────────────────────────────────────────────── */

static void rb_row_sanitize(RNetRbDriver *d, int slot, RNetRbFrame *f)
{
    if (f && d->host.sanitize_row)
        d->host.sanitize_row(d->host.ctx, slot, f);
}

static void rb_row_from_sample(RNetRbDriver *d, int slot, uint32_t tick,
                               const RNetInputSample *in, RNetRbFrame *f)
{
    memset(f, 0, sizeof(*f));
    f->tick = tick;
    f->is_valid = 1u;
    f->is_predicted = 0u;
    d->host.decode_sample(d->host.ctx, slot, in, f);
    f->tick = tick;
    f->is_valid = 1u;
    f->is_predicted = 0u;
}

static void rb_row_neutral(RNetRbDriver *d, int slot, uint32_t tick, RNetRbFrame *f)
{
    memset(f, 0, sizeof(*f));
    d->host.neutral_row(d->host.ctx, slot, f);
    f->tick = tick;
    f->is_valid = 1u;
    f->is_predicted = 0u;
}

static int rb_rows_equal(const RNetRbFrame *a, const RNetRbFrame *b)
{
    return a->buttons == b->buttons && a->stick_x == b->stick_x &&
           a->stick_y == b->stick_y;
}

/* ── corrections owed ────────────────────────────────────────────────── */

static void rb_owed_reset(RNetRbDriver *d)
{
    memset(d->owed_tick, 0, sizeof(d->owed_tick));
    memset(d->owed_slot, -1, sizeof(d->owed_slot));
    d->owed_retry_after = 0u;
}

static void rb_owed_mark(RNetRbDriver *d, uint32_t tick, int slot)
{
    uint32_t i = tick % RNET_INPUT_HIST_DEPTH;
    d->owed_tick[i] = tick;
    d->owed_slot[i] = (int8_t)slot;
}

static void rb_owed_clear_span(RNetRbDriver *d, uint32_t load, uint32_t target)
{
    uint32_t t;
    if (target < load || target - load >= RNET_INPUT_HIST_DEPTH)
        return;
    for (t = load; t <= target; ++t) {
        uint32_t i = t % RNET_INPUT_HIST_DEPTH;
        if (d->owed_slot[i] >= 0 && d->owed_tick[i] == t)
            d->owed_slot[i] = -1;
    }
}

/* Oldest owed tick still inside the history window, if any. */
static int rb_owed_first(const RNetRbDriver *d, uint32_t *tick, int *slot)
{
    uint32_t t = d->sim > RNET_INPUT_HIST_DEPTH ? d->sim - RNET_INPUT_HIST_DEPTH : 0u;
    for (; t < d->sim; ++t) {
        uint32_t i = t % RNET_INPUT_HIST_DEPTH;
        if (d->owed_slot[i] >= 0 && d->owed_tick[i] == t) {
            *tick = t;
            *slot = d->owed_slot[i];
            return 1;
        }
    }
    return 0;
}

/* ── snapshots ───────────────────────────────────────────────────────── */

/*
 * A snapshot keyed T is the state BEFORE tick T runs, which is what the
 * replay contract wants: load(load) then run t = load..target re-runs the
 * load tick itself. Keying it "after T" instead would replay every episode
 * one tick short of its own mismatch.
 */
static void rb_snap_take(RNetRbDriver *d, uint32_t tick)
{
    if (d->snap_interval > 1u && (tick % d->snap_interval) != 0u)
        return;
    (void)d->host.snap_save(d->host.ctx, tick);
}

static int rb_snap_has(RNetRbDriver *d, uint32_t tick)
{
    return d->host.snap_has(d->host.ctx, tick) ? 1 : 0;
}

static uint32_t rb_snap_oldest_or0(RNetRbDriver *d)
{
    uint32_t oldest = 0u;
    if (!d->host.snap_oldest(d->host.ctx, &oldest))
        return 0u;
    return oldest;
}

/* Deepest tick <= want that we still hold a snapshot for. Returns 0 and
 * leaves *out untouched when the store cannot reach back that far — the
 * caller must then refuse the episode rather than load the wrong tick. */
static int rb_snap_floor(RNetRbDriver *d, uint32_t want, uint32_t *out)
{
    uint32_t oldest;
    uint32_t t;

    if (!d->host.snap_oldest(d->host.ctx, &oldest))
        return 0;
    /* A snapshot that already produced a baseline fork will produce the same
     * fork every time it is loaded. psxrecomp bounds every later episode
     * below it (g_bl_fork_cap, §83); without that bound one bad episode
     * repeats forever — measured on SNES as a single fork at tick 43 becoming
     * 140 aborted episodes in one match, each reloading the same poison. */
    if (d->fork_cap > 0u) {
        if (d->fork_cap <= oldest) {
            /* The poisoned snapshot has aged out of the store, so there is
             * nothing left to avoid. Keeping the cap here refuses every
             * future episode FOREVER, because the only thing that lifts it
             * is a commit and no episode can open to produce one — measured
             * with a forced fork: 73 mispredicts, 0 episodes, rollback
             * silently off for the rest of the match. A cap that outlives
             * its subject is worse than no cap. */
            rb_log(d, "RB fork cap %u aged out (oldest snap %u) — lifted\n",
                   (unsigned)d->fork_cap, (unsigned)oldest);
            d->fork_cap = 0u;
        } else if (want >= d->fork_cap) {
            want = d->fork_cap - 1u;
        }
    }
    if (want < oldest)
        return 0;
    for (t = want; ; --t) {
        if (rb_snap_has(d, t)) {
            *out = t;
            return 1;
        }
        if (t == oldest)
            break;
    }
    return 0;
}

/* ── sim advance ─────────────────────────────────────────────────────── */

static void rb_publish(RNetRbDriver *d, uint32_t tick, int replay)
{
    d->host.publish(d->host.ctx, tick, d->rows, rb_slot_count(d), replay);
}

/* Pull the row every seat should simulate at `tick` out of the sealed table.
 * Only used during resim: Live resolves from history/wire instead. */
static int rb_load_sealed_rows(RNetRbDriver *d, uint32_t tick)
{
    int slots = rb_slot_count(d);
    int i;

    for (i = 0; i < slots; ++i) {
        RNetRbFrame f;
        if (!rnet_rb_get_sealed_frame(d->rb, i, tick, &f)) {
            d->missing_slot = i;
            d->missing_tick = tick;
            return 0;
        }
        rb_row_sanitize(d, i, &f);
        d->rows[i] = f;
    }
    return 1;
}

/* A replayed tick just ran: keep its digest for the chain (see replay_dig).
 * The same digest the live path notes after the same tick, so the chain
 * compares like with like. */
static void rb_replay_note(RNetRbDriver *d, uint32_t tick)
{
    uint32_t off;
    if (tick < d->corr.load_tick)
        return;
    off = tick - d->corr.load_tick;
    if (off >= RNET_RB_PEER_SEAL_MASK_BITS)
        return;
    d->replay_dig[off] = d->host.digest_master(d->host.ctx);
    d->replay_dig_mask |= 1ull << off;
}

/* One replayed tick, INLINE shape. */
static int rb_advance_sim(void *ctx, uint32_t tick)
{
    RNetRbDriver *d = (RNetRbDriver *)ctx;
    if (!rb_load_sealed_rows(d, tick))
        return 0;
    rb_publish(d, tick, 1);
    if (!d->host.run_tick(d->host.ctx, tick)) {
        d->missing_slot = -2;   /* not a row: the host could not run the tick */
        d->missing_tick = tick;
        return 0;
    }
    rb_replay_note(d, tick);
    d->resim_ticks++;
    return 1;
}

static int rb_vt_save_state(void *ctx, uint32_t tick)
{
    RNetRbDriver *d = (RNetRbDriver *)ctx;
    return d->host.snap_save(d->host.ctx, tick);
}

static int rb_vt_load_state(void *ctx, uint32_t tick)
{
    RNetRbDriver *d = (RNetRbDriver *)ctx;
    return d->host.snap_load(d->host.ctx, tick);
}

static uint32_t rb_vt_state_digest(void *ctx, uint32_t tick, uint32_t partition)
{
    RNetRbDriver *d = (RNetRbDriver *)ctx;
    RNetRbDigestParts p;
    (void)tick;
    if (partition == 0u)
        return d->host.digest_master(d->host.ctx);
    if (partition > 3u)
        return 0u;
    d->host.digest_parts(d->host.ctx, &p);
    return p.part[partition - 1u];
}

static uint8_t rb_vt_hash_confirm_through(void *ctx, uint32_t tick)
{
    RNetRbDriver *d = (RNetRbDriver *)ctx;
    return rb_hc_confirm_through(d, tick);
}

/*
 * Hand the engine the row a seat simulates at `tick`.
 *
 * The admitted input history stops at our live tip, and for a REMOTE seat
 * that is the end of the story: past the tip we hold predictions, and sealing
 * a prediction as authoritative is how a resim converges on a timeline
 * neither peer ran.
 *
 * Our OWN seat is different, and missing that cost every episode where the
 * peer ran ahead. Under real delay a guest tick T plays wire T while the local
 * pad is sampled and PUBLISHED at T+D, so our authoritative input already
 * exists for D ticks past the tip — the peer has it, and will seal the same
 * values — it simply has not been copied into the history ring yet, because
 * Live only writes a row as it steps onto it.
 *
 * Refusing there made the follower abort mid-replay whenever the initiator was
 * a few ticks ahead. The initiator clamps its target to its own live tip, but
 * it cannot clamp to ours, and the two tips only coincide on a zero-latency
 * link. Measured at 200 ms RTT: follower at sim=225 handed the span 220..227,
 * dying at "sealed row missing mid-replay: slot=1 tick=225" — seven times in
 * sixty seconds, and never once on loopback.
 *
 * So read the published row instead of failing. This is not invention: it is
 * the same value from the same ring the Live path would have read, and D
 * exists precisely to make it available early.
 */
static uint8_t rb_vt_get_input_row(void *ctx, int32_t slot, uint32_t tick,
                                   RNetRbFrame *out)
{
    RNetRbDriver *d = (RNetRbDriver *)ctx;
    RNetSession *s;
    RNetInputSample sample;

    if (!out || slot < 0 || slot >= rb_slot_count(d))
        return 0;
    /* An empty seat of a sparse room has no owner: every peer simulates the
     * zero sample RNetSession synthesizes for it (peek_remote_input). Seal
     * exactly that, never the history -- the history may hold an invented
     * (predicted) row for it, and nobody will ever send SEAL_ROWS to replace
     * it (the episode would wait out its watchdog and lose the correction). */
    if ((int)slot != rb_local_slot(d) && d->cfg.occupied_mask &&
        !(d->cfg.occupied_mask & (1u << (uint32_t)slot))) {
        memset(&sample, 0, sizeof(sample));
        sample.tick = rnet_sched_wire_for_sim(tick);
        sample.valid = 1;
        rb_row_from_sample(d, (int)slot, tick, &sample, out);
        rb_row_sanitize(d, (int)slot, out);
        return 1;
    }
    if (rnet_ih_get(&d->ih, (int)slot, tick, out)) {
        rb_row_sanitize(d, (int)slot, out);
        return 1;
    }
    if ((int)slot != rb_local_slot(d))
        return 0;

    s = rb_session(d);
    if (!s)
        return 0;
    memset(&sample, 0, sizeof(sample));
    if (!rnet_session_peek_input(s, (int)slot, rnet_sched_wire_for_sim(tick),
                                 &sample) ||
        !sample.valid)
        return 0; /* past the published horizon — genuinely cannot cover it */

    rb_row_from_sample(d, (int)slot, tick, &sample, out);
    /* Normal operation, not a warning — but say it, because "the seal read
     * our own input from somewhere other than the history ring" is exactly
     * the kind of quiet substitution that should never have to be inferred
     * from a later symptom. Measured ~20 rows/minute at 200 ms RTT on SNES,
     * none at all below ~120 ms, so it doubles as a read on how far ahead
     * the peer is running. */
    rb_log(d, "RB local row sealed from published input tick=%u "
              "(our tip=%u) slot=%d buttons=%04x\n",
           (unsigned)tick, (unsigned)(d->sim ? d->sim - 1u : 0u),
           (int)slot, (unsigned)out->buttons);
    /* Keep the history whole. The replay leaves sim at target+1, so Live will
     * never step onto these ticks and fill them itself, and a later episode
     * sealing back across them would find the same hole. */
    rnet_ih_put(&d->ih, (int)slot, out);
    rb_row_sanitize(d, (int)slot, out);
    return 1;
}

/* Can we seal our own seat across every tick of [load, target]? Asked with the
 * seal's own predicate rather than a tip comparison, so it also catches a tick
 * that has aged out of the history ring at the low end. */
/* An OBSERVER (spectator): local_slot == slot_count, owns no seat. It must
 * never predict (it has no seat whose input it could be right about, and
 * nobody seals rows for its episodes), never open an episode, never follow
 * or NACK one (the relay drops everything it sends, so a NACK only desyncs
 * its own ledger and stalls it), and never tip-extend. Pinning lockstep makes
 * all of that follow: it admits a tick only on every seat's wire row, so its
 * state is always the corrected one and there is nothing to roll back.
 * Measured before this (nesrecomp rb_lobby.sh, 4 seats + 1 spectator): the
 * spectator invented 403 rows, opened 6 episodes nobody answered, refused
 * every players' BEGIN ("no local row" for seat 4), and fell 600 ticks
 * behind. */
static int rb_is_observer(const RNetRbDriver *d)
{
    return rb_local_slot(d) >= rb_slot_count(d);
}

static int rb_local_rows_cover(RNetRbDriver *d, uint32_t load, uint32_t target,
                               uint32_t *gap)
{
    uint32_t t;
    int local = rb_local_slot(d);

    if (local < 0 || rb_is_observer(d) || target < load)
        return 1;
    for (t = load; t <= target; ++t) {
        RNetRbFrame row;
        if (!rb_vt_get_input_row(d, local, t, &row)) {
            if (gap)
                *gap = t;
            return 0;
        }
    }
    return 1;
}

/* ── resim window ────────────────────────────────────────────────────── */

/*
 * Resim replays ticks the player has already seen and heard. Presentation
 * must not repeat with it (recomp-ai-rules/NETPLAY.md §1: the presented image
 * is never simulation); the host suppresses it between these two calls.
 */
static void rb_resim_begin(RNetRbDriver *d)
{
    d->in_resim = 1;
    d->host.resim_begin(d->host.ctx);
}

static void rb_resim_end(RNetRbDriver *d)
{
    if (!d->in_resim)
        return;
    d->host.resim_end(d->host.ctx);
    d->in_resim = 0;
}

/* ── scheduler gates ─────────────────────────────────────────────────── */

static uint32_t rb_now(RNetRbDriver *d)
{
    return d->host.now_ms(d->host.ctx);
}

static uint32_t rb_gate_now_ms(void *ctx)
{
    return rb_now((RNetRbDriver *)ctx);
}

static uint8_t rb_gate_episode_active(void *ctx)
{
    RNetRbDriver *d = (RNetRbDriver *)ctx;
    return (d->stage == kRbSealing || d->stage == kRbReplaying ||
            d->stage == kRbVerifying) ? 1u : 0u;
}

/*
 * Hold Live at the first tick until both peers agree on the state they are
 * starting from.
 *
 * Rollback corrects a mispredicted INPUT. It cannot correct two machines that
 * booted differently: every subsequent digest disagrees, every episode forks
 * at its baseline, and the first evidence arrives minutes later as a fork on a
 * tick that had nothing to do with the cause. The cheapest possible check is
 * at tick 0, before either side has simulated anything worth losing.
 *
 * No new wire message. finish_frame already notes tick 0's digest into the
 * hash chain and sends it as a FRAME_COMMIT before sim reaches 1, so by the
 * time this gate can hold, our own digest is published and the peer's is in
 * flight. Both peers hold symmetrically and both messages are already sent, so
 * the wait is one trip, not a deadlock.
 *
 * The latch is sticky. The hash-chain ring ages tick 0 out, and re-deriving
 * agreement from a ring that no longer holds the tick would re-stall a session
 * that had already synced.
 *
 * Bounded, because an unbounded hold is the failure this codebase keeps
 * outlawing: a wedged match with nothing in the log. On timeout it releases
 * and says so, which is worse than agreeing and better than hanging.
 */
#define RB_BOOT_DIGEST_TIMEOUT_MS 4000u

/*
 * Refuse the match: latch why, stop admitting Live ticks, ask the host to leave.
 *
 * The refusal used to be only the host callback. Everything after it then
 * depended on the host acting on it -- and the gates that had been holding the
 * sim (the boot-digest wait, the mod-set wait) RELEASED the moment they
 * settled, refusal or not. A host that did not consume the request (snesrecomp's
 * desktop host never did) went on to play the match that had just been
 * declared unplayable: every episode forking at its baseline after a boot
 * fork, or a match between two different mod sets. That is the silent
 * degradation NETPLAY.md section 4 rules out ("abort rather than degrade").
 *
 * So the driver now enforces its own verdict: once refused, poll_admit admits
 * no Live tick again this match (an episode already open still finishes). The
 * host still owns LEAVING -- tearing down the session and returning its player
 * to the lobby -- and reads the reason from rnet_rb_driver_refusal(). The
 * first refusal names the match; a second one (both mod checks can fire) is
 * the same verdict and is not re-reported.
 */
static void rb_refuse(RNetRbDriver *d, const char *code)
{
    if (d->refusal)
        return;
    d->refusal = code;
    d->stall_tag = "refused";
    rb_log(d, "RB match refused (%s) at sim=%u — no further tick is "
              "admitted; asking the host to return to the lobby\n",
           code, (unsigned)d->sim);
    d->host.request_return_to_lobby(d->host.ctx);
}

/* One peer's identity, next to ours, for a refusal line. */
static void rb_log_peer_identity(RNetRbDriver *d, int seat)
{
    if (d->peer_ident_mask & (1u << seat))
        rb_log(d, "  peer identity (seat %d): build ours=%08x "
                  "peer=%08x%s, content ours=%08x peer=%08x%s\n",
               seat, (unsigned)d->local_build_fp, (unsigned)d->peer_build_fp[seat],
               d->local_build_fp == d->peer_build_fp[seat] ? "" : "  <-- DIFFERENT",
               (unsigned)d->local_content_fp,
               (unsigned)d->peer_content_fp[seat],
               d->local_content_fp == d->peer_content_fp[seat] ? "" : "  <-- DIFFERENT");
    else
        rb_log(d, "  peer identity (seat %d) not in yet — it travels "
                  "with the same tick and can lose the race by a hair. If "
                  "no \"peer identity\" line follows, the peer predates the "
                  "message, which is itself a likely cause.\n", seat);
}

static int rb_boot_digest_gate(RNetRbDriver *d)
{
    uint32_t local = 0u;
    uint32_t expect, missing, bad = 0u;
    uint32_t now;
    int i;

    if (d->boot_dig_settled || !d->started)
        return 0;
    expect = rb_expect_mask(d);

    if (d->boot_dig_local_valid) {
        local = d->boot_dig_local;
    } else {
        int first = rb_bit_slot(expect);
        if (first >= 0 && rnet_hc_local_digest(&d->hc[first], 0u, &local)) {
            d->boot_dig_local = local;
            d->boot_dig_local_valid = 1u;
            /* Break tick 0 down, always, on both peers: the chain carries only
             * the master digest, so a mismatch says the sides differ without
             * saying WHERE. Establishing that on SNES took a long forensic pass
             * over two logs and three wrong guesses, every one of which would
             * have been a glance had the partitions been in the log. What to
             * print is the engine's to say. */
            if (d->host.boot_digest_noted)
                d->host.boot_digest_noted(d->host.ctx);
        } else {
            return 1; /* our own tick 0 not published yet — nothing to compare */
        }
    }

    /* Every peer's tick 0, not the first to land: with more than two seats a
     * peer that booted differently is as fatal as it is with two. */
    for (i = 0; i < RB_MAX_SLOTS; ++i) {
        uint32_t peer = 0u;
        if (!(expect & (1u << i)) || (d->boot_dig_peer_mask & (1u << i)))
            continue;
        if (rnet_hc_peer_digest(&d->hc[i], 0u, &peer)) {
            d->boot_dig_peer[i] = peer;
            d->boot_dig_peer_mask |= 1u << i;
        }
    }
    missing = expect & ~d->boot_dig_peer_mask;
    if (missing) {
        now = rb_now(d);
        if (d->boot_dig_hold_since_ms == 0u)
            d->boot_dig_hold_since_ms = now;
        if (!d->boot_dig_waiting_logged) {
            d->boot_dig_waiting_logged = 1u;
            rb_log(d, "RB boot digest wait — ours=%08x, "
                      "peer tick 0 not in yet\n", (unsigned)local);
        }
        if ((uint32_t)(now - d->boot_dig_hold_since_ms) >
            RB_BOOT_DIGEST_TIMEOUT_MS) {
            rb_log(d, "RB boot digest TIMED OUT after %u ms — "
                      "starting UNVERIFIED (seats %x never sent tick 0). If this "
                      "match desyncs, suspect the starting state, not the "
                      "netcode.\n",
                   (unsigned)RB_BOOT_DIGEST_TIMEOUT_MS, (unsigned)missing);
            d->boot_dig_settled = 1u;
            return 0;
        }
        return 1;
    }

    d->boot_dig_settled = 1u;
    for (i = 0; i < RB_MAX_SLOTS; ++i)
        if ((expect & (1u << i)) && d->boot_dig_peer[i] != local)
            bad |= 1u << i;
    if (bad) {
        /* END THE MATCH. This used to warn and play on, and a real session did
         * exactly that: the mismatch was named at tick 0 and the first fork
         * landed at tick 4681, about 78 seconds of a fight that never had a
         * chance. Rollback corrects inputs, not two machines that started
         * differently, so there is nothing downstream that can recover it and
         * nothing to be gained by continuing.
         *
         * Every peer reaches this independently and agrees, so all return to
         * the lobby rather than one waiting on another. */
        for (i = 0; i < RB_MAX_SLOTS; ++i) {
            if (!(bad & (1u << i)))
                continue;
            rb_log(d, "RB BOOT DIGEST MISMATCH ours=%08x peer=%08x (seat %d) — "
                      "the two sides did not start from the same state, so this "
                      "match cannot be played. Rollback corrects inputs, not a "
                      "divergent start.\n",
                   (unsigned)local, (unsigned)d->boot_dig_peer[i], i);
            rb_log_peer_identity(d, i);
        }
        if (d->allow_boot_fork) {
            rb_log(d, "  %s=1 — "
                      "continuing anyway; every episode will fork at its "
                      "baseline\n", d->allow_boot_fork_env);
            return 0;
        }
        rb_refuse(d, "boot_digest_mismatch");
        return 0;
    }
    rb_log(d, "RB boot digest agreed (%08x)%s\n", (unsigned)local,
           rb_popcount(expect) > 1 ? " with every peer" : "");
    return 0;
}

static uint8_t rb_gate_pre_admit_hold(void *ctx, uint32_t sim, uint32_t wire,
                                      const char **tag_out)
{
    RNetRbDriver *d = (RNetRbDriver *)ctx;
    (void)wire;
    /* Hold the whole match, not just tick 1, while the mod-set handshake is
     * outstanding: it is the precondition for playing at all, and letting
     * frames run while it is open would be simulating a match nobody has
     * agreed to. Never under an open episode — a replay owns the sim. */
    if (d->local_modset && !d->modset_settled && d->stage == kRbIdle) {
        if (tag_out)
            *tag_out = "modset_wait";
        return 1u;
    }
    if (sim == 1u && d->stage == kRbIdle && rb_boot_digest_gate(d)) {
        if (tag_out)
            *tag_out = "boot_digest_wait";
        return 1u;
    }
    /* The boot-digest gate refuses from inside this call and then reports
     * itself settled; without this the same admission would let tick 1 run on
     * the match it had just refused. */
    if (d->refusal) {
        if (tag_out)
            *tag_out = "refused";
        return 1u;
    }
    return 0u;
}

/*
 * The mod-set handshake, pumped once a tick until it settles.
 *
 * The host is authoritative and says what everyone runs; each peer answers
 * whether it can. Nothing simulates until that is settled, because a mod
 * patches guest memory -- a peer with a different set is running a different
 * game, and the honest moment to find out is before the first frame rather
 * than at the first fork.
 *
 * Bounded, and refusing on the bound. An older peer will never answer, and
 * "we could not confirm" has to be a refusal rather than a shrug: an
 * unverified match is exactly the thing this exists to prevent.
 *
 * Seat 0 is the host, and it settles only once EVERY peer has confirmed (the
 * session keeps each seat's latest answer). It used to settle on the first
 * answer, and the session kept one answer for all seats, so with more than two
 * a second guest's answer overwrote the first and a guest that could not run
 * the set could be outvoted by one that could. A guest answers every copy the
 * host sends, not only the first: the host re-sends until it holds them all,
 * and an answer lost on the link would otherwise end the match on the bound.
 */
#define RB_MODSET_TIMEOUT_MS 4000u

static void rb_modset_fail(RNetRbDriver *d, const char *why)
{
    if (d->modset_settled)
        return;
    d->modset_settled = 1u;
    d->modset_ok = 0u;
    rb_log(d, "MOD SET NOT AGREED — %s. The match cannot start: "
              "the host decides which mods run, and a peer that cannot match "
              "them is running a different game.\n", why);
    if (d->allow_mod_mismatch) {
        rb_log(d, "  %s=1 — "
                  "starting anyway; expect an immediate desync\n",
               d->allow_mod_mismatch_env);
        d->modset_ok = 1u;
        return;
    }
    rb_refuse(d, "mod_set_not_agreed");
}

static void rb_modset_pump(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);
    const int host = (rb_local_slot(d) == 0);
    char text[1024];
    rnet_u8 status = 0;
    char reason[96];

    if (!s || !d->local_modset)
        return;
    if (d->modset_settled) {
        if (!host && rnet_session_take_modset(s, text, sizeof(text)))
            rnet_session_send_modset_ack(s, d->modset_ok ? 0u : 1u,
                                         d->modset_ok ? "" : "cannot honour the host's set");
        return;
    }
    if (d->modset_since_ms == 0u)
        d->modset_since_ms = rb_now(d);

    if (host) {
        /* Publish, then wait to be told it can be honoured. Re-sent while
         * unanswered: this is one packet and the link may drop it. */
        if (!d->modset_sent || (d->sim % 30u) == 0u) {
            /* Validation (FORCE_MODSET="<text>"): publish a set the peer
             * genuinely cannot match, so the refusal can be proven to fire. */
            const char *t = d->force_modset[0] ? d->force_modset : d->local_modset;
            if (rnet_session_send_modset(s, t) == 0)
                d->modset_sent = 1u;
        }
        while (rnet_session_take_modset_ack(s, &status, reason, sizeof(reason))) {
            uint32_t bit = rb_from_bit(d, rnet_session_rb_last_take_from(s));
            if (!bit)
                continue;   /* a seat that takes no part */
            if (status == 0u) {
                if (!(d->modset_ack_mask & bit))
                    rb_log(d, "peer confirmed the mod set (seat %d)\n", rb_bit_slot(bit));
                d->modset_ack_mask |= bit;
            } else {
                char why[160];
                snprintf(why, sizeof(why), "peer (seat %d) reports: %s", rb_bit_slot(bit),
                         reason[0] ? reason : "(no reason given)");
                rb_modset_fail(d, why);
                return;
            }
        }
        if ((d->modset_ack_mask & rb_expect_mask(d)) == rb_expect_mask(d)) {
            d->modset_settled = 1u;
            d->modset_ok = 1u;
            return;
        }
    } else if (rnet_session_take_modset(s, text, sizeof(text))) {
        int rc = d->modset_check ? d->modset_check(text, reason, sizeof(reason)) : 0;
        rnet_session_send_modset_ack(s, (rnet_u8)rc, reason);
        if (rc == 0) {
            d->modset_settled = 1u;
            d->modset_ok = 1u;
            rb_log(d, "host mod set accepted:\n%s", text);
        } else {
            char why[1400];
            char adopt_reason[96];
            int adopted = 0;
            /* Write the host's selection into ours before refusing. The player
             * asked to play with this person; making them reproduce someone
             * else's mod configuration by hand, from a log line, is a worse
             * answer than "your settings now match, start again". It cannot
             * take effect this launch -- mods activate before the session
             * exists -- so the refusal still stands. */
            if (d->modset_adopt)
                adopted = (d->modset_adopt(text, adopt_reason, sizeof(adopt_reason)) == 0);
            snprintf(why, sizeof(why), "%s.%s Host wants:\n%s",
                     reason[0] ? reason : "cannot honour the host's set",
                     adopted ? " Your settings have been changed to match the "
                               "host — start the game again to join."
                             : " Your settings were NOT changed; this build "
                               "cannot run the host's set at all.",
                     text);
            rb_modset_fail(d, why);
        }
        return;
    }

    if ((uint32_t)(rb_now(d) - d->modset_since_ms) > RB_MODSET_TIMEOUT_MS)
    {
        char why[160];
        if (host)
            snprintf(why, sizeof(why), "the peer never confirmed it (seats %x of %x "
                     "answered; an older build cannot answer)",
                     (unsigned)(d->modset_ack_mask & rb_expect_mask(d)),
                     (unsigned)rb_expect_mask(d));
        else
            snprintf(why, sizeof(why), "the host never published one (an older build "
                     "does not send it)");
        rb_modset_fail(d, why);
    }
}

/*
 * Measured link latency for the scheduler's invent-grace budget.
 *
 * The scheduler has always asked for this; psxrecomp has always answered; the
 * SNES host never bound it, so sched_rtt_ms() returned 0 and the invent grace
 * fell back to its synthetic D-scaled floor on every link (18,325 decisions
 * logged from 0 to 300 ms RTT read rtt=24 rtt_raw=0 every time).
 *
 * The sample is POST-out to POST-back, which is not a pure transit time -- the
 * peer sends its POST when its own replay finishes -- and the scheduler is
 * built for that: a raw estimate may only RAISE the synthetic floor.
 */
static uint32_t rb_gate_rtt_ms(void *ctx)
{
    return ((RNetRbDriver *)ctx)->rtt_ema_ms;
}

static uint8_t rb_gate_tip_holding(void *ctx)
{
    return ((RNetRbDriver *)ctx)->stage == kRbTipHold ? 1u : 0u;
}

static uint32_t rb_gate_episode_count(void *ctx)
{
    return ((RNetRbDriver *)ctx)->episode_count;
}

static uint64_t rb_gate_replay_ticks(void *ctx)
{
    return ((RNetRbDriver *)ctx)->resim_ticks;
}

/*
 * Degrade rather than desync (psxrecomp lockstep_no_invent).
 *
 * Predicting remote input is only worth doing while predictions are usually
 * right. Once the peers have demonstrably forked, continuing to invent means
 * simulating forward from a state we already know is wrong — so stop, wait
 * for the real rows, and pay the latency instead.
 *
 * LOCKSTEP=1 pins it on, which is the operator fallback for a title or a link
 * that cannot hold prediction at all.
 */
#define RB_LOCKSTEP_TICKS_DEFAULT 60u

/* Called wherever a fork is proven. Idempotent; extends an active window. */
static void rb_enter_lockstep(RNetRbDriver *d, const char *why)
{
    uint32_t until;
    if (d->lockstep_ticks == 0u)
        return;
    until = d->sim + d->lockstep_ticks;
    if (until <= d->lockstep_until)
        return;
    rb_log(d, "RB lockstep for %u ticks (through %u) — %s\n",
           (unsigned)d->lockstep_ticks, (unsigned)until, why);
    d->lockstep_until = until;
}

/* Pure: the scheduler only asks when it is about to invent, so expiry cannot
 * live here — with no remote misses the window would lapse unobserved and the
 * "released" line would never print. rb_lockstep_tick() owns the transition. */
static uint8_t rb_gate_lockstep_no_invent(void *ctx)
{
    RNetRbDriver *d = (RNetRbDriver *)ctx;
    if (d->lockstep_pinned || d->hold_active)
        return 1u;
    return (d->lockstep_until != 0u && d->sim < d->lockstep_until) ? 1u : 0u;
}

/* Once per tick, so entry and release are symmetric and both always logged. */
static void rb_lockstep_tick(RNetRbDriver *d)
{
    if (d->lockstep_until == 0u || d->sim < d->lockstep_until)
        return;
    rb_log(d, "RB lockstep released at %u — predicting again\n", (unsigned)d->sim);
    d->lockstep_until = 0u;
}

static const char *rb_gate_lockstep_tag(void *ctx)
{
    const RNetRbDriver *d = (const RNetRbDriver *)ctx;
    if (d->hold_active)
        return "session_hold";
    return d->lockstep_pinned ? "lockstep_pinned" : "desync_cooldown";
}

static void rb_bind_sched(RNetRbDriver *d)
{
    RNetSchedBridge br;

    memset(&br, 0, sizeof(br));
    d->rollback_flag = 1;
    br.session = d->cfg.session;
    br.input_delay = d->cfg.input_delay;
    br.input_prediction = &d->prediction_cap;
    br.local_slot = d->cfg.local_slot;
    br.force_turn = d->cfg.force_turn;
    br.rollback = &d->rollback_flag;
    br.gates.ctx = d;
    br.gates.now_ms = &rb_gate_now_ms;
    br.gates.episode_active = &rb_gate_episode_active;
    br.gates.rtt_ms = &rb_gate_rtt_ms;
    br.gates.pre_admit_hold = &rb_gate_pre_admit_hold;
    br.gates.tip_holding = &rb_gate_tip_holding;
    br.gates.episode_count = &rb_gate_episode_count;
    br.gates.replay_ticks_total = &rb_gate_replay_ticks;
    br.gates.lockstep_no_invent = &rb_gate_lockstep_no_invent;
    br.gates.lockstep_stall_tag = &rb_gate_lockstep_tag;
    br.gates.desync_hold = &rb_gate_lockstep_no_invent;   /* tag only */
    /* media_active stays NULL: no engine on this driver has an FMV path yet,
     * so invent is never held for media and auto-D always samples. */
    rnet_sched_bind(&br);
}

/* ── lifecycle ───────────────────────────────────────────────────────── */

RNetRbDriver *rnet_rb_driver_create(void)
{
    RNetRbDriver *d = (RNetRbDriver *)calloc(1u, sizeof(*d));
    if (d) {
        d->force_invent_slot = -1;
        rb_owed_reset(d);
    }
    return d;
}

void rnet_rb_driver_destroy(RNetRbDriver *d)
{
    if (!d)
        return;
    rnet_rb_driver_shutdown(d);
    free(d);
}

void rnet_rb_driver_set_identity(RNetRbDriver *d, uint32_t build_fp, uint32_t content_fp)
{
    if (!d)
        return;
    d->local_build_fp = build_fp;
    d->local_content_fp = content_fp;
}

void rnet_rb_driver_set_modset(RNetRbDriver *d, const char *text, RNetRbModSetCheckFn check,
                               RNetRbModSetAdoptFn adopt)
{
    if (!d)
        return;
    d->local_modset = text;
    d->modset_check = check;
    d->modset_adopt = adopt;
}

/* Name the first required callback a host left out, or NULL. */
static const char *rb_host_missing(const RNetRbDriverConfig *cfg, const RNetRbHost *h)
{
    if (!h->snap_save) return "snap_save";
    if (!h->snap_load) return "snap_load";
    if (!h->snap_has) return "snap_has";
    if (!h->snap_oldest) return "snap_oldest";
    if (!h->snap_drop_after) return "snap_drop_after";
    if (!h->publish) return "publish";
    if (cfg->replay_mode == RNET_RB_REPLAY_INLINE && !h->run_tick)
        return "run_tick (required by RNET_RB_REPLAY_INLINE)";
    if (!h->resim_begin) return "resim_begin";
    if (!h->resim_end) return "resim_end";
    if (!h->digest_master) return "digest_master";
    if (!h->digest_parts) return "digest_parts";
    if (!h->decode_sample) return "decode_sample";
    if (!h->neutral_row) return "neutral_row";
    if (!h->request_return_to_lobby) return "request_return_to_lobby";
    if (!h->now_ms) return "now_ms";
    return NULL;
}

int rnet_rb_driver_start(RNetRbDriver *d, const RNetRbDriverConfig *cfg, const RNetRbHost *host)
{
    RNetRbConfig rcfg;
    RNetRollbackVTable vt;
    int slots;
    int i;

    if (!d)
        return 0;
    rnet_rb_driver_shutdown(d);

    /*
     * Rematch cold reset.
     *
     * A second match in the same process is a NEW agreement between the peers,
     * and every scrap of the previous one is at best noise and at worst a lie:
     * a fork cap naming a tick this session will never reach, a lockstep window
     * measured against the old sim clock, a settled boot-digest latch that
     * would skip the one check that catches two sides restarting differently.
     *
     * So wipe the struct and name what SURVIVES instead. The list of things
     * that must persist is short, stable, and obvious at a glance; the list of
     * things that must be cleared is neither. (On SNES the hand-maintained
     * clear list covered about twenty fields of fifty-eight.)
     *
     * Order matters: shutdown() first, because it destroys the session.
     */
    {
        /* Identity and mod set are properties of the PROCESS, not of a match:
         * set once before the first start and the same for every match this
         * binary plays. */
        uint32_t keep_build_fp = d->local_build_fp;
        uint32_t keep_content_fp = d->local_content_fp;
        const char *keep_modset = d->local_modset;
        RNetRbModSetCheckFn keep_check = d->modset_check;
        RNetRbModSetAdoptFn keep_adopt = d->modset_adopt;
        memset(d, 0, sizeof(*d));
        d->local_build_fp = keep_build_fp;
        d->local_content_fp = keep_content_fp;
        d->local_modset = keep_modset;
        d->modset_check = keep_check;
        d->modset_adopt = keep_adopt;
    }
    /* The only fields whose cleared value is not zero. */
    d->force_invent_slot = -1;
    d->missing_slot = -1;
    rb_owed_reset(d);

    if (!cfg || !host) {
        fprintf(stderr, "rnet_rb: driver start with no %s — refused\n",
                !cfg ? "config" : "host");
        return 0;
    }
    d->cfg = *cfg;
    d->host = *host;
    {
        const char *missing = rb_host_missing(cfg, host);
        if (missing) {
            rb_log(d, "RB start refused — host callback %s is missing. A "
                      "driver with a hole in its host would run a rollback "
                      "path that cannot work, with no error.\n", missing);
            memset(&d->host, 0, sizeof(d->host));
            return 0;
        }
    }
    if (!cfg->session) {
        rb_log(d, "RB start with no session binding — cfg.session is NULL, "
                  "so there is no wire to run an episode over.\n");
        return 0;
    }
    slots = rb_slot_count(d);

    /* Session-settled P (recomp-ui: P = 4 + D) when the host bound one;
     * PREDICTION remains the operator override. A P below the delay it must
     * cover is what produced the observed freezes: pred_depth walks to the cap
     * during a relay stall, pcap FREEZE stops the sim, and the hitch shows as
     * debt. A fixed default of 8 was SHORTER than D on a relayed session
     * (measured at D=9). */
    {
        int bound = d->cfg.input_prediction ? *d->cfg.input_prediction : 0;
        int dflt;
        if (bound >= 2) {
            dflt = bound;
        } else {
            dflt = 4 + rb_input_delay(d);
            if (dflt < 6) dflt = 6;
            if (dflt > 16) dflt = 16;
        }
        d->prediction_cap = rb_env_int(d, "PREDICTION", dflt, 1, 32);
    }
    d->snap_interval = (uint32_t)rb_env_int(d, "SNAP_INTERVAL", 1, 1, 16);
    d->seal_timeout_ms = (uint32_t)rb_env_int(d, "EPISODE_TIMEOUT_MS", 2000, 100, 30000);
    d->lockstep_ticks = (uint32_t)rb_env_int(d, "LOCKSTEP_TICKS",
                                             (int)RB_LOCKSTEP_TICKS_DEFAULT, 0, 600);
    {
        const char *v = rb_env_str(d, "LOCKSTEP", d->lockstep_env, sizeof(d->lockstep_env));
        d->lockstep_pinned = (v && v[0] && v[0] != '0') ? 1 : 0;
        if (rb_is_observer(d) && !d->lockstep_pinned) {
            d->lockstep_pinned = 1;
            snprintf(d->lockstep_env, sizeof(d->lockstep_env), "observer");
        }
        if (d->lockstep_pinned)
            rb_log_raw(d, "rbe: LOCKSTEP pinned on — remote input is never "
                          "predicted (%s)\n", d->lockstep_env);
    }
    {
        const char *v = rb_env_str(d, "FORCE_FORK", NULL, 0);
        d->force_fork_every = (v && v[0]) ? atoi(v) : 0;
        if (d->force_fork_every < 0) d->force_fork_every = 0;
        if (d->force_fork_every)
            rb_log_raw(d, "rbe: FORCE FORK every %d baseline exchange "
                          "(validation only)\n", d->force_fork_every);
    }
    {
        const char *v = rb_env_str(d, "FORCE_MISPREDICT", NULL, 0);
        d->force_mispredict_every = (v && v[0]) ? atoi(v) : 0;
        if (d->force_mispredict_every < 0) d->force_mispredict_every = 0;
        if (d->force_mispredict_every)
            rb_log_raw(d, "rbe: FORCE MISPREDICT every %d remote row "
                          "(validation only)\n", d->force_mispredict_every);
    }
    d->force_boot_fork = rb_env_int(d, "FORCE_BOOT_FORK", 0, 0, 1);
    d->force_mod_mismatch = rb_env_int(d, "FORCE_MOD_MISMATCH", 0, 0, 1);
    {
        const char *v = rb_env_str(d, "FORCE_MODSET", NULL, 0);
        if (v && v[0])
            snprintf(d->force_modset, sizeof(d->force_modset), "%s", v);
    }
    (void)rb_env_str(d, "ALLOW_BOOT_FORK", d->allow_boot_fork_env,
                     sizeof(d->allow_boot_fork_env));
    d->allow_boot_fork = rb_env_int(d, "ALLOW_BOOT_FORK", 0, 0, 1);
    (void)rb_env_str(d, "ALLOW_MOD_MISMATCH", d->allow_mod_mismatch_env,
                     sizeof(d->allow_mod_mismatch_env));
    d->allow_mod_mismatch = rb_env_int(d, "ALLOW_MOD_MISMATCH", 0, 0, 1);

    memset(&rcfg, 0, sizeof(rcfg));
    rcfg.local_slot = (uint32_t)rb_local_slot(d);
    rcfg.delay = (uint32_t)rb_input_delay(d);
    rcfg.slot_count = (uint32_t)slots;
    /* Seal completion waits on the same seats the episode FSM does
     * (rb_expect_mask): never on an empty seat of a sparse room. */
    rcfg.occupied_mask = d->cfg.occupied_mask;
    rcfg.tip_runway = (uint32_t)rb_env_int(d, "TIP_RUNWAY",
                                           RNET_RB_TIP_RUNWAY_DEFAULT, 0, 32);
    /* Keep the light-tip ceiling at the runway: a coalesced episode's depth
     * grows toward tip_runway, and a ceiling below it silently costs the fast
     * path a second round trip (docs/rollback.md, "Light tip"). */
    rcfg.light_tip_max_depth = rcfg.tip_runway > RNET_RB_LIGHT_TIP_MAX_DEPTH
                                   ? rcfg.tip_runway
                                   : RNET_RB_LIGHT_TIP_MAX_DEPTH;

    memset(&vt, 0, sizeof(vt));
    vt.ctx = d;
    vt.save_state = &rb_vt_save_state;
    vt.load_state = &rb_vt_load_state;
    vt.advance_sim = &rb_advance_sim;
    vt.state_digest = &rb_vt_state_digest;
    vt.hash_confirm_through = &rb_vt_hash_confirm_through;
    vt.get_input_row = &rb_vt_get_input_row;

    d->rb = rnet_rb_create(&rcfg, &vt);
    if (!d->rb) {
        rb_log(d, "RB start refused — rollback session rejected slot=%u "
                  "slots=%u\n", (unsigned)rcfg.local_slot, (unsigned)rcfg.slot_count);
        return 0;
    }

    rnet_ih_reset(&d->ih, slots);
    for (i = 0; i < RB_MAX_SLOTS; ++i)
        rnet_hc_reset(&d->hc[i]);

    /* Seed a neutral row per seat, and tell the history what neutral IS, so
     * hold-last never falls back to input_hist's PSX-shaped 0xFFFF. */
    for (i = 0; i < slots; ++i) {
        RNetRbFrame f;
        rb_row_neutral(d, i, 0u, &f);
        rnet_ih_set_neutral(&d->ih, i, &f);
        rnet_ih_put(&d->ih, i, &f);
    }

    rb_bind_sched(d);
    /* Filter rb episode traffic to the one peer when there is one. With more
     * peers every one takes part, so accept all and attribute per sender
     * (rnet_session_rb_last_take_from). "One peer" is the expected set, not
     * the seat count: a four-seat room with one other occupant has one peer. */
    if (rb_session(d)) {
        uint32_t expect = rb_expect_mask(d);
        rnet_session_set_rb_peer_slot(rb_session(d),
                                      rb_popcount(expect) == 1 ? rb_bit_slot(expect) : -1);
    }

    d->sim = 0;
    d->stage = kRbIdle;
    d->started = 1;
    d->stall_tag = NULL;
    rb_log(d, "ROLLBACK start slot=%d slots=%d D=%d P=%d "
              "snap_interval=%u snap_depth=%u (reach %u ticks) "
              "tip_runway=%u\n",
           rb_local_slot(d), slots, rb_input_delay(d), d->prediction_cap,
           (unsigned)d->snap_interval, (unsigned)d->cfg.snap_depth,
           /* Slots are not ticks: reach is depth x interval. Printed because
            * an operator setting a depth is reasoning about ticks. */
           (unsigned)(d->cfg.snap_depth * d->snap_interval),
           (unsigned)rcfg.tip_runway);
    if (d->cfg.replay_mode == RNET_RB_REPLAY_INCREMENTAL)
        rb_log(d, "RB replay shape: incremental (one replayed tick per host "
                  "iteration)\n");
    return 1;
}

/*
 * Announce who we are, from the very first tick.
 *
 * Timing is the whole point. The boot-digest verdict is rendered as soon as
 * the peer's tick-0 digest arrives, and identity is what EXPLAINS that verdict
 * — so it has to travel alongside the tick-0 FRAME_COMMIT, not after it.
 *
 * Retried until the peer's own identity comes back rather than fired once and
 * assumed: the session may not be RUNNING on the first tick, and a send that
 * failed must not count as sent. Bounded, because a peer that predates this
 * message will never answer and we must not retransmit for the whole match.
 */
#define RB_IDENT_RETRY_TICKS 90u
#define RB_IDENT_RESEND_MS 100u

/* The content fingerprint this peer claims. FORCE_MOD_MISMATCH makes it a
 * peer that really has a different set -- on the wire AND in its own
 * comparison. Perturbing only what it sends made the knob one-sided: the
 * forcing peer compared the honest reply against its honest fingerprint, saw a
 * match, and never refused, where two peers with genuinely different sets both
 * refuse. A validation knob must model the fault, not half of it. */
static uint32_t rb_claimed_content_fp(const RNetRbDriver *d)
{
    return d->force_mod_mismatch > 0 ? (d->local_content_fp ^ 0x5a5a5a5au)
                                     : d->local_content_fp;
}

static void rb_send_identity(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);
    if (!s || d->sim > RB_IDENT_RETRY_TICKS)
        return;
    if (d->local_build_fp == 0u && d->local_content_fp == 0u)
        return;
    /* Stop once the peer's identity is in AND ours has gone out. Stopping on
     * the peer's alone made the exchange one-sided: the peer that started
     * later usually had the other's IDENT in hand before its own first send
     * (tick 0, or the next multiple of 8), and then never sent at all -- so
     * only one side could ever see a mod-set difference and refuse, and the
     * other left on "peer disconnected" without knowing why. */
    /* ...every peer's identity, not the first one in: with more than two
     * seats a peer that starts late would otherwise never hear ours. And
     * every IDENT names the seats whose identity its sender already holds (in
     * the unused target field), so one that lacks ours is answered again
     * (ident_owed) -- one lost IDENT used to leave that peer unable to refuse
     * on our identity (n64lle, 4 seats at 200 ms: the relay never saw seat
     * 2's). A peer that predates the field sends 0 and is answered until the
     * bound. */
    if ((d->peer_ident_mask & rb_expect_mask(d)) == rb_expect_mask(d) && d->ident_sent &&
        !d->ident_owed)
        return;
    /* Paced by time: at most every RB_IDENT_RESEND_MS. It was paced by tick
     * ("every 8"), which is every POLL while the boot-digest gate holds sim
     * at 0, and every poll again once any one peer's identity was in: with
     * three peers at 200 ms that flooded the relay -- its RB_SYNC queue
     * refused 320 datagrams and the link simulator's hold queue overflowed,
     * all before tick 1 (n64lle, 4 seats). */
    {
        uint32_t now = rb_now(d);
        if (d->ident_sent && (uint32_t)(now - d->ident_sent_ms) < RB_IDENT_RESEND_MS)
            return;
        /* Validation (FORCE_MOD_MISMATCH=1): claim a mod set we do not have,
         * so the refusal path can be proven to fire. A refusal that has never
         * refused anything is not a safety feature. */
        if (rnet_session_send_rb_sync(
                s, 0u, d->local_build_fp, rb_claimed_content_fp(d),
                d->peer_ident_mask | (1u << rb_local_slot(d)),
                (rnet_u8)(rb_local_slot(d) < 0 ? 0 : rb_local_slot(d)),
                RNET_RB_SYNC_OP_IDENT, 0u) == 0 ||
            d->force_mod_mismatch > 0) {
            d->ident_sent = 1u;
            d->ident_sent_ms = now;
            d->ident_owed = 0u;
        }
    }
}

void rnet_rb_driver_shutdown(RNetRbDriver *d)
{
    if (!d)
        return;
    /* A shutdown mid-replay (INCREMENTAL) must not leave the host with
     * presentation suppressed. */
    if (d->in_resim && d->host.resim_end)
        d->host.resim_end(d->host.ctx);
    if (d->rb) {
        rnet_rb_destroy(d->rb);
        d->rb = NULL;
    }
    if (d->started)
        rnet_sched_bind(NULL);
    d->started = 0;
    d->stage = kRbIdle;
    d->sim = 0;
    d->in_resim = 0;
    d->pending_admit = RNET_RB_ADMIT_STALL;
    d->peer_post_mask = 0;
    d->peer_commit_mask = 0;
    d->episode_count = 0;
    d->desync_count = 0;
    d->resim_ticks = 0;
    d->fork_seen = 0;
    d->stall_tag = NULL;
}

/* ── episode teardown ────────────────────────────────────────────────── */

#define RB_COOLDOWN_TICKS 30u

/* Every stage change goes through here, so tip-hold's entry and every one of
 * its exits are paired in the log whatever path took it: "RB tip-hold ended
 * ... held=N ticks". The runway is an upper bound, not a duration -- a peer's
 * COMMIT, a tip-extend, an abort or a yield all end the hold sooner -- so a
 * sweep that reports only the runway it configured says nothing about how
 * long the quiet window was actually open. */
static void rb_stage_set(RNetRbDriver *d, RbEpisodeStage stage)
{
    if (d->stage == kRbTipHold && stage != kRbTipHold) {
        rb_log(d, "RB tip-hold ended epoch=%u held=%u ticks (runway %u) — %s\n",
               (unsigned)d->corr.epoch_id,
               (unsigned)(d->sim > d->tiphold_enter_sim ? d->sim - d->tiphold_enter_sim : 0u),
               (unsigned)rnet_rb_get_tip_runway(d->rb),
               d->tiphold_exit_why ? d->tiphold_exit_why : "cleared");
    } else if (stage == kRbTipHold && d->stage != kRbTipHold) {
        d->tiphold_enter_sim = d->sim;
    }
    d->tiphold_exit_why = NULL;
    d->stage = stage;
    d->stage_entered_ms = rb_now(d);
}

static void rb_episode_clear(RNetRbDriver *d)
{
    rb_stage_set(d, kRbIdle);
    d->initiator = 0;
    d->peer_post_mask = 0;
    d->peer_commit_mask = 0;
    d->local_base_valid = 0;
    d->peer_base_mask = 0;
    memset(&d->corr, 0, sizeof(d->corr));
    if (d->rb)
        rnet_rb_session_reset(d->rb);
}

/* Put the engine back on the live tip after an abort that followed the
 * baseline load. Without this the engine sat on the load tick's state while
 * sim stayed at the old tip, and every later tick was simulated from the
 * wrong starting point -- a desync the abort itself manufactured, even when
 * the abort's own verdict (a forced fork, a missing row) was harmless. */
static void rb_restore_tip(RNetRbDriver *d)
{
    if (!d->rewound)
        return;
    d->rewound = 0;
    rb_resim_end(d);
    if (d->host.snap_load(d->host.ctx, d->tip_tick))
        rb_log(d, "RB tip restored after the episode ended mid-replay "
                  "(tick %u) — the baseline load is undone\n",
               (unsigned)d->tip_tick);
    else
        rb_log(d, "RB tip NOT restored — no snapshot at tick %u. The engine "
                  "is on the load tick's state while sim=%u, and this peer "
                  "has diverged\n",
               (unsigned)d->tip_tick, (unsigned)d->sim);
}

/* The aborting side always takes RB_COOLDOWN_TICKS, whatever the class; only
 * the peer RECEIVING the ABORT mirrors the class (REALIGN -> 0). snesrecomp's
 * comments and its parity audit describe a peer-NACK abort as REALIGN "whose
 * cooldown is zero, so it re-opens at once" -- true of the receiver, not of
 * the initiator that aborted. Behaviour kept as shipped; the claim corrected
 * here (and at the NACK handler). */
static void rb_episode_abort(RNetRbDriver *d, uint8_t abort_class, const char *why)
{
    RNetSession *s = rb_session(d);
    rb_log(d, "RB abort epoch=%u load=%u target=%u class=%u — %s\n",
           (unsigned)d->corr.epoch_id, (unsigned)d->corr.load_tick,
           (unsigned)d->corr.target_tick, (unsigned)abort_class,
           why ? why : "?");
    if (s)
        rnet_session_send_rb_sync(s, d->corr.epoch_id, abort_class,
                                  d->sim, 0u,
                                  (rnet_u8)(d->corr.slot < 0 ? 0 : d->corr.slot),
                                  RNET_RB_SYNC_OP_ABORT, 0u);
    rb_restore_tip(d);
    d->tiphold_exit_why = "aborted";
    rb_episode_clear(d);
    d->cooldown_until_tick = d->sim + RB_COOLDOWN_TICKS;
}

/* ── seal-row exchange ───────────────────────────────────────────────── */

/*
 * Publish this peer's authoritative rows for the sealed span.
 *
 * Two wire fields here are NOT ticks, and sending ticks in them silently
 * posted nothing at all:
 *
 *   row_begin  is an OFFSET into the sealed span, zero-based
 *              (rnet_rb_export_seal_rows_chunk / rnet_rb_apply_peer_seal_rows
 *              both index `sealed[offset]`). Passing load_tick made the very
 *              first export take `row_begin >= sealed_span`, return count 0,
 *              and break the loop before a single packet went out.
 *
 *   mismatch   carries seal_base_tick, which is the LOAD tick, not
 *              corr.mismatch_tick. The receiver rejects the chunk outright
 *              when it disagrees. The two are equal whenever the snapshot
 *              floor lands on the mismatch itself, which is why this hid.
 *
 * A FOLLOWER never notices: the rollback core pre-seals a remote seat straight
 * from wire-confirmed history, so its mask completes with no peer message. An
 * INITIATOR cannot — the row that opened the episode is by definition
 * predicted, so it is the one row that must come from the peer. Measured over
 * a real SNES LAN match: follower 12/12 episodes replayed, initiator 12/12
 * "timed out waiting for peer seal rows".
 */
static void rb_send_local_seal_rows(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);
    int slot = rb_local_slot(d);
    uint32_t span = d->corr.target_tick >= d->corr.load_tick
                        ? d->corr.target_tick - d->corr.load_tick + 1u
                        : 0u;
    uint32_t off = 0;

    if (!s || !d->rb || slot >= rb_slot_count(d))
        return;   /* an observer owns no rows */
    while (off < span) {
        RNetRbFrame rows[RNET_RB_SEAL_ROWS_CHUNK_MAX];
        uint32_t count = 0;
        uint32_t want = span - off;
        if (want > RNET_RB_SEAL_ROWS_CHUNK_MAX)
            want = RNET_RB_SEAL_ROWS_CHUNK_MAX;
        if (!rnet_rb_export_seal_rows_chunk(d->rb, slot, off, want, rows,
                                            &count) || count == 0)
            break;
        rnet_session_send_rb_seal_rows(s, d->corr.epoch_id,
                                       d->corr.load_tick,
                                       d->corr.target_tick, (rnet_u8)slot,
                                       off, rows, (rnet_u16)count);
        off += count;
    }
}

/* ── replay ──────────────────────────────────────────────────────────── */

static void rb_baseline_try_compare(RNetRbDriver *d);

static void rb_send_baseline(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);
    RNetRbDigestParts p;
    if (!s)
        return;
    memset(&p, 0, sizeof(p));
    d->host.digest_parts(d->host.ctx, &p);
    /* Ours is the digest of the tick we just loaded. Keep it: the peer's copy
     * is compared against THIS, never against whatever the live machine
     * happens to hold when the message lands. */
    d->local_base = p;
    d->local_base_valid = 1;
    rnet_session_send_rb_baseline(s, d->corr.epoch_id, d->corr.load_tick,
                                  p.master, p.part[0], p.part[1], p.part[2]);
    rb_baseline_try_compare(d);
}

static void rb_replay_missing_abort(RNetRbDriver *d)
{
    char why[160];
    if (d->missing_slot == -2) {
        snprintf(why, sizeof(why), "host run_tick failed mid-replay at tick=%u "
                 "(span %u..%u)", (unsigned)d->missing_tick,
                 (unsigned)d->corr.load_tick, (unsigned)d->corr.target_tick);
        rb_episode_abort(d, RNET_RB_ABORT_CLASS_ABORT, why);
        return;
    }
    snprintf(why, sizeof(why),
             "sealed row missing mid-replay: slot=%d tick=%u "
             "(span %u..%u, seal_base=%u span_len=%u)",
             d->missing_slot, (unsigned)d->missing_tick,
             (unsigned)d->corr.load_tick,
             (unsigned)d->corr.target_tick,
             (unsigned)rnet_rb_get_seal_base_tick(d->rb),
             (unsigned)rnet_rb_get_seal_span(d->rb));
    rb_episode_abort(d, RNET_RB_ABORT_CLASS_ABORT, why);
}

/* The replay reached its target: close the resim window and put the sim clock
 * on the corrected timeline. */
static void rb_replay_finish(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);
    uint32_t off;

    d->rewound = 0;
    /* The replayed ticks' digests replace the live ones in the chain -- ours,
     * and, by FRAME_COMMIT, every peer's copy of ours. A commit re-primes the
     * chain anyway; an episode that ends any other way after this point (a
     * NACK or abort while verifying) used to leave the mispredicted digests
     * behind, and the chain stalled on a tick this replay had already fixed. */
    for (off = 0; off < RNET_RB_PEER_SEAL_MASK_BITS; ++off) {
        uint32_t t = d->corr.load_tick + off;
        if (!(d->replay_dig_mask & (1ull << off)))
            continue;
        rb_hc_note_local(d, t, d->replay_dig[off]);
        if (s)
            rnet_session_send_rb_frame_commit(s, t, d->replay_dig[off]);
    }
    d->replay_dig_mask = 0u;
    /* What was owed is paid the moment the span has been re-run on sealed --
     * i.e. authoritative -- rows, whether or not the episode then commits.
     * Waiting for the commit re-opened episodes for ticks already fixed: a
     * tip-extend the peer declined after we had replayed it, for one. */
    rb_owed_clear_span(d, d->corr.load_tick, d->corr.target_tick);
    rb_resim_end(d);
    /* Anything keyed past the target belongs to the timeline we just
     * discarded; a later episode must never load one of those. */
    d->host.snap_drop_after(d->host.ctx, d->corr.target_tick);

    d->sim = d->corr.target_tick + 1u;
    if (rb_session(d))
        rnet_session_set_sim_tick(rb_session(d), d->sim);
}

/*
 * Load the baseline snapshot and resim the sealed span.
 *
 * INLINE: runs every tick here. On SNES a resim tick is a plain RtlRunFrame
 * and the deepest span the core will open is bounded by the runway and the
 * 64-row seal mask, so the whole replay is tens of frames with no host stack
 * to unwind. Returns 1 when the replay completed.
 *
 * INCREMENTAL: loads, sends the baseline and enters kRbReplaying; poll_admit
 * then hands out one tick per call and finish_frame closes the replay. Returns
 * 0 (not complete yet), with the stage saying whether it is running.
 */
static int rb_run_replay(RNetRbDriver *d)
{
    uint32_t t;

    /* The live tip, so an abort after the load can put the engine back where
     * sim says it is (rb_restore_tip). Keyed like every snapshot: the state
     * BEFORE tick sim runs, which is the state now. */
    d->tip_tick = d->sim;
    if (!d->host.snap_save(d->host.ctx, d->tip_tick)) {
        rb_episode_abort(d, RNET_RB_ABORT_CLASS_NO_SNAP,
                         "could not save the live tip before the baseline load");
        return 0;
    }
    if (!d->host.snap_load(d->host.ctx, d->corr.load_tick)) {
        rb_episode_abort(d, RNET_RB_ABORT_CLASS_NO_SNAP, "no snapshot at load tick");
        return 0;
    }
    d->rewound = 1;
    d->replay_dig_mask = 0u;
    rb_send_baseline(d);
    /* rb_send_baseline compares digests and may abort the episode outright on
     * a fork, which clears corr and resets the session. Carrying on into the
     * loop below then re-aborted with "sealed row missing mid-replay" — one
     * cause, two log lines, and a half-replayed timeline in between. */
    if (d->stage == kRbIdle)
        return 0;
    rnet_rb_set_phase(d->rb, nRNetRbPhaseReplay);

    rb_resim_begin(d);
    if (d->cfg.replay_mode == RNET_RB_REPLAY_INCREMENTAL) {
        d->replay_next = d->corr.load_tick;
        rb_stage_set(d, kRbReplaying);
        return 0;
    }
    for (t = d->corr.load_tick; t <= d->corr.target_tick; ++t) {
        /* Re-key the store onto the replayed timeline as we go: every entry
         * from here on must describe the corrected run, not the dead one. */
        rb_snap_take(d, t);
        if (!rb_advance_sim(d, t)) {
            rb_resim_end(d);
            rb_replay_missing_abort(d);
            return 0;
        }
    }
    rb_replay_finish(d);
    return 1;
}

static void rb_enter_verify(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);
    uint32_t master = d->host.digest_master(d->host.ctx);

    rnet_rb_set_phase(d->rb, nRNetRbPhaseVerify);
    d->local_post_digest = master;
    if (s)
        rnet_session_send_rb_post(s, d->corr.epoch_id, d->corr.target_tick,
                                  master, 0u, 1u);
    d->post_sent_ms = rb_now(d);
    d->post_resend_ms = d->post_sent_ms;
    rb_stage_set(d, kRbVerifying);
    /* Do NOT clear the peer POSTs here: one may already have arrived and be
     * waiting for us. rb_episode_clear owns the reset. */
}

static void rb_commit_episode(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);

    /*
     * Do NOT call rnet_rb_on_post_match here. It sets the phase to Commit,
     * and rnet_rb_enter_tip_hold below requires Verify — so calling it first
     * made enter_tip_hold return 0 on EVERY episode and drop into the
     * else-branch clear. Measured on SNES: 0 tip-hold entries in 281 episodes
     * across three link latencies. The stage, its watchdog, the runway check
     * and the whole tip-extend branch in rb_reconcile_wire were unreachable,
     * and a parity matrix recorded tip-hold as "present" on the strength of
     * the stage existing.
     *
     * The two are alternatives, not a sequence: entering tip-hold IS the
     * post-match transition (it promotes the sealed rows itself), and
     * on_post_match is what ends the quiet window later. It is called on the
     * failure path below, which is the only place it belongs.
     */
    rnet_rb_commit_promote_sealed(d->rb);
    /* Both peers just agreed on a replayed span, and the replay re-keyed the
     * store onto that timeline. Whatever was poisoned before is gone, so the
     * cap must lift — a ratchet that only ever tightens would walk the usable
     * snapshot window down to nothing over a long match. */
    if (d->fork_cap) {
        rb_log(d, "RB fork cap lifted (episode %u committed through %u)\n",
               (unsigned)d->corr.epoch_id, (unsigned)d->corr.target_tick);
        d->fork_cap = 0u;
    }
    /* Ticks through the target are now agreed; drop the live-invent
     * FRAME_COMMITs that preceded the correction so the watermark restarts
     * from a tick both peers actually ran. */
    rb_hc_prime_after(d, d->corr.target_tick);
    rnet_sched_note_episode_boundary();
    /* Counted, not inferred: on SNES tip-hold was graded "present" for weeks
     * while the transition into it failed on every episode, and nothing in
     * the log could have said so. */
    if (rnet_rb_enter_tip_hold(d->rb)) {
        rb_log(d, "RB committed epoch=%u span=%u..%u — tip-hold for %u ticks\n",
               (unsigned)d->corr.epoch_id, (unsigned)d->corr.load_tick,
               (unsigned)d->corr.target_tick,
               (unsigned)rnet_rb_get_tip_runway(d->rb));
        d->peer_commit_mask = 0;
        rb_stage_set(d, kRbTipHold);
    } else {
        rb_log(d, "RB committed epoch=%u span=%u..%u — no tip-hold (the core "
                  "refused it); cleared\n",
               (unsigned)d->corr.epoch_id, (unsigned)d->corr.load_tick,
               (unsigned)d->corr.target_tick);
        rnet_rb_on_post_match(d->rb);
        rb_episode_clear(d);
    }
    if (s) {
        rnet_session_send_rb_sync(s, d->corr.epoch_id,
                                  d->corr.mismatch_tick, d->corr.load_tick,
                                  d->corr.target_tick,
                                  (rnet_u8)(d->corr.slot < 0 ? 0 : d->corr.slot),
                                  RNET_RB_SYNC_OP_COMMIT, 0u);
        rnet_session_send_rb_resolved(s, rnet_rb_resolved_through(d->rb));
    }
}

/* ── tip-extend ──────────────────────────────────────────────────────── */

#define RB_MAX_TIP_EXTENDS 4u

/*
 * A late edge landed past the sealed tip while we were tip-holding.
 *
 * The core grows the span and re-seals our own rows, but it will not
 * resimulate for us — rnet_rb_extend_target's contract says TipHold stays
 * TipHold and the host schedules the rereplay. The SNES host at first never
 * did, so the raise was inert: the tick stayed simulated with the predicted
 * input, and reconcile had already promoted the true row, so nothing would
 * ever look at it again.
 *
 * Rather than add a separate rereplay path, re-enter the cycle the episode
 * already knows how to run: grow the target, re-send our seal rows over the
 * wider span, drop the POST handshake that named the old tip, and go back to
 * kRbSealing. rb_pump_episode then replays and verifies exactly as it did the
 * first time. That re-runs from the original load tick rather than from the
 * old tip — more frames than psxrecomp re-runs — but the span is bounded by
 * the runway and the 64-row seal mask, and reusing the proven path beats a
 * second one that can rot independently of it.
 */
static int rb_tip_extend(RNetRbDriver *d, uint32_t tick, int slot, int notify_peer)
{
    RNetSession *s = rb_session(d);
    uint32_t old_target = d->corr.target_tick;
    uint32_t new_target = tick > old_target ? tick : old_target;
    uint32_t gap = 0;

    /* TipHold is the common case; Verifying is not. Measured at a 6-tick
     * injector with a 24-tick runway: 13 of 17 peer extends arrived while we
     * had already re-replayed and were awaiting POST, and declining them made
     * the initiator abort REALIGN over and over -- one run ended in a baseline
     * digest fork. The core has always allowed extend from Verify (it drops
     * the phase back to Replay), and psxrecomp accepts it in five phases. */
    if (!d->rb ||
        (d->stage != kRbTipHold && d->stage != kRbVerifying))
        return 0;
    /* A local extend is new work the peer has to take on; a drain opens none.
     * One the PEER asked for is still followed -- it is the peer's episode,
     * and refusing it would only turn its extend into an abort. */
    if (notify_peer && d->quiesce != RNET_RB_QUIESCE_NONE) {
        rb_log(d, "RB tip-extend refused epoch=%u tick=%u — draining, so "
                  "no new work opens\n",
               (unsigned)d->corr.epoch_id, (unsigned)tick);
        return 0;
    }
    if (d->tip_extends >= RB_MAX_TIP_EXTENDS) {
        rb_log(d, "RB tip-extend refused epoch=%u tick=%u — %u "
                  "already spent on this episode; let it commit so a fresh one "
                  "covers it\n",
               (unsigned)d->corr.epoch_id, (unsigned)tick,
               (unsigned)d->tip_extends);
        return 0;
    }
    /* The replay re-runs from load, so we must still hold that snapshot, and
     * we must be able to seal our own seat across the WHOLE grown span. */
    if (!rb_snap_has(d, d->corr.load_tick)) {
        rb_log(d, "RB tip-extend refused epoch=%u — load snapshot "
                  "%u has aged out of the ring\n",
               (unsigned)d->corr.epoch_id, (unsigned)d->corr.load_tick);
        return 0;
    }
    if (!rb_local_rows_cover(d, d->corr.load_tick, new_target, &gap)) {
        rb_log(d, "RB tip-extend refused epoch=%u span=%u..%u — "
                  "no local row at tick=%u\n",
               (unsigned)d->corr.epoch_id, (unsigned)d->corr.load_tick,
               (unsigned)new_target, (unsigned)gap);
        return 0;
    }
    if (!rnet_rb_can_extend_target(d->rb, new_target) ||
        !rnet_rb_extend_target(d->rb, new_target))
        return 0;

    d->tip_extends++;
    rb_log(d, "RB tip-extend epoch=%u %u→%u tick=%u slot=%d "
              "(#%u, %s)\n",
           (unsigned)d->corr.epoch_id, (unsigned)old_target,
           (unsigned)new_target, (unsigned)tick, slot,
           (unsigned)d->tip_extends, notify_peer ? "local" : "from peer");

    /* Tell the peer BEFORE sending rows, so its span has grown by the time
     * they land — an offset past the sealed span cannot be credited. */
    if (notify_peer && s)
        rnet_session_send_rb_sync(s, d->corr.epoch_id, tick,
                                  d->corr.load_tick, new_target,
                                  (rnet_u8)(slot < 0 ? 0 : slot),
                                  RNET_RB_SYNC_OP_BEGIN,
                                  RNET_RB_SYNC_FLAG_REREPLAY);

    d->corr.target_tick = new_target;
    /* The POST pair named the old tip and the baseline verdict was already
     * consumed; both must be re-established over the new span, or Verify
     * would match on stale digests. */
    d->peer_post_mask = 0;
    d->peer_commit_mask = 0;
    d->local_post_digest = 0;
    d->local_base_valid = 0;
    d->peer_base_mask = 0;
    rnet_rb_set_phase(d->rb, nRNetRbPhaseSealInputs);
    rb_send_local_seal_rows(d);
    d->tiphold_exit_why = notify_peer ? "tip-extend (local)" : "tip-extend (from peer)";
    rb_stage_set(d, kRbSealing);
    return 1;
}

/* ── early seal rows ─────────────────────────────────────────────────── */

static int rb_epoch_is_early(const RNetRbDriver *d, uint32_t epoch);

/* A SEAL_ROWS chunk for a peer epoch that has not opened here yet (see
 * early_seal). Sanitized on arrival, like every row. */
static void rb_early_seal_hold(RNetRbDriver *d, uint32_t epoch, uint32_t base,
                               uint32_t target, int slot, uint32_t row_begin,
                               const RNetRbFrame *rows, uint16_t count)
{
    uint32_t n;
    if (count > RNET_RB_SEAL_ROWS_CHUNK_MAX)
        count = RNET_RB_SEAL_ROWS_CHUNK_MAX;
    if (d->early_seal_n >= RB_EARLY_SEAL_MAX) {
        rb_log(d, "RB SEAL_ROWS epoch=%u held early, but the buffer is full "
                  "(%u chunks) — the oldest (epoch=%u) is dropped\n",
               (unsigned)epoch, (unsigned)RB_EARLY_SEAL_MAX,
               (unsigned)d->early_seal[0].epoch);
        memmove(&d->early_seal[0], &d->early_seal[1],
                sizeof(d->early_seal[0]) * (RB_EARLY_SEAL_MAX - 1u));
        d->early_seal_n = RB_EARLY_SEAL_MAX - 1u;
    }
    n = d->early_seal_n++;
    d->early_seal[n].epoch = epoch;
    d->early_seal[n].base = base;
    d->early_seal[n].target = target;
    d->early_seal[n].slot = slot;
    d->early_seal[n].row_begin = row_begin;
    d->early_seal[n].count = count;
    memcpy(d->early_seal[n].rows, rows, sizeof(RNetRbFrame) * count);
}

/* The episode just opened as a follower: apply the chunks that were waiting
 * for it, in arrival order (a narrower chunk sent before a tip-extend is
 * refused by the core as stale; the re-sent wider one is taken). Anything held
 * for another epoch is for an episode that will now never open here. */
static void rb_early_seal_take(RNetRbDriver *d)
{
    uint32_t i, n = 0u;
    for (i = 0; i < d->early_seal_n; ++i) {
        if (d->early_seal[i].epoch != d->corr.epoch_id)
            continue;
        rnet_rb_apply_peer_seal_rows(d->rb, d->early_seal[i].epoch,
                                     d->early_seal[i].base, d->early_seal[i].target,
                                     (int32_t)d->early_seal[i].slot,
                                     d->early_seal[i].row_begin, d->early_seal[i].rows,
                                     d->early_seal[i].count);
        n++;
    }
    if (n)
        rb_log(d, "RB SEAL_ROWS epoch=%u: %u chunk(s) taken from the early buffer\n",
               (unsigned)d->corr.epoch_id, (unsigned)n);
    /* Keep what is still early for another episode (see early_post_mask). */
    for (i = 0, n = 0u; i < d->early_seal_n; ++i) {
        if (d->early_seal[i].epoch == d->corr.epoch_id ||
            !rb_epoch_is_early(d, d->early_seal[i].epoch))
            continue;
        if (n != i)
            d->early_seal[n] = d->early_seal[i];
        n++;
    }
    d->early_seal_n = n;
}

/* ── episode open ────────────────────────────────────────────────────── */

static int rb_cooldown_active(const RNetRbDriver *d)
{
    return d->cooldown_until_tick != 0u && d->sim < d->cooldown_until_tick;
}

/* Refuse a peer's BEGIN, and remember that we did (see begin_resend_ms): a
 * repeat of that BEGIN means the NACK was lost, and it is sent again. */
static void rb_nack_begin(RNetRbDriver *d, uint32_t epoch, uint32_t mismatch,
                          uint32_t load, uint32_t frontier, int slot)
{
    RNetSession *s = rb_session(d);
    uint32_t i = d->nacked_n++ % RB_NACKED;
    d->nacked[i].epoch = epoch;
    d->nacked[i].mismatch = mismatch;
    d->nacked[i].load = load;
    d->nacked[i].frontier = frontier;
    d->nacked[i].slot = slot;
    if (s)
        rnet_session_send_rb_sync(s, epoch, mismatch, load, frontier,
                                  (rnet_u8)(slot < 0 ? 0 : slot),
                                  RNET_RB_SYNC_OP_NACK, 0u);
}

static void rb_send_nack(RNetRbDriver *d, uint32_t epoch, uint32_t mismatch,
                         uint32_t load, int slot)
{
    rb_nack_begin(d, epoch, mismatch, load, rnet_rb_resolved_through(d->rb), slot);
}

/* 1 if this BEGIN was processed before (a repeat); repeats our NACK if we
 * refused it. Otherwise notes it and returns 0. */
static int rb_begin_repeat(RNetRbDriver *d, uint32_t epoch)
{
    RNetSession *s = rb_session(d);
    uint32_t i, n = d->begin_seen_n < RB_BEGIN_SEEN ? d->begin_seen_n : RB_BEGIN_SEEN;
    for (i = 0; i < n; ++i) {
        if (d->begin_seen[i] != epoch)
            continue;
        n = d->nacked_n < RB_NACKED ? d->nacked_n : RB_NACKED;
        for (i = 0; i < n; ++i)
            if (d->nacked[i].epoch == epoch && s)
                rnet_session_send_rb_sync(s, epoch, d->nacked[i].mismatch,
                                          d->nacked[i].load, d->nacked[i].frontier,
                                          (rnet_u8)(d->nacked[i].slot < 0 ? 0
                                                                          : d->nacked[i].slot),
                                          RNET_RB_SYNC_OP_NACK, 0u);
        return 1;
    }
    d->begin_seen[d->begin_seen_n++ % RB_BEGIN_SEEN] = epoch;
    return 0;
}

static int rb_begin_episode(RNetRbDriver *d, uint32_t mismatch_tick, int slot,
                            int as_initiator, uint32_t peer_load,
                            uint32_t peer_target, uint32_t peer_epoch,
                            uint8_t peer_flags)
{
    RNetSession *s = rb_session(d);
    uint32_t load;
    uint32_t target;
    uint32_t sim_tip = d->sim ? d->sim - 1u : 0u;

    /* Every refusal says why. Silent exits here left an episode the peer had
     * opened with no trace on our side, which showed up as an intermittent
     * unaccounted episode in the ledger and nothing else. Follower-side only:
     * the initiator reaches here from reconcile, which already checked the
     * stage, so logging both sides would be noise. */
    if (!d->rb || d->stage != kRbIdle) {
        if (!as_initiator)
            rb_log(d, "RB follow refused epoch=%u — busy in %s "
                      "when the BEGIN reached rb_begin_episode\n",
                   (unsigned)peer_epoch, rb_stage_name(d->stage));
        return 0;
    }
    if (as_initiator && rb_cooldown_active(d))
        return 0;
    /* Draining: nothing new opens, and saying so is what keeps a stop from
     * hiding a correction -- the tick stays simulated on the predicted row,
     * and the count of these is printed when the drain completes. Following
     * is unaffected (below): a BEGIN the peer sent is its episode, in flight. */
    if (as_initiator && d->quiesce != RNET_RB_QUIESCE_NONE) {
        d->drain_unopened++;
        rb_log(d, "RB drain: correction not opened mismatch=%u slot=%d — "
                  "draining, so no new episode opens (%u so far)\n",
               (unsigned)mismatch_tick, slot, (unsigned)d->drain_unopened);
        return 0;
    }
    if (mismatch_tick == 0u) {
        if (!as_initiator)
            rb_log(d, "RB follow refused epoch=%u — BEGIN carried "
                      "no mismatch tick\n", (unsigned)peer_epoch);
        return 0;
    }

    if (as_initiator) {
        if (!rb_snap_floor(d, mismatch_tick, &load)) {
            /* No snapshot reaches back that far. Refusing is the honest
             * outcome — loading a different tick would resim a timeline
             * neither peer ran — but refusing SILENTLY is not: this path
             * swallowed every episode while the fork cap was wedged, and the
             * only symptom was rollback quietly not happening. Say which
             * bound stopped it. */
            rb_log(d, "RB episode refused at mismatch=%u — no "
                      "snapshot in reach (ring oldest=%u, confirmed through=%u, "
                      "fork_cap=%u)\n",
                   (unsigned)mismatch_tick, (unsigned)rb_snap_oldest_or0(d),
                   (unsigned)rnet_rb_resolved_through(d->rb),
                   (unsigned)d->fork_cap);
            rnet_sched_note_mispredict(d->sim - mismatch_tick);
            return 0;
        }
        target = rnet_rb_suggest_target(d->rb, mismatch_tick, sim_tip);
        /* suggest_target adds tip_seal_slack (default 2) so the tick after the
         * replay is already sealed. That only works if this host can hand the
         * core its own seat's rows for those ticks, and it cannot: the
         * admitted input history stops at the tip. An initiator detects its
         * mismatch on the very next tick, so the slack landed two ticks in its
         * own future and the replay died on "sealed row missing mid-replay:
         * slot=0 tick=<tip+1>" — measured on SNES episode #1 of every session.
         * Replay only ticks we hold authoritative input for; the core re-seals
         * the tip normally, and extend_target still grows the span when the
         * peer advertises more. */
        if (target > sim_tip)
            target = sim_tip;
    } else {
        load = peer_load;
        target = peer_target;
        /* The load tick is our live tip: we have simulated everything before
         * it and nothing of it. A snapshot keyed T is the state BEFORE tick T
         * runs, which is exactly the machine as it stands between two polls
         * at sim == T -- the same state the live admit of T would have saved.
         * Take it now instead of refusing (see defer_valid for what refusing
         * cost). Never mid-replay: the machine is then not at sim. */
        if (!rb_snap_has(d, load) && load == d->sim && !d->in_resim &&
            d->host.snap_save(d->host.ctx, load)) {
            d->n_follow_at_tip++;
            rb_log(d, "RB follow at our live tip epoch=%u load=%u — the load "
                      "snapshot is the live state; taken now\n",
                   (unsigned)peer_epoch, (unsigned)load);
        }
        if (!rb_snap_has(d, load)) {
            /* Every BEGIN we turn down has to be countable, or the two peers'
             * episode ledgers cannot be reconciled — which is exactly how a
             * spurious follow episode hid in the counts. */
            rb_log(d, "RB follow refused epoch=%u span=%u..%u — "
                      "no snapshot at load tick (ring oldest=%u, our sim=%u)\n",
                   (unsigned)peer_epoch, (unsigned)load, (unsigned)target,
                   (unsigned)rb_snap_oldest_or0(d), (unsigned)d->sim);
            rb_send_nack(d, peer_epoch, mismatch_tick, load, slot);
            return 0;
        }
        /* We still owe a correction BEFORE the load tick: our state at load is
         * not the one the peer holds, so following can only end in a baseline
         * fork -- the fork cap, lockstep and an abort, for a divergence we
         * already know about. Refuse and let our own re-open cover it; the
         * NACK carries a frontier below the owed tick. */
        {
            uint32_t owed_t;
            int owed_slot;
            if (rb_owed_first(d, &owed_t, &owed_slot) && owed_t < load) {
                rb_log(d, "RB follow refused epoch=%u span=%u..%u — we owe a "
                          "correction at tick=%u slot=%d before it; NACK at "
                          "frontier=%u\n",
                       (unsigned)peer_epoch, (unsigned)load, (unsigned)target,
                       (unsigned)owed_t, owed_slot,
                       (unsigned)(owed_t ? owed_t - 1u : 0u));
                rb_nack_begin(d, peer_epoch, mismatch_tick, load,
                              owed_t ? owed_t - 1u : 0u, slot);
                return 0;
            }
        }
        /* The published-input fallback in rb_vt_get_input_row covers the
         * ordinary case where the initiator is a few ticks ahead of us, but it
         * runs out D ticks past our tip. Beyond that we cannot seal our own
         * seat at all, and discovering it inside the replay is the worst place
         * to: by then a snapshot is loaded, frames have been re-run, and the
         * abort leaves a half-replayed timeline behind. Refuse before touching
         * any state. NACK carries our frontier, which the initiator demotes to
         * and re-opens over a span we can actually seal. */
        {
            uint32_t gap = 0;
            uint32_t probe = target < mismatch_tick ? mismatch_tick : target;
            if (!rb_local_rows_cover(d, load, probe, &gap)) {
                rb_log(d, "RB follow refused epoch=%u span=%u..%u "
                          "— no local row at tick=%u (our sim=%u); NACK at "
                          "frontier=%u\n",
                       (unsigned)peer_epoch, (unsigned)load, (unsigned)probe,
                       (unsigned)gap, (unsigned)d->sim,
                       (unsigned)rnet_rb_resolved_through(d->rb));
                rb_send_nack(d, peer_epoch, mismatch_tick, load, slot);
                return 0;
            }
        }
    }
    if (target < mismatch_tick)
        target = mismatch_tick;

    /* The core tracks arrived seal rows in a 64-bit per-slot mask, so an
     * offset of 64 or more can never be credited and the exchange could never
     * complete. A fork cap can push `load` well behind the mismatch, so this
     * is reachable in practice; refuse loudly rather than open an episode the
     * protocol cannot finish. */
    if (target >= load && (target - load) >= RNET_RB_PEER_SEAL_MASK_BITS) {
        rb_log(d, "RB episode refused — span %u..%u exceeds the "
                  "64-row seal mask (fork_cap=%u)\n",
               (unsigned)load, (unsigned)target, (unsigned)d->fork_cap);
        return 0;
    }

    memset(&d->corr, 0, sizeof(d->corr));
    /* Partitioned by initiator seat, so two peers opening at once never share
     * an epoch and either can tell who opened any episode from its id. */
    d->corr.epoch_id = as_initiator
                           ? rnet_rb_epoch_make(++d->epoch_seq,
                                                (uint32_t)rb_local_slot(d))
                           : peer_epoch;
    d->corr.mismatch_tick = mismatch_tick;
    d->corr.load_tick = load;
    d->corr.target_tick = target;
    d->corr.slot = slot;
    d->corr.initiator = as_initiator ? 1u : 0u;
    d->corr.from_peer_notify = as_initiator ? 0u : 1u;
    d->corr.flags = as_initiator
                        ? (rnet_rb_is_light_tip_candidate_ex(
                               load, target, rnet_rb_resolved_through(d->rb),
                               rnet_rb_get_light_tip_max_depth(d->rb))
                               ? RNET_RB_CORR_LIGHT_TIP : 0u)
                        : (peer_flags & RNET_RB_SYNC_FLAG_LIGHT_TIP
                               ? RNET_RB_CORR_LIGHT_TIP : 0u);

    rnet_rb_begin_episode(d->rb, &d->corr);
    /* Seal from the LOAD tick, not the mismatch: replay publishes a sealed row
     * for every tick it re-runs, and load..mismatch-1 are among them. */
    rnet_rb_seal_inputs(d->rb, load, target, slot);
    if (!rnet_rb_inputs_sealed(d->rb)) {
        /* This one used to clear and return, so an episode the peer had opened
         * simply vanished on our side with nothing on either log to explain
         * it. Chased three times as an intermittent "unaccounted episode". */
        rb_log(d, "RB %s refused epoch=%u span=%u..%u slot=%d — "
                  "engine would not seal the span\n",
               as_initiator ? "episode" : "follow",
               (unsigned)d->corr.epoch_id, (unsigned)load,
               (unsigned)target, slot);
        rb_episode_clear(d);
        return 0;
    }

    if (as_initiator && s)
        rnet_session_send_rb_sync(s, d->corr.epoch_id, mismatch_tick, load,
                                  target, (rnet_u8)(slot < 0 ? 0 : slot),
                                  RNET_RB_SYNC_OP_BEGIN,
                                  (rnet_u8)(d->corr.flags & RNET_RB_CORR_LIGHT_TIP
                                                ? RNET_RB_SYNC_FLAG_LIGHT_TIP : 0u));
    d->begin_resend_ms = rb_now(d);
    d->answered_mask = 0u;

    rb_send_local_seal_rows(d);
    d->initiator = as_initiator;
    d->tip_extends = 0;
    d->peer_post_mask = 0;
    d->peer_commit_mask = 0;
    d->peer_base_mask = 0;
    rb_stage_set(d, kRbSealing);
    d->episode_count++;
    if (as_initiator)
        d->ep_initiated++;
    else
        d->ep_followed++;
    /* Answers that beat this BEGIN here (see early_post_mask). One held for
     * another epoch is dropped only if that episode can no longer open here
     * (its seat has since sent a BEGIN at or past it). With more than two
     * seats two episodes are routinely in flight at once, and discarding the
     * other's answers here cost it: a follower that switched to the winning
     * episode had already thrown away the winner's POST while briefly
     * following the loser, and waited out the 2 s budget for it. */
    if (!as_initiator) {
        uint32_t m = d->early_post_mask;
        uint32_t keep_post = 0u, keep_base = 0u;
        while (m) {
            uint32_t bit = m & (~m + 1u);
            int i = rb_bit_slot(bit);
            m &= ~bit;
            if (d->early_post_epoch[i] != d->corr.epoch_id) {
                if (rb_epoch_is_early(d, d->early_post_epoch[i]))
                    keep_post |= bit;
                continue;
            }
            d->peer_post_digest[i] = d->early_post_digest[i];
            d->peer_post_target[i] = d->early_post_target[i];
            d->peer_post_mask |= bit;
            rb_log(d, "RB POST epoch=%u taken from the early buffer\n",
                   (unsigned)d->corr.epoch_id);
        }
        m = d->early_base_mask;
        while (m) {
            uint32_t bit = m & (~m + 1u);
            int i = rb_bit_slot(bit);
            m &= ~bit;
            if (d->early_base_epoch[i] != d->corr.epoch_id ||
                d->early_base_load[i] != d->corr.load_tick) {
                if (d->early_base_epoch[i] != d->corr.epoch_id &&
                    rb_epoch_is_early(d, d->early_base_epoch[i]))
                    keep_base |= bit;
                continue;
            }
            d->peer_base[i] = d->early_base[i];
            d->peer_base_mask |= bit;
            rb_log(d, "RB BASELINE epoch=%u taken from the early buffer\n",
                   (unsigned)d->corr.epoch_id);
        }
        d->early_post_mask = keep_post;
        d->early_base_mask = keep_base;
        rb_early_seal_take(d);
    }
    /* A resim episode is the whole point of rollback and must leave a trace:
     * "mispredict=N" counts DETECTIONS (and one of its call sites is the
     * refusal path), so it cannot answer "did we actually rewind and
     * replay?". load..target is the replayed span, INCLUSIVE — a one-tick
     * span replays 1 frame, not 0. */
    /* epoch= is last so the substrings harnesses count ('RESIM episode',
     * 'initiator', 'follower') are unchanged; an N-seat ledger pairs each
     * initiated epoch with every other seat's answer to it. */
    rb_log_raw(d, "rbe: RESIM episode #%u %s slot=%d mismatch=%u load=%u target=%u "
                  "(rewind %u frames, replay %u) epoch=%u\n",
               (unsigned)d->episode_count,
               as_initiator ? "initiator" : "follower", slot,
               (unsigned)mismatch_tick, (unsigned)load, (unsigned)target,
               (unsigned)(d->sim > load ? d->sim - load : 0u),
               (unsigned)(target >= load ? target - load + 1u : 0u),
               (unsigned)d->corr.epoch_id);
    return 1;
}

/* ── deferred follow ─────────────────────────────────────────────────── */

/* Follow a peer's BEGIN now, or hold it until our sim reaches its load tick
 * (defer_valid). A load tick we have passed, or are standing on, is followed
 * at once -- rb_begin_episode takes the snapshot at our tip if it has to. */
static void rb_follow_or_defer(RNetRbDriver *d, uint32_t epoch, uint32_t mismatch,
                               uint32_t load, uint32_t target, int slot, uint8_t flags)
{
    if (load > d->sim && mismatch != 0u && d->stage == kRbIdle) {
        if (!d->defer_valid || d->defer_epoch != epoch) {
            d->defer_valid = 1;
            d->defer_epoch = epoch;
            d->defer_mismatch = mismatch;
            d->defer_load = load;
            d->defer_target = target;
            d->defer_slot = slot;
            d->defer_flags = flags;
            d->defer_since_ms = rb_now(d);
            d->n_follow_deferred++;
            rb_log(d, "RB follow deferred epoch=%u span=%u..%u — our sim=%u has "
                      "not reached the load tick; following when it does\n",
                   (unsigned)epoch, (unsigned)load, (unsigned)target, (unsigned)d->sim);
        }
        return;
    }
    rb_begin_episode(d, mismatch, slot, 0, load, target, epoch, flags);
}

/* Once a poll: follow the held BEGIN when our sim reaches its load tick, or
 * refuse it on the bound. Half the episode budget, so the NACK reaches the
 * initiator before its own watchdog would have ended the episode anyway. */
static void rb_defer_pump(RNetRbDriver *d)
{
    uint32_t waited;
    if (!d->defer_valid || d->stage != kRbIdle)
        return;
    waited = rb_now(d) - d->defer_since_ms;
    if (d->sim >= d->defer_load) {
        d->defer_valid = 0;
        rb_log(d, "RB follow resumed epoch=%u after %u ms — our sim=%u reached "
                  "load=%u\n",
               (unsigned)d->defer_epoch, (unsigned)waited, (unsigned)d->sim,
               (unsigned)d->defer_load);
        rb_begin_episode(d, d->defer_mismatch, d->defer_slot, 0, d->defer_load,
                         d->defer_target, d->defer_epoch, d->defer_flags);
        return;
    }
    if (waited > d->seal_timeout_ms / 2u) {
        d->defer_valid = 0;
        rb_log(d, "RB follow refused epoch=%u span=%u..%u — no snapshot at load "
                  "tick: our sim=%u did not reach it within %u ms; NACK at "
                  "frontier=%u\n",
               (unsigned)d->defer_epoch, (unsigned)d->defer_load,
               (unsigned)d->defer_target, (unsigned)d->sim, (unsigned)waited,
               (unsigned)rnet_rb_resolved_through(d->rb));
        rb_send_nack(d, d->defer_epoch, d->defer_mismatch, d->defer_load, d->defer_slot);
    }
}

/* ── wire ingress ────────────────────────────────────────────────────── */

/*
 * Compare baseline digests once BOTH exist. Ours only exists after the replay
 * has loaded the snapshot for corr.load_tick — until then the live machine is
 * sitting on a later tick, and digesting it would compare two different
 * instants. Doing exactly that was a defect: on a loopback pair the peer's
 * BASELINE always beat our own load, so every episode reported a fork and
 * aborted (measured 83/83 on SNES).
 *
 * Validation (FORCE_FORK=N): declare every Nth baseline exchange a fork even
 * though the digests agree. The fork CAP and its recovery are otherwise
 * unreachable in testing — real forks stopped once replay became
 * deterministic — and an untested recovery path is the one that fails in a
 * match. Nothing about the guest is touched; only the verdict is.
 */
static void rb_baseline_try_compare(RNetRbDriver *d)
{
    const RNetRbDigestParts *mine = &d->local_base;
    uint32_t pending;

    if (!d->local_base_valid || !d->peer_base_mask)
        return;
    pending = d->peer_base_mask;
    d->peer_base_mask = 0;   /* one verdict per exchange */

    while (pending) {
        uint32_t bit = pending & (~pending + 1u);
        int from = rb_bit_slot(bit);
        const RNetRbDigestParts *theirs = &d->peer_base[from];
        int forced = 0;
        pending &= ~bit;

        if (d->force_fork_every > 0 &&
            (++d->force_fork_n % (unsigned long)d->force_fork_every) == 0ul) {
            forced = 1;
            rb_log_raw(d, "rbe: forced fork verdict at load=%u "
                          "(digests actually agree)\n",
                       (unsigned)d->corr.load_tick);
        }
        if (mine->master == theirs->master && !forced)
            continue;

        /* The peers do not agree on the state they are about to replay from,
         * so the replay is doomed before it starts. Name the subsystem that
         * moved — "the state differs" is not a diagnosis. */
        d->fork_seen = 1;
        d->fork_tick = d->corr.load_tick;
        d->fork_mine = mine->master;
        d->fork_theirs = theirs->master;
        if (mine->part[0] != theirs->part[0])
            d->fork_partition = d->cfg.part_names[0] ? d->cfg.part_names[0] : "part0";
        else if (mine->part[1] != theirs->part[1])
            d->fork_partition = d->cfg.part_names[1] ? d->cfg.part_names[1] : "part1";
        else if (mine->part[2] != theirs->part[2])
            d->fork_partition = d->cfg.part_names[2] ? d->cfg.part_names[2] : "part2";
        else
            d->fork_partition = "other";
        d->desync_count++;
        if (d->fork_cap == 0u || d->corr.load_tick < d->fork_cap) {
            d->fork_cap = d->corr.load_tick;
            rb_log(d, "RB fork cap — next load must be < %u\n", (unsigned)d->fork_cap);
        }
        rb_log(d, "RB BASELINE FORK tick=%u partition=%s "
                  "local=%08x peer=%08x\n",
               (unsigned)d->corr.load_tick, d->fork_partition,
               (unsigned)mine->master, (unsigned)theirs->master);
        rb_enter_lockstep(d, "baseline fork");
        rb_episode_abort(d, RNET_RB_ABORT_CLASS_ABORT, "baseline digest fork");
        return;
    }
}

/* An answer for a PEER's episode whose BEGIN we have not processed yet. Our
 * own epochs never qualify (their BEGIN is ours), nor does one at or below
 * the newest BEGIN already seen from that seat (that episode is over). */
static int rb_epoch_is_early(const RNetRbDriver *d, uint32_t epoch)
{
    uint32_t init = rnet_rb_epoch_initiator(epoch);
    if ((int)init == rb_local_slot(d) || init >= RB_MAX_SLOTS)
        return 0;
    if (d->stage != kRbIdle && epoch == d->corr.epoch_id)
        return 0;
    /* A BEGIN held until we reach its load tick: its answers are early too. */
    if (d->defer_valid && epoch == d->defer_epoch)
        return 1;
    return (epoch >> RNET_RB_EPOCH_SLOT_BITS) > d->begin_seen_seq[init];
}

static void rb_on_peer_baseline(RNetRbDriver *d, int from, uint32_t epoch,
                                uint32_t load_tick, const RNetRbDigestParts *p)
{
    uint32_t bit;
    if (rb_epoch_is_early(d, epoch)) {
        bit = rb_from_bit(d, from);
        if (bit) {
            int i = rb_bit_slot(bit);
            d->early_base_epoch[i] = epoch;
            d->early_base_load[i] = load_tick;
            d->early_base[i] = *p;
            d->early_base_mask |= bit;
            rb_log(d, "RB BASELINE epoch=%u arrived before its BEGIN — held "
                      "for it\n", (unsigned)epoch);
        }
        return;
    }
    if (d->stage == kRbIdle || epoch != d->corr.epoch_id ||
        load_tick != d->corr.load_tick)
        return;
    bit = rb_from_bit(d, from);
    if (!bit) {
        rb_log(d, "RB BASELINE from seat %d ignored — that seat takes no "
                  "part in this match\n", from);
        return;
    }
    d->peer_base[rb_bit_slot(bit)] = *p;
    d->peer_base_mask |= bit;
    d->answered_mask |= bit;
    rb_baseline_try_compare(d);
}

/*
 * Buffer the peer's POST rather than requiring us to already be verifying.
 *
 * Whoever finishes replaying first sends POST first, and that is normally the
 * INITIATOR: it rewinds one frame while the follower rewinds however far
 * behind the initiator it was running. Insisting on kRbVerifying here threw
 * the initiator's POST away, and the follower then burned the full episode
 * budget waiting for a message that had already been delivered (SNES: 8 of 8
 * follower episodes aborted over a real Linux/Windows match).
 *
 * An exchange is not a stage, and a message that arrives early is still an
 * answer. The tip check moves to the point of use, where corr.target_tick is
 * final.
 */
static void rb_on_peer_post(RNetRbDriver *d, int from, uint32_t epoch,
                            uint32_t target, uint32_t master)
{
    uint32_t bit;
    if (rb_epoch_is_early(d, epoch)) {
        bit = rb_from_bit(d, from);
        if (bit) {
            int i = rb_bit_slot(bit);
            d->early_post_epoch[i] = epoch;
            d->early_post_target[i] = target;
            d->early_post_digest[i] = master;
            d->early_post_mask |= bit;
            rb_log(d, "RB POST epoch=%u arrived before its BEGIN — held for "
                      "it\n", (unsigned)epoch);
        }
        return;
    }
    if (d->stage == kRbIdle || epoch != d->corr.epoch_id)
        return;
    bit = rb_from_bit(d, from);
    if (!bit) {
        rb_log(d, "RB POST from seat %d ignored — that seat takes no part "
                  "in this match\n", from);
        return;
    }
    /* Time from our POST going out to the peer's coming back. Sampled only
     * while we are the one waiting, so a POST that arrived before we even
     * entered Verify cannot be timed against a previous episode's clock. */
    if (d->stage == kRbVerifying && d->post_sent_ms != 0u) {
        uint32_t sample = rb_now(d) - d->post_sent_ms;
        d->post_sent_ms = 0u;
        /* Anything past a second is a stalled peer, not a link measurement. */
        if (sample <= 1000u)
            d->rtt_ema_ms = d->rtt_ema_ms
                                ? (uint32_t)((3u * d->rtt_ema_ms + sample) / 4u)
                                : sample;
    }
    d->peer_post_digest[rb_bit_slot(bit)] = master;
    d->peer_post_target[rb_bit_slot(bit)] = target;
    d->peer_post_mask |= bit;
    d->answered_mask |= bit;
}

static void rb_drain_wire(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);
    rnet_u32 epoch, a, b, c;
    rnet_u8 slot, op, flags;

    if (!s)
        return;

    while (rnet_session_take_rb_sync(s, &epoch, &a, &b, &c, &slot, &op, &flags)) {
        int from = rnet_session_rb_last_take_from(s);
        switch (op) {
        case RNET_RB_SYNC_OP_FRAME_COMMIT_REQUEST:
            if ((int)slot == rb_local_slot(d) && rb_from_bit(d, from)) {
                uint32_t mask = rb_expect_mask(d);
                uint32_t digest;
                if (mask && a < d->sim) {
                    int first = rb_bit_slot(mask & (~mask + 1u));
                    if (rnet_hc_local_digest(&d->hc[first], a, &digest))
                        rnet_session_send_rb_frame_commit(s, a, digest);
                }
            }
            break;
        case RNET_RB_SYNC_OP_BEGIN:
            if (rb_is_observer(d)) {
                /* See rb_is_observer: never predicted, so nothing to correct;
                 * never answers (the relay would drop it anyway). Counted. */
                d->n_observer_skipped++;
                if (!(flags & RNET_RB_SYNC_FLAG_REREPLAY))
                    rb_log(d, "RB observer ignores episode epoch=%u (seat %d) — "
                              "lockstep, nothing to correct\n",
                           (unsigned)epoch, from);
                break;
            }
            /* A tip-extend for the episode we are already holding — not a new
             * episode, so it must be recognised before the dual-initiation
             * arbitration below, which would otherwise decline our own
             * epoch back to the peer. Only the seat that PREDICTED the row
             * detects the edge; the seat that owns that input never
             * mispredicts it, so this message is the only way it learns the
             * span moved. */
            if (flags & RNET_RB_SYNC_FLAG_REREPLAY) {
                /* Gate on the FLAG, not on our stage. A tip-extend is never a
                 * new episode, and letting one fall through to the arbitration
                 * below opened a follow episode for an epoch the peer believed
                 * was mid-extend: the SNES follower logged 39 episodes against
                 * the initiator's 36 — exactly the four extends less the one
                 * already NACK'd. */
                if ((d->stage == kRbTipHold || d->stage == kRbVerifying) &&
                    epoch == d->corr.epoch_id) {
                    if (!rb_tip_extend(d, c, (int)slot, 0))
                        rb_episode_abort(d, RNET_RB_ABORT_CLASS_REALIGN,
                                         "cannot follow peer tip-extend");
                } else if (d->defer_valid && epoch == d->defer_epoch) {
                    /* The episode we are waiting to follow grew before we
                     * reached it: follow the grown span. Its re-sent rows are
                     * held with the rest (rb_epoch_is_early). */
                    if (c > d->defer_target) {
                        rb_log(d, "RB follow deferred epoch=%u tip-extended "
                                  "%u→%u while we wait for load=%u\n",
                               (unsigned)epoch, (unsigned)d->defer_target,
                               (unsigned)c, (unsigned)d->defer_load);
                        d->defer_target = c;
                    }
                } else {
                    /* We already released that episode (our runway is shorter
                     * than the round trip) or never held it. Refuse, so the
                     * peer ends it and re-opens rather than waiting out a
                     * handshake we are never going to answer. */
                    rb_log(d, "RB tip-extend declined epoch=%u "
                              "target=%u — we are %s%s\n",
                           (unsigned)epoch, (unsigned)c,
                           rb_stage_name(d->stage),
                           epoch == d->corr.epoch_id ? "" : " on another epoch");
                    rnet_session_send_rb_sync(
                        s, epoch, a, b, rnet_rb_resolved_through(d->rb),
                        slot, RNET_RB_SYNC_OP_NACK, 0u);
                }
                break;
            }
            /* A BEGIN is answered once. Its initiator repeats it until every
             * peer has answered (begin_resend_ms); a repeat of one we refused
             * repeats the NACK, anything else about it is already under way. */
            if (rb_begin_repeat(d, epoch))
                break;
            {
                uint32_t init = rnet_rb_epoch_initiator(epoch);
                uint32_t seq = epoch >> RNET_RB_EPOCH_SLOT_BITS;
                if (init < RB_MAX_SLOTS && seq > d->begin_seen_seq[init]) {
                    d->begin_seen_seq[init] = seq;
                } else if (init < RB_MAX_SLOTS && seq < d->begin_seen_seq[init]) {
                    /* Overtaken on the link by a newer BEGIN from the same
                     * seat: that initiator has left this episode. Refused,
                     * so the ledger still hears one answer for it. */
                    rb_log(d, "RB follow refused epoch=%u — it arrived after a "
                              "newer BEGIN from seat %u; NACK at frontier=%u\n",
                           (unsigned)epoch, (unsigned)init,
                           (unsigned)rnet_rb_resolved_through(d->rb));
                    rb_nack_begin(d, epoch, a, b, rnet_rb_resolved_through(d->rb),
                                  (int)slot);
                    break;
                }
            }
            /* Concurrent dual initiation: lower initiator seat wins, the
             * loser yields and follows. The initiator's seat is in the epoch
             * id, so this holds for any number of seats.
             *
             * It is the episode's initiator that is compared, not our own
             * seat: with more than two seats a FOLLOWER can hold the losing
             * episode when the winning BEGIN arrives, and it must switch too
             * -- the loser's initiator is about to yield, so staying would
             * only NACK the winner and kill both. With two seats a follower's
             * episode is always the other seat's, the comparison is false, and
             * the rule is the one it always was. */
            if (d->stage != kRbIdle) {
                int peer_init = (int)rnet_rb_epoch_initiator(epoch);
                int held_init = (int)rnet_rb_epoch_initiator(d->corr.epoch_id);
                if (peer_init < held_init) {
                    if (!d->initiator)
                        rb_log(d, "RB follow switched epoch=%u → epoch=%u — the "
                                  "lower seat's episode wins, and seat %d will yield "
                                  "to it too\n",
                               (unsigned)d->corr.epoch_id, (unsigned)epoch, held_init);
                    d->tiphold_exit_why = "yielded to a peer BEGIN";
                    rb_episode_clear(d);
                } else if (!d->initiator && peer_init == held_init &&
                           (epoch >> RNET_RB_EPOCH_SLOT_BITS) >
                               (d->corr.epoch_id >> RNET_RB_EPOCH_SLOT_BITS)) {
                    /* An initiator runs one episode at a time, so a newer BEGIN
                     * from the one whose episode we follow means it has left
                     * that one -- committed, or aborted with an ABORT we did
                     * not get. Holding on (a COMMIT lost on the link leaves a
                     * follower in tip-hold) only NACKed the new episode. Our
                     * side is safe to drop at any stage the drain runs in:
                     * sealing has loaded nothing, and a finished replay ran on
                     * authoritative rows. */
                    rb_log(d, "RB follow released epoch=%u in %s — seat %d has "
                              "opened epoch=%u, so it is over there\n",
                           (unsigned)d->corr.epoch_id, rb_stage_name(d->stage),
                           held_init, (unsigned)epoch);
                    d->tiphold_exit_why = "its initiator opened the next episode";
                    rb_episode_clear(d);
                } else {
                    /* We are busy — either mid-episode of our own, or we won
                     * the dual-initiation race. Either way this BEGIN is not
                     * going to be followed, and the peer has to be TOLD.
                     *
                     * Dropping it silently left the initiator sealed and
                     * waiting on rows that could never arrive until its
                     * watchdog fired, which freezes the sim for the whole
                     * timeout (SNES, 300 ms RTT: two episodes per minute the
                     * follower had no record of at all).
                     *
                     * NACK is exactly the right reply and the initiator
                     * already handles it: demote to our frontier and abort
                     * REALIGN (we mirror REALIGN as no cooldown; the
                     * initiator itself still takes RB_COOLDOWN_TICKS -- see
                     * rb_episode_abort).
                     * If we won the race our own BEGIN is already in flight;
                     * should this NACK land after the peer has yielded to it,
                     * the epoch no longer matches theirs and it is ignored.
                     * Safe in either order. */
                    rb_log(d, "RB follow refused epoch=%u — busy "
                              "in %s (ours epoch=%u); NACK at frontier=%u\n",
                           (unsigned)epoch, rb_stage_name(d->stage),
                           (unsigned)d->corr.epoch_id,
                           (unsigned)rnet_rb_resolved_through(d->rb));
                    rb_nack_begin(d, epoch, a, b, rnet_rb_resolved_through(d->rb),
                                  (int)slot);
                    break;
                }
            }
            /* A BEGIN held for its load tick (defer_valid) is arbitrated like
             * an open episode: the lower initiator seat wins, and a newer
             * episode from the same seat means the held one has ended there. */
            if (d->defer_valid) {
                uint32_t held_init = rnet_rb_epoch_initiator(d->defer_epoch);
                uint32_t new_init = rnet_rb_epoch_initiator(epoch);
                if (epoch == d->defer_epoch)
                    break;   /* a duplicate of the one we hold */
                if (new_init < held_init ||
                    (new_init == held_init &&
                     (epoch >> RNET_RB_EPOCH_SLOT_BITS) >
                         (d->defer_epoch >> RNET_RB_EPOCH_SLOT_BITS))) {
                    rb_log(d, "RB follow refused epoch=%u span=%u..%u — superseded "
                              "by epoch=%u before we reached its load tick; NACK "
                              "at frontier=%u\n",
                           (unsigned)d->defer_epoch, (unsigned)d->defer_load,
                           (unsigned)d->defer_target, (unsigned)epoch,
                           (unsigned)rnet_rb_resolved_through(d->rb));
                    rb_send_nack(d, d->defer_epoch, d->defer_mismatch, d->defer_load,
                                 d->defer_slot);
                    d->defer_valid = 0;
                } else {
                    rb_log(d, "RB follow refused epoch=%u — busy waiting to follow "
                              "epoch=%u (its seat is lower); NACK at frontier=%u\n",
                           (unsigned)epoch, (unsigned)d->defer_epoch,
                           (unsigned)rnet_rb_resolved_through(d->rb));
                    rb_nack_begin(d, epoch, a, b, rnet_rb_resolved_through(d->rb),
                                  (int)slot);
                    break;
                }
            }
            rb_follow_or_defer(d, epoch, a, b, c, (int)slot, flags);
            break;
        case RNET_RB_SYNC_OP_NACK:
            if (d->stage != kRbIdle && epoch == d->corr.epoch_id) {
                /* c carries the follower's confirmed frontier: demote to a
                 * mutually provable tick instead of guessing load-1. */
                rnet_rb_demote_resolved_through(d->rb, c);
                rb_episode_abort(d, RNET_RB_ABORT_CLASS_REALIGN, "peer NACK");
            }
            break;
        case RNET_RB_SYNC_OP_ABORT:
            if (d->defer_valid && epoch == d->defer_epoch) {
                /* The episode we were waiting to follow ended before we
                 * reached it. We never opened it, so it is a refusal on our
                 * side of the ledger -- counted, never silent -- and no NACK:
                 * its initiator has already let it go. */
                rb_log(d, "RB follow refused epoch=%u span=%u..%u — its initiator "
                          "aborted it (class %u) before we reached its load tick\n",
                       (unsigned)epoch, (unsigned)d->defer_load,
                       (unsigned)d->defer_target, (unsigned)a);
                d->defer_valid = 0;
                break;
            }
            if (d->stage != kRbIdle && epoch == d->corr.epoch_id) {
                /* Say so. Clearing silently made the two peers' logs disagree
                 * about the same session — measured 9 aborts on the initiator
                 * against 140 on the follower. An episode that did not commit
                 * must leave a trace on BOTH sides. */
                rb_log(d, "RB episode dropped by peer epoch=%u "
                          "load=%u target=%u class=%u (peer aborted)\n",
                       (unsigned)d->corr.epoch_id,
                       (unsigned)d->corr.load_tick,
                       (unsigned)d->corr.target_tick, (unsigned)a);
                d->tiphold_exit_why = "peer aborted";
                rb_episode_clear(d);
                /* Mirror the sender's cooldown class so both peers re-arm on
                 * the same schedule. */
                d->cooldown_until_tick =
                    d->sim + (a == RNET_RB_ABORT_CLASS_REALIGN ? 0u : RB_COOLDOWN_TICKS);
            }
            break;
        case RNET_RB_SYNC_OP_IDENT: {
            /* Per seat: every peer's identity is judged, each once. */
            uint32_t bit = rb_from_bit(d, from);
            int seat = rb_bit_slot(bit);
            if (!bit)
                break;
            /* Retransmits of an identity already judged say nothing new; a
             * refusal is one verdict, not one per copy. */
            /* c: the seats whose identity the sender holds. */
            if (!(c & (1u << rb_local_slot(d))))
                d->ident_owed = 1u;
            if ((d->peer_ident_mask & bit) && d->peer_build_fp[seat] == a &&
                d->peer_content_fp[seat] == b)
                break;
            d->peer_build_fp[seat] = a;
            d->peer_content_fp[seat] = b;
            d->peer_ident_mask |= bit;
            if ((d->local_build_fp | d->local_content_fp) != 0u) {
                int build_ok = (a == d->local_build_fp);
                int content_ok = (b == rb_claimed_content_fp(d));
                if (content_ok && build_ok) {
                    rb_log(d, "peer identity matches "
                              "(seat %d: build=%08x content=%08x)\n",
                           seat, (unsigned)a, (unsigned)b);
                    break;
                }
                if (!build_ok)
                    rb_log(d, "peer BUILD differs (seat %d) ours=%08x "
                              "peer=%08x — not refused on its own, because a "
                              "build can differ without changing what the guest "
                              "simulates. The boot digest is the arbiter.\n",
                           seat, (unsigned)d->local_build_fp, (unsigned)a);
                if (!content_ok) {
                    /* REFUSE. A mod set is not like a build: every mod in it
                     * exists to change what the guest simulates, so two peers
                     * with different sets are running different games by
                     * definition, and no amount of rollback reconciles that.
                     * Both peers receive the other's identity and refuse
                     * independently, so neither waits on the other. */
                    rb_log(d, "MOD SETS DIFFER ours=%08x "
                              "peer=%08x (seat %d) — this match cannot be played. "
                              "Every mod in the set changes what the guest "
                              "simulates, so the two sides are running different "
                              "games. Compare the \"game: mod set\" line in each "
                              "log; the host's selection is the one to match.\n",
                           (unsigned)rb_claimed_content_fp(d), (unsigned)b, seat);
                    if (d->allow_mod_mismatch) {
                        rb_log(d, "  %s=1 — continuing "
                                  "anyway; expect an immediate desync\n",
                               d->allow_mod_mismatch_env);
                        break;
                    }
                    rb_refuse(d, "mod_set_mismatch");
                }
            }
            break;
        }
        case RNET_RB_SYNC_OP_COMMIT:
            if (d->stage == kRbTipHold && epoch == d->corr.epoch_id) {
                uint32_t bit = rb_from_bit(d, from);
                uint32_t expect = rb_expect_mask(d);
                d->peer_commit_mask |= bit;
                /* Every peer has committed and left: nothing can tip-extend
                 * this episode any more. */
                if ((d->peer_commit_mask & expect) == expect) {
                    d->tiphold_exit_why = "every peer committed";
                    rb_episode_clear(d);
                }
            }
            break;
        case RNET_RB_SYNC_OP_QUIESCE: {
            /* The peer will open no more episodes. Counted per peer; a peer
             * that is draining makes us drain too, so asking one side stops
             * the match rather than leaving the other opening episodes into a
             * peer that is about to leave. */
            uint32_t bit = rb_from_bit(d, from);
            if (!bit)
                break;
            if (!(d->peer_quiesce_mask & bit))
                rb_log(d, "RB quiesce: seat %d will open no more episodes "
                          "(its sim=%u, ours=%u)\n",
                       rb_bit_slot(bit), (unsigned)b, (unsigned)d->sim);
            d->peer_quiesce_mask |= bit;
            if (a)
                d->peer_quiesce_ack_mask |= bit;
            if (d->quiesce == RNET_RB_QUIESCE_NONE) {
                d->quiesce_origin = "the peer is draining";
                rnet_rb_driver_request_quiesce(d);
            }
            break;
        }
        default:
            break;
        }
    }

    for (;;) {
        RNetRbFrame rows[RNET_RB_SEAL_ROWS_CHUNK_MAX];
        rnet_u16 count = 0;
        rnet_u32 row_begin = 0;
        if (!rnet_session_take_rb_seal_rows(s, &epoch, &a, &b, &slot,
                                            &row_begin, rows, &count))
            break;
        if (count == 0)
            continue;
        {
            rnet_u16 i;
            for (i = 0; i < count; ++i)
                rb_row_sanitize(d, (int)slot, &rows[i]);
        }
        if (d->stage == kRbIdle || epoch != d->corr.epoch_id) {
            if (rb_epoch_is_early(d, epoch))
                rb_early_seal_hold(d, epoch, a, b, (int)slot, row_begin, rows, count);
            continue;
        }
        rnet_rb_apply_peer_seal_rows(d->rb, epoch, a, b, (int32_t)slot,
                                     row_begin, rows, count);
        d->answered_mask |= rb_from_bit(d, rnet_session_rb_last_take_from(s));
    }

    {
        RNetRbDigestParts p;
        while (rnet_session_take_rb_baseline(s, &epoch, &a, &p.master, &p.part[0],
                                             &p.part[1], &p.part[2]))
            rb_on_peer_baseline(d, rnet_session_rb_last_take_from(s), epoch, a, &p);
    }

    while (rnet_session_take_rb_post(s, &epoch, &a, &b, &c, &op))
        rb_on_peer_post(d, rnet_session_rb_last_take_from(s), epoch, a, b);

    while (rnet_session_take_rb_resolved(s, &a)) {
        if (d->rb)
            rnet_rb_set_peer_convergence(d->rb, a);
    }

    /* Each peer's commits into its own chain. With one peer rb_from_bit is
     * that peer whatever the header says, exactly as before. */
    while (rnet_session_take_rb_frame_commit(s, &a, &b)) {
        uint32_t bit = rb_from_bit(d, rnet_session_rb_last_take_from(s));
        if (bit)
            rnet_hc_note_peer(&d->hc[rb_bit_slot(bit)], a, b);
    }
}

/* ── episode pump ────────────────────────────────────────────────────── */

/*
 * An episode stalls the sim while it waits on the peer. A lost SEAL_ROWS or
 * POST datagram must therefore not be able to wait forever — without a
 * watchdog a single dropped packet freezes the match with no diagnosis. The
 * budget is generous relative to any plausible RTT: expiring is a real
 * failure, so it aborts loudly and takes the cooldown.
 *
 * kRbReplaying has none: it waits on no peer, only on the host running the
 * ticks it was handed, and a host that stops calling cannot be rescued by a
 * check that only runs when it calls.
 */
static int rb_stage_expired(RNetRbDriver *d)
{
    uint32_t now = rb_now(d);
    return (uint32_t)(now - d->stage_entered_ms) > d->seal_timeout_ms;
}

#define RB_POST_RESEND_MIN_MS 50u

/* See begin_resend_ms. Initiator only, while the episode waits on peers. */
static void rb_begin_resend(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);
    uint32_t now = rb_now(d);
    uint32_t expect = rb_expect_mask(d);
    uint32_t every = d->rtt_ema_ms > RB_POST_RESEND_MIN_MS ? d->rtt_ema_ms
                                                            : RB_POST_RESEND_MIN_MS;
    if (!s || !d->initiator || (d->answered_mask & expect) == expect ||
        (uint32_t)(now - d->begin_resend_ms) < every)
        return;
    rnet_session_send_rb_sync(s, d->corr.epoch_id, d->corr.mismatch_tick,
                              d->corr.load_tick, d->corr.target_tick,
                              (rnet_u8)(d->corr.slot < 0 ? 0 : d->corr.slot),
                              RNET_RB_SYNC_OP_BEGIN,
                              (rnet_u8)(d->corr.flags & RNET_RB_CORR_LIGHT_TIP
                                            ? RNET_RB_SYNC_FLAG_LIGHT_TIP : 0u));
    d->begin_resend_ms = now;
    d->n_begin_resent++;
}

/* See post_resend_ms. */
static void rb_post_resend(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);
    uint32_t now = rb_now(d);
    uint32_t every = d->rtt_ema_ms > RB_POST_RESEND_MIN_MS ? d->rtt_ema_ms
                                                            : RB_POST_RESEND_MIN_MS;
    if (!s || (uint32_t)(now - d->post_resend_ms) < every)
        return;
    rnet_session_send_rb_post(s, d->corr.epoch_id, d->corr.target_tick,
                              d->local_post_digest, 0u, 1u);
    d->post_resend_ms = now;
    d->n_post_resent++;
}

static void rb_pump_episode(RNetRbDriver *d)
{
    switch (d->stage) {
    case kRbSealing:
        if (rnet_rb_all_peer_seal_rows_complete(d->rb)) {
            rnet_rb_set_phase(d->rb, nRNetRbPhaseAwaitingBaseline);
            if (rb_run_replay(d))
                rb_enter_verify(d);
        } else if (rb_stage_expired(d)) {
            rb_episode_abort(d, RNET_RB_ABORT_CLASS_ABORT,
                             "timed out waiting for peer seal rows");
        } else {
            rb_begin_resend(d);
        }
        break;
    case kRbVerifying: {
        uint32_t expect = rb_expect_mask(d);
        uint32_t m = d->peer_post_mask;
        /* After a tip-extend a peer may still hold a POST for the prior tip;
         * treating that as this episode's answer is a false fork. Drop it and
         * keep waiting for one that describes the tip we actually verified. */
        while (m) {
            uint32_t bit = m & (~m + 1u);
            int i = rb_bit_slot(bit);
            m &= ~bit;
            if (!rnet_rb_peer_post_tip_ok(d->peer_post_target[i], d->corr.target_tick))
                d->peer_post_mask &= ~bit;
        }
        if (d->peer_post_mask && (d->peer_post_mask & expect) == expect) {
            uint32_t local = d->local_post_digest;
            int bad = -1;
            int i;
            for (i = 0; i < RB_MAX_SLOTS; ++i) {
                if ((expect & (1u << i)) && d->peer_post_digest[i] != local) {
                    bad = i;
                    break;
                }
            }
            if (bad < 0) {
                rb_commit_episode(d);
            } else {
                d->desync_count++;
                d->fork_seen = 1;
                d->fork_tick = d->corr.target_tick;
                d->fork_mine = local;
                d->fork_theirs = d->peer_post_digest[bad];
                d->fork_partition = "post";
                rb_log(d, "RB POST FORK tick=%u local=%08x "
                          "peer=%08x\n",
                       (unsigned)d->corr.target_tick, (unsigned)local,
                       (unsigned)d->peer_post_digest[bad]);
                rnet_rb_on_post_diverge(d->rb);
                rb_enter_lockstep(d, "post fork");
                rb_episode_abort(d, RNET_RB_ABORT_CLASS_ABORT, "post digest fork");
            }
        } else if (rb_stage_expired(d)) {
            rb_episode_abort(d, RNET_RB_ABORT_CLASS_ABORT,
                             "timed out waiting for peer POST");
        } else {
            rb_begin_resend(d);
            rb_post_resend(d);
        }
        break;
    }
    case kRbTipHold:
        if (d->sim > d->corr.target_tick + rnet_rb_get_tip_runway(d->rb)) {
            d->tiphold_exit_why = "runway spent";
            rb_episode_clear(d);
        } else if (rb_stage_expired(d)) {
            /* A tip-hold that never met its exit condition wedged silently —
             * and while the stage is not Idle rb_begin_episode refuses EVERY
             * new episode, which disables rollback without a single line in
             * the log. No non-idle stage may sit without a bound.
             *
             * Released, not aborted: this episode already committed, and the
             * peer has moved on. Sending it an ABORT would retract work both
             * sides agreed on. */
            rb_log(d, "RB tip-hold expired epoch=%u target=%u "
                      "sim=%u — releasing\n",
                   (unsigned)d->corr.epoch_id,
                   (unsigned)d->corr.target_tick, (unsigned)d->sim);
            d->tiphold_exit_why = "watchdog expired";
            rb_episode_clear(d);
        } else {
            /* A peer that lost our POST is still verifying; every peer that
             * has committed has told us so, and tip-hold ends when all have. */
            rb_post_resend(d);
        }
        break;
    default:
        break;
    }
}

/* ── reconcile late wire against predicted history ───────────────────── */

static void rb_reconcile_wire(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);
    int slots = rb_slot_count(d);
    int local = rb_local_slot(d);
    uint32_t from = rnet_rb_resolved_through(d->rb) + 1u;
    int slot;

    /* Nothing older than the history window can be reconciled anyway, and an
     * un-advancing watermark would otherwise make this scan grow without
     * bound as the match runs. */
    if (d->sim > RNET_INPUT_HIST_DEPTH &&
        from < d->sim - RNET_INPUT_HIST_DEPTH)
        from = d->sim - RNET_INPUT_HIST_DEPTH;

    if (!s)
        return;

    /* A correction owed from an episode that did not commit comes first: it is
     * the oldest thing wrong with our timeline. */
    if (d->stage == kRbIdle && !rb_cooldown_active(d) && d->sim >= d->owed_retry_after &&
        d->quiesce == RNET_RB_QUIESCE_NONE && !d->defer_valid) {
        uint32_t t, load;
        int oslot;
        if (rb_owed_first(d, &t, &oslot)) {
            if (!rb_snap_floor(d, t, &load)) {
                /* Past every snapshot: nothing can re-run this tick any more.
                 * Said as what it is. Whether the peers now differ depends on
                 * whether the guest read that seat's input on that tick: on
                 * n64lle, 17 corrections to a port the attract scene never
                 * polls were lost in one 4-seat 2 % run and nothing forked,
                 * so this line used to overstate it ("the two sides have
                 * diverged"). The chain and every later baseline say which. */
                rb_log(d, "RB correction LOST tick=%u slot=%d — no snapshot "
                          "reaches it any more (ring oldest=%u). This peer ran "
                          "that tick on input its owner never sent; unless the "
                          "guest ignored that input, the peers now differ.\n",
                       (unsigned)t, oslot, (unsigned)rb_snap_oldest_or0(d));
                rb_owed_clear_span(d, t, t);
                return;
            }
            /* A replay re-runs load..tip, and the seal mask holds 64 rows:
             * once the tick is that far behind the tip no episode can carry
             * it. Retrying only logged "span exceeds the 64-row seal mask"
             * every 30 ticks until the tick aged out of the history, and then
             * the owed mark vanished without a word ("still owed: 0" in the
             * drain line of a peer that had in fact forked). Say it once. */
            if (d->sim > t && d->sim - 1u - t >= RNET_RB_PEER_SEAL_MASK_BITS) {
                rb_log(d, "RB correction LOST tick=%u slot=%d — replaying it now "
                          "would re-run %u..%u, more than the %u-row seal mask "
                          "carries. This peer ran that tick on input its owner "
                          "never sent; unless the guest ignored that input, the "
                          "peers now differ.\n",
                       (unsigned)t, oslot, (unsigned)t, (unsigned)(d->sim - 1u),
                       (unsigned)RNET_RB_PEER_SEAL_MASK_BITS);
                rb_owed_clear_span(d, t, t);
                return;
            }
            rb_log(d, "RB correction retried tick=%u slot=%d — the episode "
                      "that should have applied it did not commit\n",
                   (unsigned)t, oslot);
            if (rb_begin_episode(d, t, oslot, 1, 0u, 0u, 0u, 0u))
                return;
            /* Refused (and said why). Back off rather than retry every poll. */
            d->owed_retry_after = d->sim + RB_COOLDOWN_TICKS;
            return;
        }
    }

    for (slot = 0; slot < slots; ++slot) {
        uint32_t t;
        if (slot == local)
            continue;
        for (t = from; t < d->sim; ++t) {
            RNetInputSample sample;
            RNetRbFrame published;
            RNetRbFrame wire;

            if (!rnet_ih_get(&d->ih, slot, t, &published))
                continue;
            if (!published.is_predicted)
                continue;
            if (!rnet_session_peek_remote_input(
                    s, slot, rnet_sched_wire_for_sim(t), &sample) ||
                !sample.valid)
                continue;

            rb_row_from_sample(d, slot, t, &sample, &wire);
            if (rb_rows_equal(&wire, &published)) {
                /* Prediction held: promote in place, no episode. */
                rnet_ih_promote(&d->ih, slot, &wire);
                continue;
            }

            {
                RNetInputContractFrame pub_c, wire_c;
                RNetInputContractDecision dec;
                rnet_ih_frame_to_contract(&published, &pub_c);
                rnet_ih_frame_to_contract(&wire, &wire_c);
                dec = rnet_rb_decide_stick_replace(d->rb, &pub_c, &wire_c,
                                                   1 /* completed sim */);
                rnet_ih_promote(&d->ih, slot, &wire);
                if (!rnet_input_contract_decision_is_rewind(dec))
                    continue;
            }

            rnet_sched_note_mispredict(d->sim > t ? d->sim - t : 0u);
            /* Owed until an episode that replays t commits. */
            rb_owed_mark(d, t, slot);
            if (d->stage == kRbIdle) {
                if (rb_cooldown_active(d))
                    /* Used to be refused in silence -- and with the row
                     * already promoted, lost for good. Now it waits. */
                    rb_log(d, "RB correction deferred tick=%u slot=%d — "
                              "cooldown until %u, re-opened after it\n",
                           (unsigned)t, slot, (unsigned)d->cooldown_until_tick);
                else if (d->defer_valid)
                    /* Owed (marked above): our own episode would race the
                     * peer's we are about to follow. Re-opened after it. */
                    rb_log(d, "RB correction deferred tick=%u slot=%d — "
                              "following epoch=%u first, re-opened after it\n",
                           (unsigned)t, slot, (unsigned)d->defer_epoch);
                else
                    rb_begin_episode(d, t, slot, 1, 0u, 0u, 0u, 0u);
            } else if (t >= d->corr.load_tick &&
                       t <= d->corr.target_tick) {
                /* Already covered. The open episode replays this tick, and
                 * reconcile only ever runs on REMOTE slots, whose sealed rows
                 * come from the peer's own authoritative export rather than
                 * from our history — so the replay uses the true value even
                 * though we did nothing here. Counting these as drops reported
                 * 42 findings that were all fine, which is worse than
                 * reporting none. */
            } else {
                /* Genuinely lost. rnet_ih_promote above already marked the row
                 * authoritative, so the next scan skips it: this was the only
                 * chance to act, the tick is outside every span we will
                 * replay, and we do not resimulate it. Tip-hold can still
                 * extend over it; every other stage cannot. */
                if (d->stage == kRbTipHold && rb_tip_extend(d, t, slot, 1))
                    return; /* resimulated after all — not a loss */
                d->drop_stage_n[d->stage]++;
                rb_log(d, "RB late wire UNAPPLIED stage=%s tick=%u "
                          "slot=%d sim=%u span=%u..%u pub=%04x wire=%04x (n=%u)\n",
                       rb_stage_name(d->stage), (unsigned)t, slot,
                       (unsigned)d->sim, (unsigned)d->corr.load_tick,
                       (unsigned)d->corr.target_tick,
                       (unsigned)published.buttons, (unsigned)wire.buttons,
                       (unsigned)d->drop_stage_n[d->stage]);
            }
            return; /* one correction at a time; the rest follow next tick */
        }
    }
}

/* ── coordinated stop ────────────────────────────────────────────────── */

/* How often an unanswered marker is re-sent, and how long a peer that holds
 * every other peer's marker waits to hear that they hold ITS marker too before
 * it leaves anyway. The last message of any exchange can be lost, so the wait
 * is bounded; by then nothing either side opened is outstanding, so leaving
 * early can only cost the peer its clean exit, never the ledger. */
#define RB_QUIESCE_RESEND_MS 50u
#define RB_QUIESCE_ACK_GRACE_MS 500u

static uint32_t rb_owed_outstanding(const RNetRbDriver *d)
{
    uint32_t n = 0u, t;
    uint32_t from = d->sim > RNET_INPUT_HIST_DEPTH ? d->sim - RNET_INPUT_HIST_DEPTH : 0u;
    for (t = from; t < d->sim; ++t) {
        uint32_t i = t % RNET_INPUT_HIST_DEPTH;
        if (d->owed_slot[i] >= 0 && d->owed_tick[i] == t)
            n++;
    }
    return n;
}

static void rb_quiesce_send(RNetRbDriver *d)
{
    RNetSession *s = rb_session(d);
    uint32_t expect = rb_expect_mask(d);
    uint32_t have_all = (d->peer_quiesce_mask & expect) == expect ? 1u : 0u;
    if (s)
        rnet_session_send_rb_sync(s, 0u, have_all, d->sim, 0u,
                                  (rnet_u8)rb_local_slot(d),
                                  RNET_RB_SYNC_OP_QUIESCE, 0u);
    d->quiesce_sent_ms = rb_now(d);
    if (d->quiesce_sent_ms == 0u)
        d->quiesce_sent_ms = 1u;
}

/* Pumped from poll_admit. wire_ok = 0 when the session is not running: the
 * timeout still counts and a drain that already holds every marker can still
 * finish, but nothing is sent. */
static void rb_quiesce_pump(RNetRbDriver *d, int wire_ok)
{
    uint32_t now, expect;

    if (d->quiesce != RNET_RB_QUIESCE_DRAINING)
        return;
    now = rb_now(d);
    expect = rb_expect_mask(d);
    if ((uint32_t)(now - d->quiesce_req_ms) > RNET_RB_QUIESCE_TIMEOUT_MS) {
        d->quiesce = RNET_RB_QUIESCE_TIMED_OUT;
        rb_log(d, "RB quiesce TIMED OUT after %u ms at sim=%u — stage=%s, "
                  "peer markers held %x of %x, acknowledged %x. The episode "
                  "ledger is NOT guaranteed to balance: a peer vanished, "
                  "predates the marker, or an episode never resolved.\n",
               (unsigned)(now - d->quiesce_req_ms), (unsigned)d->sim,
               rb_stage_name(d->stage), (unsigned)(d->peer_quiesce_mask & expect),
               (unsigned)expect, (unsigned)(d->peer_quiesce_ack_mask & expect));
        return;
    }
    /* In flight: let it finish. The marker promises "none open here", so it
     * is only ever sent from idle -- and a BEGIN held for its load tick is
     * open here too. */
    if (d->stage != kRbIdle || d->defer_valid)
        return;
    if (wire_ok && (d->quiesce_sent_ms == 0u ||
                    (uint32_t)(now - d->quiesce_sent_ms) >= RB_QUIESCE_RESEND_MS))
        rb_quiesce_send(d);
    if ((d->peer_quiesce_mask & expect) != expect)
        return;
    if (d->quiesce_all_ms == 0u)
        d->quiesce_all_ms = now ? now : 1u;
    /* wire_ok == 0: the session is down or the peer said BYE. It has left,
     * and a draining peer leaves only once it holds our marker, so there is
     * no acknowledgement left to wait for. */
    if (wire_ok && (d->peer_quiesce_ack_mask & expect) != expect &&
        (uint32_t)(now - d->quiesce_all_ms) < RB_QUIESCE_ACK_GRACE_MS)
        return;
    if (wire_ok)
        rb_quiesce_send(d);   /* the last word: we hold theirs */
    d->quiesce = RNET_RB_QUIESCE_DRAINED;
    rb_log(d, "RB quiesced at sim=%u after %u ms (requested at sim=%u: %s) — "
              "no episode open here and every peer has promised to open none. "
              "Opened here: %u as initiator, %u as follower; corrections not "
              "opened while draining: %u; still owed: %u; peers acknowledged "
              "%x of %x; follows that waited for their load tick: %u, that took "
              "it at our tip: %u; POSTs re-sent: %u, BEGINs re-sent: %u\n",
           (unsigned)d->sim, (unsigned)(now - d->quiesce_req_ms),
           (unsigned)d->quiesce_req_sim,
           d->quiesce_origin ? d->quiesce_origin : "host request",
           (unsigned)d->ep_initiated, (unsigned)d->ep_followed,
           (unsigned)d->drain_unopened, (unsigned)rb_owed_outstanding(d),
           (unsigned)(d->peer_quiesce_ack_mask & expect), (unsigned)expect,
           (unsigned)d->n_follow_deferred, (unsigned)d->n_follow_at_tip,
           (unsigned)d->n_post_resent, (unsigned)d->n_begin_resent);
}

void rnet_rb_driver_request_quiesce(RNetRbDriver *d)
{
    if (!d || !d->started || d->quiesce != RNET_RB_QUIESCE_NONE)
        return;
    d->quiesce = RNET_RB_QUIESCE_DRAINING;
    d->quiesce_req_ms = rb_now(d);
    d->quiesce_req_sim = d->sim;
    /* An armed injection would add one more mispredict that the drain then
     * refuses to open; the injector perturbs a running match, not its end. */
    d->force_invent_slot = -1;
    rb_log(d, "RB quiesce requested at sim=%u (%s) — stage=%s; no new episode "
              "or local tip-extend opens from here, open ones finish, peer "
              "BEGINs are still followed\n",
           (unsigned)d->sim, d->quiesce_origin ? d->quiesce_origin : "host request",
           rb_stage_name(d->stage));
}

RNetRbQuiesce rnet_rb_driver_quiesce_state(const RNetRbDriver *d)
{
    return d ? d->quiesce : RNET_RB_QUIESCE_NONE;
}

/* ── live admit ──────────────────────────────────────────────────────── */

/*
 * The scheduler reads one remote tip from the session's stats, and the stats
 * report the HIGHEST over every remote seat. With two seats that is the one
 * peer's tip. With more, a peer that stops sending (a 2 s watchdog wait, a
 * long replay) hides behind the others: the prediction cap is measured
 * against a tip that keeps moving, never trips, and the rest run on invented
 * rows for that seat indefinitely -- measured in rb_driver_test's 4-seat
 * cell: three peers ran 1,000 ticks ahead of a fourth stuck at 221, and it
 * never caught up. So with more than one peer, pacing reads the SLOWEST
 * peer's tip, and the invent decision for a seat reads that seat's own.
 * With one peer neither function changes anything.
 */
static void rb_stats_slowest_tip(RNetRbDriver *d, RNetSession *s, RNetSessionStats *st)
{
    uint32_t m = rb_expect_mask(d);
    rnet_u32 tip, lo = 0u;
    int i, any = 0;
    if (rb_popcount(m) < 2)
        return;
    for (i = 0; i < RB_MAX_SLOTS; ++i) {
        if (!(m & (1u << i)) || !rnet_session_remote_tip(s, i, &tip))
            continue;
        if (!any || tip < lo)
            lo = tip;
        any = 1;
    }
    if (!any)
        return;
    st->highest_remote_wire = lo;
    st->remote_lead = (int)lo - (int)st->sim_tick;
}

static void rb_stats_seat_tip(RNetRbDriver *d, RNetSession *s, int slot, RNetSessionStats *st)
{
    rnet_u32 tip;
    if (rb_popcount(rb_expect_mask(d)) < 2 || !rnet_session_remote_tip(s, slot, &tip))
        return;
    st->highest_remote_wire = tip;
    st->remote_lead = (int)tip - (int)st->sim_tick;
}

/* INCREMENTAL: hand the host the next replayed tick. */
static RNetRbAdmit rb_replay_step(RNetRbDriver *d)
{
    uint32_t t = d->replay_next;

    d->stall_tag = "replay";
    /* Re-key the store onto the replayed timeline as we go. */
    rb_snap_take(d, t);
    if (!rb_load_sealed_rows(d, t)) {
        rb_resim_end(d);
        rb_replay_missing_abort(d);
        return RNET_RB_ADMIT_STALL;
    }
    rb_publish(d, t, 1);
    d->pending_admit = RNET_RB_ADMIT_REPLAY;
    return RNET_RB_ADMIT_REPLAY;
}

static void rb_check_chain_fork(RNetRbDriver *d);

RNetRbAdmit rnet_rb_driver_poll_admit(RNetRbDriver *d)
{
    RNetSession *s;
    RNetSessionStats st;
    uint32_t wire;
    int slots;
    int local;
    int any_invent = 0;
    int slot;

    if (!d || !d->started)
        return RNET_RB_ADMIT_STALL;
    s = rb_session(d);
    if (!s)
        return RNET_RB_ADMIT_STALL;
    /* A replay in progress owns the sim, exactly as the INLINE loop does:
     * no wire is drained until it finishes, so both shapes see the same
     * messages at the same point in the episode. */
    if (d->stage == kRbReplaying)
        return rb_replay_step(d);
    if (!rnet_session_is_running(s)) {
        /* A draining peer that has left ends our session (its BYE), and its
         * last marker may be sitting in the queue behind it. Read what is
         * there -- a peer cannot open anything after its marker, so nothing
         * taken here starts new work -- and let the drain finish or time out.
         * Measured before this: the survivor of a drain never pumped again
         * once the session dropped, and left through the 1.5 s peer-gone path
         * with no "RB quiesced" line. */
        if (d->quiesce == RNET_RB_QUIESCE_DRAINING)
            rb_drain_wire(d);
        rb_quiesce_pump(d, 0);
        return RNET_RB_ADMIT_STALL;
    }
    slots = rb_slot_count(d);
    local = rb_local_slot(d);

    rb_drain_wire(d);
    rb_request_missing_frame_commits(d);
    rb_defer_pump(d);
    /* Before anything else: the gate below stops the frame from happening
     * while this is outstanding, so pumping it from finish_frame deadlocked
     * -- no frame, no pump, no handshake, forever. A precondition has to be
     * driven by something that runs whether or not the thing it gates runs. */
    rb_modset_pump(d);
    /* Identity too, for the same reason: it is sent from finish_frame, and a
     * peer held at tick 0-1 (boot-digest wait, or already refused on OUR
     * identity) runs no frame -- so it never answered, and the other side,
     * never seeing its identity, could not refuse on the same evidence. */
    rb_send_identity(d);
    rb_reconcile_wire(d);
    rb_pump_episode(d);
    /* A peer that said BYE has left even while the session still reads
     * RUNNING; a drain must not wait for its acknowledgement. With more than
     * one peer a BYE names only the one that left (the session keeps a single
     * flag): the rest are still there, and still need our marker. */
    rb_quiesce_pump(d, (rb_popcount(rb_expect_mask(d)) == 1 &&
                        rnet_session_peer_disconnected(s, 0)) ? 0 : 1);
    /* The hash-chain watermark can stop all frame admission. Its recovery
     * timer must advance from the poll path, including while no frame runs. */
    rb_check_chain_fork(d);

    if (d->stage == kRbReplaying)
        return rb_replay_step(d);   /* INCREMENTAL: the episode just loaded */
    /* Seal / Verify own the sim; Live must not advance under them. */
    if (d->stage == kRbSealing || d->stage == kRbVerifying) {
        d->stall_tag = "episode";
        return RNET_RB_ADMIT_STALL;
    }
    /* A refused match plays no further tick (rb_refuse). Checked after the
     * wire and the episode pump, so an episode already open still finishes
     * and a drain still completes; only Live is closed. */
    if (d->refusal) {
        d->stall_tag = "refused";
        return RNET_RB_ADMIT_STALL;
    }

    /* A session hold stops Live before the local tip is sealed: a pad held
     * while every peer is paused must not become input for any tick. The
     * episode and wire pumps above keep running, so a late row before the
     * hold is still reconciled and replayed while held. */
    if (d->hold_active && d->sim >= d->hold_at) {
        if (!d->hold_logged) {
            rb_log(d, "RB hold reached at %u\n", (unsigned)d->sim);
            d->hold_logged = 1;
        }
        d->stall_tag = "session_hold";
        return RNET_RB_ADMIT_STALL;
    }

    memset(&st, 0, sizeof(st));
    rnet_session_get_stats(s, &st);
    rb_stats_slowest_tip(d, s, &st);
    rnet_sched_sync_delay_from_session();

    /*
     * Seal the local tip for this sim tick exactly ONCE.
     *
     * prepare_local_tip samples the staged pad and stores it at sim+D, then
     * emits it. Calling it again on a later stalled attempt at the same sim
     * tick would overwrite a wire tick the peer may already have consumed —
     * the two peers would then simulate different local input for that tick,
     * with no mismatch anywhere to detect it.
     */
    if (!d->tip_prepared_valid || d->tip_prepared_for != d->sim) {
        rnet_session_prepare_local_tip(s, d->sim);
        d->tip_prepared_for = d->sim;
        d->tip_prepared_valid = 1;
    }

    wire = rnet_sched_wire_for_sim(d->sim);
    if (rnet_sched_pre_admit(d->sim, wire, &st)) {
        d->stall_tag = rnet_sched_admit_stall_tag();
        return RNET_RB_ADMIT_STALL;
    }

    for (slot = 0; slot < slots; ++slot) {
        RNetInputSample sample;
        RNetRbFrame row;

        memset(&sample, 0, sizeof(sample));
        if (slot == local) {
            /*
             * Our own seat simulates the row we PUBLISHED for this wire tick
             * — sampled D ticks ago — not the pad currently being held. The
             * live pad belongs to tick sim+D and the peer will not see it
             * until then; simulating it here would fork the two peers on
             * local input, which is the one thing rollback cannot correct
             * because neither side ever reports a mismatch.
             *
             * Before the delay pipeline has filled (sim < D) there is no
             * published row yet and neutral is the agreed value, matching the
             * neutral priming delay-sync does at session start.
             */
            if (rnet_session_peek_input(s, slot, wire, &sample) && sample.valid) {
                rb_row_from_sample(d, slot, d->sim, &sample, &row);
                if (d->host.admit_sample)
                    d->host.admit_sample(d->host.ctx, slot, d->sim, &sample);
            } else {
                rb_row_neutral(d, slot, d->sim, &row);
            }
            rnet_ih_put(&d->ih, slot, &row);
            d->rows[slot] = row;
            continue;
        }

        /* Validation: withhold an arrived row so the scheduler must invent.
         * peek does not consume, so the true row is still there for the
         * reconcile pass, which then finds the mismatch and must rewind --
         * a REAL late arrival, not a corrupted one. Never before the peers
         * have agreed on tick 0: at a dense interval the injector otherwise
         * corrupts the boot tick itself, and everything measured afterwards
         * is of a session that was never valid. A validation knob must
         * perturb the thing under test, not the premise of the test. */
        if (d->force_mispredict_every > 0 && slot != local && d->boot_dig_settled &&
            d->quiesce == RNET_RB_QUIESCE_NONE &&
            d->force_mispredict_row[slot] != d->sim + 1u) {
            /* Once per row: a stalled poll retries the same tick. */
            d->force_mispredict_row[slot] = d->sim + 1u;
            if ((++d->force_mispredict_n % (unsigned long)d->force_mispredict_every) == 0ul) {
                d->force_invent_slot = slot;
                rb_log_raw(d, "rbe: forced late row slot=%d sim=%u "
                              "(invent + corrupt)\n",
                           slot, (unsigned)d->sim);
            }
        }
        /* An armed injection waits for an invent, and lockstep forbids one: the
         * seat would then skip its real row and stall on every poll, forever.
         * The knob must perturb the thing under test, never wedge it. */
        if (d->force_invent_slot == slot && rb_gate_lockstep_no_invent(d)) {
            rb_log_raw(d, "rbe: injected mispredict cancelled slot=%d sim=%u "
                          "— lockstep forbids invent\n", slot, (unsigned)d->sim);
            d->force_invent_slot = -1;
        }
        if (d->force_invent_slot != slot &&
            rnet_session_peek_remote_input(s, slot, wire, &sample) &&
            sample.valid) {
            rb_row_from_sample(d, slot, d->sim, &sample, &row);
            if (d->host.admit_sample)
                d->host.admit_sample(d->host.ctx, slot, d->sim, &sample);
            rnet_ih_put(&d->ih, slot, &row);
            d->rows[slot] = row;
            rnet_sched_note_remote_hit();
            continue;
        }

        {
            const char *why = NULL;
            RNetSessionStats st_seat = st;
            rb_stats_seat_tip(d, s, slot, &st_seat);
            if (rnet_sched_on_remote_miss(slot, d->sim, wire, &st_seat,
                                          d->prediction_cap, &why)) {
                /* Same wedge as lockstep above: outside a grace wait (which
                 * times out into an invent) the scheduler is waiting for a
                 * REAL row -- cushion_rebuild, boot_tip_wait -- and that is
                 * the row the injector is hiding. Release it; the retry
                 * admits the real row. */
                size_t wl = why ? strlen(why) : 0;
                if (d->force_invent_slot == slot &&
                    !(wl > 6 && !strcmp(why + wl - 6, "_grace"))) {
                    rb_log_raw(d, "rbe: injected mispredict cancelled slot=%d "
                                  "sim=%u — scheduler stalled (%s)\n", slot,
                               (unsigned)d->sim, why ? why : "remote_miss");
                    d->force_invent_slot = -1;
                }
                d->stall_tag = why ? why : "remote_miss";
                return RNET_RB_ADMIT_STALL;
            }
            if (!rnet_ih_invent_hold_last(&d->ih, slot, d->sim, &row))
                return RNET_RB_ADMIT_STALL;
            if (d->force_invent_slot == slot) {
                /* Guarantee the invention is WRONG: hold-last would otherwise
                 * match a peer sitting on the same buttons, and a correct
                 * prediction exercises nothing. */
                row.buttons ^= d->cfg.inject_flip_bits ? d->cfg.inject_flip_bits
                                                       : (uint16_t)0x0040u;
                d->force_invent_slot = -1;
            }
            rb_row_sanitize(d, slot, &row);
            /* Sanitise wrote through a copy; store the corrected row so a
             * foreign neutral can never reach the sim or a seal. */
            rnet_ih_put(&d->ih, slot, &row);
            d->rows[slot] = row;
            any_invent = 1;
        }
    }

    rnet_sched_post_admit(any_invent);
    rnet_sched_clear_admit_stall();
    d->stall_tag = NULL;
    rb_snap_take(d, d->sim); /* state before this tick — see rb_snap_take */
    rb_publish(d, d->sim, 0);
    d->pending_admit = RNET_RB_ADMIT_LIVE;
    return RNET_RB_ADMIT_LIVE;
}

/*
 * The hash chain compares a per-tick state digest against the peer's.
 *
 * A live mismatch stops the watermark advancing -- try_advance halts at the
 * first tick where the digests differ -- and resolved_through bounds the
 * reconcile scan and feeds peer convergence. So a genuine state fork degraded
 * rollback QUIETLY. This reports it.
 *
 * Checked only while idle, and only after the mismatch PERSISTS: reporting on
 * sight gave one to three "forks" per healthy SNES run, and the tell was the
 * reported tick moving backwards -- 118, then 51, then 87. Those were
 * in-flight FRAME_COMMITs describing the peer's pre-correction timeline. A
 * transient resolves within a round trip; a genuine fork never can.
 *
 * Two-seat sessions keep this advisory: an episode that aborts can leave
 * local digests describing a timeline the peer did not run. Multi-seat
 * sessions retry a persistent fork through a sealed-row episode. Two traces
 * that previously produced false reports were removed at the source:
 *
 *   - an episode that aborted AFTER its replay (a peer NACK that arrived
 *     while we verified) left the live, mispredicted digests of the replayed
 *     ticks in the chain; every stall in n64lle's 0 ms sweep sat on one. A
 *     completed replay now writes its own digests into the chain and sends
 *     them to the peers (rb_replay_finish);
 *   - a mismatch at or after a correction we still OWE is our timeline being
 *     known-wrong until the retry replays it, not an unexplained fork, so it
 *     is not reported while the correction is owed. If the correction is
 *     LOST, the owed mark goes with a line saying so, and a mismatch that
 *     then persists is reported.
 *
 * Per peer: each chain stalls on its own, and a report names the seat when
 * there is more than one.
 */
static void rb_check_chain_fork(RNetRbDriver *d)
{
    uint32_t tick = 0u, local = 0u, peer = 0u;
    uint32_t now, settle, expect, owed_t = 0u;
    int owed_slot = -1, owed, i;

    if (d->stage != kRbIdle || !d->rb)
        return;
    expect = rb_expect_mask(d);
    owed = rb_owed_first(d, &owed_t, &owed_slot);
    now = rb_now(d);
    /* Two trips plus slack: one for the peer's correction to reach us, one for
     * anything it had already sent to drain. */
    settle = (d->rtt_ema_ms * 2u) + 250u;
    if (settle < 300u)
        settle = 300u;
    for (i = 0; i < RB_MAX_SLOTS; ++i) {
        if (!(expect & (1u << i)))
            continue;
        if (!rnet_hc_peek_mismatch(&d->hc[i], &tick, &local, &peer)) {
            d->chain_pending_tick[i] = 0u; /* cleared on its own: it was in flight */
            continue;
        }
        if (owed && owed_t <= tick) {
            d->chain_pending_tick[i] = 0u; /* explained: a correction is owed */
            continue;
        }
        if (d->chain_pending_tick[i] != tick) {
            d->chain_pending_tick[i] = tick;
            d->chain_pending_ms[i] = now;
            continue;
        }
        if ((uint32_t)(now - d->chain_pending_ms[i]) < settle)
            continue;
        if (rb_popcount(expect) == 1 && d->chain_fork_tick[i] == tick)
            continue; /* already reported; the watermark is stuck here */

        if (rb_popcount(expect) > 1) {
            /* Every seat may hold every input row and still disagree about
             * the state it produced. In an N-seat room this can follow a
             * declined tip extension or a correction that was not replayed
             * everywhere. The chain has identified the first bad tick and
             * the last agreed snapshot is still in reach: open a normal
             * sealed-row episode for it. Owed bookkeeping retries if this
             * first exchange aborts or cooldown delays it. */
            if (rb_begin_episode(d, tick, i, 1, 0u, 0u, 0u, 0u)) {
                rb_log(d, "RB chain repair tick=%u local=%08x peer=%08x seat=%d "
                          "after %ums\n", (unsigned)tick, (unsigned)local,
                       (unsigned)peer, i, (unsigned)(now - d->chain_pending_ms[i]));
                rb_owed_mark(d, tick, i);
                d->chain_pending_tick[i] = 0u;
                return;
            }
            d->chain_pending_ms[i] = now; /* cooldown or drain: retry later */
        } else {
            d->chain_fork_tick[i] = tick;
            rb_log(d, "RB chain stall tick=%u local=%08x peer=%08x "
                      "— unresolved for %ums (settle %ums). The confirmed watermark "
                      "cannot advance past this tick. ADVISORY: not acted on.\n",
                   (unsigned)tick, (unsigned)local, (unsigned)peer,
                   (unsigned)(now - d->chain_pending_ms[i]), (unsigned)settle);
        }
    }
}

void rnet_rb_driver_finish_frame(RNetRbDriver *d)
{
    RNetSession *s;
    uint32_t master;
    int admitted;

    if (!d || !d->started)
        return;
    admitted = d->pending_admit;
    d->pending_admit = RNET_RB_ADMIT_STALL;

    if (admitted == RNET_RB_ADMIT_REPLAY) {
        /* INCREMENTAL: the host ran replayed tick replay_next. Its digest is
         * kept, and every replayed tick's goes into the chain (and out as a
         * FRAME_COMMIT) together when the replay completes -- exactly as the
         * inline loop does (rb_replay_note, rb_replay_finish). */
        if (d->stage != kRbReplaying)
            return;
        rb_replay_note(d, d->replay_next);
        d->resim_ticks++;
        d->replay_next++;
        if (d->replay_next > d->corr.target_tick) {
            rb_replay_finish(d);
            rb_enter_verify(d);
        }
        return;
    }
    if (d->in_resim)
        return;

    s = rb_session(d);
    master = d->host.digest_master(d->host.ctx);
    /* Validation (FORCE_BOOT_FORK=1): publish a wrong digest for tick 0 only,
     * so the two peers genuinely disagree about the state they booted from.
     * This is what proves rb_boot_digest_gate can fail — an agreement check
     * that has only ever agreed has not been tested. Never set in a match. */
    if (d->sim == 0u && d->force_boot_fork > 0) {
        master ^= 0xa5a5a5a5u;
        rb_log_raw(d, "rbe: forced boot fork — publishing %08x for tick 0\n",
                   (unsigned)master);
    }
    rb_hc_note_local(d, d->sim, master);
    if (s)
        rnet_session_send_rb_frame_commit(s, d->sim, master);

    d->sim++;
    if (s)
        rnet_session_set_sim_tick(s, d->sim);
    /* An owed correction leaving the history window is lost, and says so
     * (rb_owed_first only looks inside the window). */
    if (d->sim > RNET_INPUT_HIST_DEPTH + 1u) {
        uint32_t t = d->sim - RNET_INPUT_HIST_DEPTH - 1u;
        uint32_t i = t % RNET_INPUT_HIST_DEPTH;
        if (d->owed_slot[i] >= 0 && d->owed_tick[i] == t) {
            rb_log(d, "RB correction LOST tick=%u slot=%d — it aged out of the "
                      "input history before any episode could replay it. This "
                      "peer ran that tick on input its owner never sent; unless "
                      "the guest ignored that input, the peers now differ.\n",
                   (unsigned)t, (int)d->owed_slot[i]);
            d->owed_slot[i] = -1;
        }
    }
    rb_lockstep_tick(d);
    rb_send_identity(d);

    /* A stuck watermark whose next tick aged out of the ring is not a fork. */
    rb_hc_heal(d);
    /* ...but one that is still stuck after healing may well be. */
    rb_check_chain_fork(d);
    if (d->rb)
        rnet_rb_set_peer_convergence(d->rb, rb_hc_resolved(d));
}

/* ── diagnostics ─────────────────────────────────────────────────────── */

uint32_t rnet_rb_driver_sim_tick(const RNetRbDriver *d) { return d ? d->sim : 0u; }
uint32_t rnet_rb_driver_episode_count(const RNetRbDriver *d) { return d ? d->episode_count : 0u; }
uint32_t rnet_rb_driver_invent_count(const RNetRbDriver *d) { return d ? d->ih.invent_count : 0u; }
uint32_t rnet_rb_driver_promote_count(const RNetRbDriver *d) { return d ? d->ih.promote_count : 0u; }
uint64_t rnet_rb_driver_resim_ticks(const RNetRbDriver *d) { return d ? d->resim_ticks : 0u; }
uint32_t rnet_rb_driver_desync_count(const RNetRbDriver *d) { return d ? d->desync_count : 0u; }
uint32_t rnet_rb_driver_rtt_estimate_ms(const RNetRbDriver *d) { return d ? d->rtt_ema_ms : 0u; }

void rnet_rb_driver_set_hold(RNetRbDriver *d, uint32_t tick)
{
    if (!d)
        return;
    d->hold_active = 1;
    d->hold_at = tick;
    d->hold_logged = 0;
    rb_log(d, "RB hold set at %u (sim %u)\n", (unsigned)tick, (unsigned)d->sim);
}

void rnet_rb_driver_clear_hold(RNetRbDriver *d)
{
    if (!d || !d->hold_active)
        return;
    d->hold_active = 0;
    rb_log(d, "RB hold released at %u\n", (unsigned)d->sim);
}

int rnet_rb_driver_hold_settled(const RNetRbDriver *d)
{
    int slots, local, slot;
    uint32_t t, from;
    if (!d || !d->started || !d->hold_active || d->sim < d->hold_at)
        return 0;
    if (d->stage != kRbIdle)
        return 0;
    /* Every remote row the sim consumed before the hold must be real. A
     * row predicted before the hold was set stays predicted until its wire
     * arrives; a mismatch then opens an episode, which the check above waits
     * out. Rows older than the history window are already settled. */
    slots = rb_slot_count(d);
    local = rb_local_slot(d);
    from = d->sim > RNET_INPUT_HIST_DEPTH ? d->sim - RNET_INPUT_HIST_DEPTH : 0u;
    for (slot = 0; slot < slots; ++slot) {
        if (slot == local || !(rb_expect_mask(d) & (1u << slot)))
            continue;
        for (t = from; t < d->sim; ++t) {
            RNetRbFrame row;
            if (rnet_ih_get(&d->ih, slot, t, &row) && row.is_valid && row.is_predicted)
                return 0;
        }
    }
    return 1;
}

uint32_t rnet_rb_driver_confirmed_through(const RNetRbDriver *d)
{
    return (d && d->rb) ? rnet_rb_resolved_through(d->rb) : 0u;
}

uint32_t rnet_rb_driver_confirmed_remaining(const RNetRbDriver *d)
{
    uint32_t through = rnet_rb_driver_confirmed_through(d);
    return (d && d->sim > through) ? (d->sim - through) : 0u;
}

int rnet_rb_driver_episode_active(const RNetRbDriver *d)
{
    return d && d->stage != kRbIdle;
}

void rnet_rb_driver_debug_state(const RNetRbDriver *d, char *out, size_t cap)
{
    uint32_t mask;
    int i;
    if (!out || cap == 0u)
        return;
    if (!d) {
        snprintf(out, cap, "no driver");
        return;
    }
    snprintf(out, cap,
             "stage=%s epoch=%u span=%u..%u sim=%u age_ms=%u seal=%d "
             "answered=%x posts=%x commits=%x expect=%x deferred=%d",
             rb_stage_name(d->stage), (unsigned)d->corr.epoch_id,
             (unsigned)d->corr.load_tick, (unsigned)d->corr.target_tick,
             (unsigned)d->sim, (unsigned)(rb_now((RNetRbDriver *)d) - d->stage_entered_ms),
             d->rb ? rnet_rb_all_peer_seal_rows_complete(d->rb) : 0,
             (unsigned)d->answered_mask, (unsigned)d->peer_post_mask,
             (unsigned)d->peer_commit_mask, (unsigned)rb_expect_mask(d), d->defer_valid);
    mask = rb_expect_mask(d);
    for (i = 0; i < RB_MAX_SLOTS; ++i) {
        uint32_t through, next;
        size_t used;
        if (!(mask & (1u << i)))
            continue;
        through = rnet_hc_resolved_through(&d->hc[i]);
        next = through + 1u;
        used = strlen(out);
        if (used < cap)
            snprintf(out + used, cap - used, " hc%d=%u:%u%u", i,
                     (unsigned)through,
                     (unsigned)rnet_hc_local_digest(&d->hc[i], next, NULL),
                     (unsigned)rnet_hc_peer_digest(&d->hc[i], next, NULL));
    }
}

int rnet_rb_driver_in_resim(const RNetRbDriver *d)
{
    return d && d->in_resim;
}

const char *rnet_rb_driver_stall_tag(const RNetRbDriver *d)
{
    return d ? d->stall_tag : NULL;
}

const char *rnet_rb_driver_refusal(const RNetRbDriver *d)
{
    return d ? d->refusal : NULL;
}

int rnet_rb_driver_last_fork(const RNetRbDriver *d, uint32_t *tick, const char **partition)
{
    if (!d || !d->fork_seen)
        return 0;
    if (tick)
        *tick = d->fork_tick;
    if (partition)
        *partition = d->fork_partition ? d->fork_partition : "?";
    return 1;
}

int rnet_rb_driver_fork_digests(const RNetRbDriver *d, uint32_t *mine, uint32_t *theirs)
{
    if (!d || !d->fork_seen)
        return 0;
    if (mine)
        *mine = d->fork_mine;
    if (theirs)
        *theirs = d->fork_theirs;
    return 1;
}
