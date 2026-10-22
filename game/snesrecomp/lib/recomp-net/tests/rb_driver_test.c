/*
 * rb_driver_test -- the rollback episode driver against itself.
 *
 * Two parts.
 *
 * In-process: the driver's contracts that need no peer -- epoch encoding, and
 * start() refusing (and saying why) when the host leaves a hole.
 *
 * Two processes: fork() a pair, each with its own RNetSession over UDP
 * loopback, its own driver, and a toy deterministic engine. Two processes,
 * not one, because the admission scheduler is process-global -- and because
 * the doctrine is "two processes, never one" for anything that decides whether
 * peers agree. The N-seat scenarios fork three or four, seat 0 relaying for
 * the rest (rnet_session_start_lan_hub), and grade the ledger per initiator /
 * follower pair by epoch. The toy engine folds every seat's row into an accumulator, so
 * a replay that used a wrong row changes every later digest. Each child
 * records the digest it ended up with for every tick; the parent compares the
 * two timelines independently of the hash chain the driver itself uses.
 *
 * Scenarios cover both replay shapes and a MIXED pair (one peer inline, one
 * incremental), which is the claim that the shapes are behaviourally
 * identical on the wire. The link simulator puts real latency under the
 * incremental cases so tip-hold is reachable.
 *
 * This is the FSM's unit test, not the verdict: the verdict is an engine's
 * two-process harness over its real game (snesrecomp tools/rb_sweep.sh).
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* clock_gettime, nanosleep, setenv under -std=c11 */
#endif

#include "recomp_net/rb_driver.h"
#include "recomp_net/recomp_net.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
int main(void)
{
    printf("rb_driver_test: two-process part needs fork(); in-process part only on POSIX\n");
    return 0;
}
#else

#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_failures;

static void expect_true(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_failures++;
    }
}

static uint32_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void sleep_ms(unsigned ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000u;
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

/* ── toy engine ──────────────────────────────────────────────────────── */

#define TOY_SNAPS 128u
#define TOY_TICKS 4096u

typedef struct Toy {
    uint32_t tick;   /* next tick to run */
    uint32_t acc;
} Toy;

typedef struct Child {
    int slot;
    int mode;
    Toy state;
    struct { uint32_t tick; int valid; Toy s; } snaps[TOY_SNAPS];
    RNetRbFrame pub[RNET_RB_MAX_SLOTS];
    int pub_slots;
    uint32_t timeline[TOY_TICKS];   /* digest after tick t ran, last writer wins */
    FILE *log;
    /* ledger, counted from the driver's own lines */
    uint32_t n_ep_init, n_ep_follow, n_refused, n_abort, n_fork, n_timeout,
             n_tiphold_extend, n_unapplied, n_lobby, n_forced;
    uint32_t n_quiesced, n_quiesce_timeout, n_drain_unopened, n_tiphold_end;
    uint32_t replay_admits;
    uint32_t n_refusal_lines;   /* "RB match refused" */
    uint32_t n_deferred, n_at_tip, n_chain_stall;
    /* Epochs, for the per-pair ledger: opened here, followed, refused. */
#define MAX_EPOCHS 512
    uint32_t ep_init[MAX_EPOCHS], ep_follow[MAX_EPOCHS], ep_refused[MAX_EPOCHS];
    uint32_t n_ep_init_ids, n_ep_follow_ids, n_ep_refused_ids;
} Child;

static Child g_c;

static uint32_t fold(uint32_t h, uint32_t v)
{
    h ^= v;
    h *= 16777619u;
    return h;
}

static uint16_t toy_pad(int slot, uint32_t tick)
{
    /* Changes every few ticks and differs by seat, so hold-last predictions
     * are regularly wrong under latency: organic mispredicts, not only the
     * injected ones. */
    uint16_t b = (uint16_t)((slot + 1) << 8);
    if (((tick / 5u) + (uint32_t)slot * 3u) % 7u == 0u) b |= 0x40u;
    if ((tick / 11u) & 1u) b |= 0x80u;
    return (uint16_t)(b & 0x0FFFu);
}

static uint32_t toy_digest(const Toy *t)
{
    return fold(fold(2166136261u, t->acc), t->tick);
}

static void toy_step(void)
{
    int i;
    uint32_t tick = g_c.state.tick;
    uint32_t acc = fold(g_c.state.acc, tick);
    for (i = 0; i < g_c.pub_slots; ++i)
        acc = fold(acc, (uint32_t)g_c.pub[i].buttons | ((uint32_t)i << 16));
    g_c.state.acc = acc;
    g_c.state.tick = tick + 1u;
    if (tick < TOY_TICKS)
        g_c.timeline[tick] = toy_digest(&g_c.state);
}

static int h_snap_save(void *ctx, uint32_t tick)
{
    unsigned i = tick % TOY_SNAPS;
    (void)ctx;
    g_c.snaps[i].tick = tick;
    g_c.snaps[i].valid = 1;
    g_c.snaps[i].s = g_c.state;
    return 1;
}

static int h_snap_load(void *ctx, uint32_t tick)
{
    unsigned i = tick % TOY_SNAPS;
    (void)ctx;
    if (!g_c.snaps[i].valid || g_c.snaps[i].tick != tick)
        return 0;
    g_c.state = g_c.snaps[i].s;
    return 1;
}

static int h_snap_has(void *ctx, uint32_t tick)
{
    unsigned i = tick % TOY_SNAPS;
    (void)ctx;
    return g_c.snaps[i].valid && g_c.snaps[i].tick == tick;
}

static int h_snap_oldest(void *ctx, uint32_t *oldest)
{
    unsigned i;
    int any = 0;
    uint32_t best = 0;
    (void)ctx;
    for (i = 0; i < TOY_SNAPS; ++i) {
        if (g_c.snaps[i].valid && (!any || g_c.snaps[i].tick < best)) {
            best = g_c.snaps[i].tick;
            any = 1;
        }
    }
    if (any)
        *oldest = best;
    return any;
}

static void h_snap_drop_after(void *ctx, uint32_t tick)
{
    unsigned i;
    (void)ctx;
    for (i = 0; i < TOY_SNAPS; ++i)
        if (g_c.snaps[i].valid && g_c.snaps[i].tick > tick)
            g_c.snaps[i].valid = 0;
}

static void h_publish(void *ctx, uint32_t tick, const RNetRbFrame *rows, int slots, int replay)
{
    (void)ctx;
    (void)replay;
    if (g_c.state.tick != tick) {
        fprintf(g_c.log, "TEST: publish for tick %u while the engine is at %u\n",
                (unsigned)tick, (unsigned)g_c.state.tick);
        g_c.n_fork++;   /* a skew here is a desync by construction */
    }
    memcpy(g_c.pub, rows, sizeof(RNetRbFrame) * (size_t)slots);
    g_c.pub_slots = slots;
}

static int h_run_tick(void *ctx, uint32_t tick)
{
    (void)ctx;
    (void)tick;
    toy_step();
    return 1;
}

static void h_resim(void *ctx) { (void)ctx; }

static uint32_t h_digest_master(void *ctx)
{
    (void)ctx;
    return toy_digest(&g_c.state);
}

static void h_digest_parts(void *ctx, RNetRbDigestParts *out)
{
    (void)ctx;
    out->master = toy_digest(&g_c.state);
    out->part[0] = g_c.state.acc & 0xffffu;
    out->part[1] = g_c.state.acc >> 16;
    out->part[2] = g_c.state.tick;
}

static void h_decode(void *ctx, int slot, const RNetInputSample *in, RNetRbFrame *out)
{
    (void)ctx;
    (void)slot;
    out->buttons = (uint16_t)((in->bytes[0] | ((uint16_t)in->bytes[1] << 8)) & 0x0FFFu);
}

static void h_sanitize(void *ctx, int slot, RNetRbFrame *f)
{
    (void)ctx;
    (void)slot;
    if (f->buttons == 0xFFFFu)
        f->buttons = 0;
    f->buttons &= 0x0FFFu;
}

static void h_neutral(void *ctx, int slot, RNetRbFrame *out)
{
    (void)ctx;
    (void)slot;
    out->buttons = 0;
}

static void h_lobby(void *ctx)
{
    (void)ctx;
    g_c.n_lobby++;
}

static uint32_t h_now(void *ctx)
{
    (void)ctx;
    return mono_ms();
}

static uint32_t line_epoch(const char *line)
{
    const char *e = strstr(line, "epoch=");
    return e ? (uint32_t)strtoul(e + 6, NULL, 10) : 0xffffffffu;
}

static void h_log(void *ctx, const char *line)
{
    (void)ctx;
    if (strstr(line, "RESIM episode") && strstr(line, "initiator")) {
        g_c.n_ep_init++;
        if (g_c.n_ep_init_ids < MAX_EPOCHS)
            g_c.ep_init[g_c.n_ep_init_ids++] = line_epoch(line);
    }
    if (strstr(line, "RESIM episode") && strstr(line, "follower")) {
        g_c.n_ep_follow++;
        if (g_c.n_ep_follow_ids < MAX_EPOCHS)
            g_c.ep_follow[g_c.n_ep_follow_ids++] = line_epoch(line);
    }
    if (strstr(line, "RB follow refused")) {
        g_c.n_refused++;
        if (g_c.n_ep_refused_ids < MAX_EPOCHS)
            g_c.ep_refused[g_c.n_ep_refused_ids++] = line_epoch(line);
    }
    if (strstr(line, "RB follow deferred epoch") && strstr(line, "span=")) g_c.n_deferred++;
    if (strstr(line, "RB follow at our live tip")) g_c.n_at_tip++;
    if (strstr(line, "RB chain stall")) g_c.n_chain_stall++;
    if (strstr(line, "RB abort")) g_c.n_abort++;
    if (strstr(line, "FORK") && !strstr(line, "FORCE FORK")) g_c.n_fork++;
    if (strstr(line, "timed out waiting")) g_c.n_timeout++;
    if (strstr(line, "RB tip-extend epoch")) g_c.n_tiphold_extend++;
    if (strstr(line, "UNAPPLIED")) g_c.n_unapplied++;
    if (strstr(line, "forced late row")) g_c.n_forced++;
    if (strstr(line, "RB quiesced")) g_c.n_quiesced++;
    if (strstr(line, "RB quiesce TIMED OUT")) g_c.n_quiesce_timeout++;
    if (strstr(line, "RB drain: correction not opened")) g_c.n_drain_unopened++;
    if (strstr(line, "RB tip-hold ended")) g_c.n_tiphold_end++;
    if (strstr(line, "RB match refused")) g_c.n_refusal_lines++;
    if (g_c.log)
        fputs(line, g_c.log);
}

static void sample_local(rnet_u32 tick, RNetInputSample *out, void *opaque)
{
    uint16_t b = toy_pad(g_c.slot, tick);
    (void)opaque;
    memset(out, 0, sizeof(*out));
    out->size = 2;
    out->bytes[0] = (rnet_u8)(b & 0xffu);
    out->bytes[1] = (rnet_u8)(b >> 8);
    out->valid = 1;
}

static void publish_unused(rnet_u32 tick, const RNetInputSample *by_slot, int slots, void *opaque)
{
    (void)tick;
    (void)by_slot;
    (void)slots;
    (void)opaque;
}

static void fill_host(RNetRbHost *h)
{
    memset(h, 0, sizeof(*h));
    h->snap_save = h_snap_save;
    h->snap_load = h_snap_load;
    h->snap_has = h_snap_has;
    h->snap_oldest = h_snap_oldest;
    h->snap_drop_after = h_snap_drop_after;
    h->publish = h_publish;
    h->run_tick = h_run_tick;
    h->resim_begin = h_resim;
    h->resim_end = h_resim;
    h->digest_master = h_digest_master;
    h->digest_parts = h_digest_parts;
    h->decode_sample = h_decode;
    h->sanitize_row = h_sanitize;
    h->neutral_row = h_neutral;
    h->request_return_to_lobby = h_lobby;
    h->log = h_log;
    h->now_ms = h_now;
}

/* ── in-process ──────────────────────────────────────────────────────── */

static char g_last_line[512];
static void capture_log(void *ctx, const char *line)
{
    (void)ctx;
    snprintf(g_last_line, sizeof(g_last_line), "%s", line);
}

static void in_process_tests(void)
{
    uint32_t seat;
    RNetRbDriver *d;
    RNetRbDriverConfig cfg;
    RNetRbHost h;
    RNetSession *null_session = NULL;

    for (seat = 0; seat < RNET_RB_MAX_SLOTS; ++seat) {
        uint32_t e = rnet_rb_epoch_make(41u, seat);
        expect_true(rnet_rb_epoch_initiator(e) == seat, "epoch carries its initiator seat");
        expect_true(e != 0u, "an epoch is never 0");
    }
    expect_true(rnet_rb_epoch_make(1u, 1u) != rnet_rb_epoch_make(1u, 2u),
                "two seats opening at once never share an epoch");

    d = rnet_rb_driver_create();
    expect_true(d != NULL, "driver create");

    memset(&cfg, 0, sizeof(cfg));
    cfg.session = &null_session;
    fill_host(&h);
    h.log = capture_log;
    h.snap_has = NULL;
    expect_true(!rnet_rb_driver_start(d, &cfg, &h), "start refuses a host with a hole");
    expect_true(strstr(g_last_line, "snap_has") != NULL, "...and names the missing callback");

    fill_host(&h);
    h.log = capture_log;
    h.run_tick = NULL;
    cfg.replay_mode = RNET_RB_REPLAY_INLINE;
    expect_true(!rnet_rb_driver_start(d, &cfg, &h), "INLINE replay requires run_tick");
    expect_true(strstr(g_last_line, "run_tick") != NULL, "...and says so");

    fill_host(&h);
    h.log = capture_log;
    cfg.session = NULL;
    expect_true(!rnet_rb_driver_start(d, &cfg, &h), "start refuses no session binding");
    expect_true(strstr(g_last_line, "no session binding") != NULL, "...and says so");

    expect_true(rnet_rb_driver_poll_admit(d) == RNET_RB_ADMIT_STALL,
                "a driver that never started admits nothing");
    rnet_rb_driver_finish_frame(d);   /* must be a no-op, not a crash */
    expect_true(rnet_rb_driver_sim_tick(d) == 0u, "finish_frame without a start is inert");

    /* Identity and mod set survive the cold reset in start(). */
    rnet_rb_driver_set_identity(d, 0x1234u, 0x5678u);
    rnet_rb_driver_destroy(d);
}

/* ── two processes ───────────────────────────────────────────────────── */

typedef struct Report {
    int running;
    uint32_t sim, confirmed;
    uint32_t n_ep_init, n_ep_follow, n_refused, n_abort, n_fork, n_timeout,
             n_extend, n_unapplied, n_lobby, n_forced, replay_admits;
    uint32_t n_quiesced, n_quiesce_timeout, n_drain_unopened, n_tiphold_end;
    uint32_t n_deferred, n_at_tip, n_chain_stall;
    int quiesce_state;
    uint64_t resim_ticks;
    uint32_t episodes_driver;
    /* Refusal scenarios: when it came, where the sim stood, where it stood a
     * second of polling later, and why. */
    uint32_t n_refusal_lines;
    uint32_t refused_after_ms;
    uint32_t sim_at_refusal;
    uint32_t sim_after_hold;
    uint32_t live_admits_after;
    char refusal[32];
    uint32_t ep_init[MAX_EPOCHS], ep_follow[MAX_EPOCHS], ep_refused[MAX_EPOCHS];
    uint32_t n_ep_init_ids, n_ep_follow_ids, n_ep_refused_ids;
    uint32_t timeline[TOY_TICKS];
    /* Hold scenarios: where the sim stood once settled, the engine's digest
     * there, the furthest it got while held, and whether it ever settled. */
    uint32_t hold_sim, hold_digest, hold_max_sim;
    int hold_settled, hold_released;
} Report;

#define MAX_SEATS 4
#define MAX_PROCS 6   /* seats + observers */

typedef struct Scenario {
    const char *name;
    int mode_a, mode_b;       /* seat 0's replay shape; every other seat's */
    const char *latency_ms;   /* per peer, one way; "" = none */
    const char *jitter_ms;
    int force_mispredict;     /* initiator only; 0 = off */
    uint32_t ticks;
    /* 1: only the initiator is asked to quiesce; the follower must start
     * draining from the peer's marker alone. */
    int quiesce_one_side;
    /* Refusal scenarios: the initiator runs RNET_RB_FORCE_BOOT_FORK=1 or
     * RNET_RB_FORCE_MOD_MISMATCH=1 (never the follower), and `refusal` is the
     * code both peers must report. NULL = an ordinary match. */
    int force_boot_fork;
    int force_mod_mismatch;
    const char *refusal;
    /* Seats (0 = 2). With 3 or 4, seat 0 relays for the rest. */
    int seats;
    /* The LAST seat stops polling for this many ms every 10 ticks from tick
     * 60 (after the boot-digest gate, which re-aligns every seat at tick 1):
     * a field that took that long, the way an N64 field takes 30-70 ms at its
     * p99. Each one puts it that far behind until the next episode's wait
     * levels the seats again, so a good share of BEGINs reach it short of
     * the load tick. */
    unsigned lag_ms;
    /* Every seat publishes / confirms a mod set: the handshake runs. */
    int modset;
    /* 1: the follower-behind case must have been exercised (a deferral or a
     * follow at our tip), and nothing refused. */
    int expect_no_refusal;
    /* RBE_RB_TIMESYNC=0 on every seat: the pacing controller would otherwise
     * pull the lagging seat level again (it slows the peer that mispredicts,
     * here the injecting one), and the lag the scenario is about would last a
     * few ticks. */
    int timesync_off;
    /* The LAST seat paces this many ms slower per frame than the others. */
    unsigned slow_ms;
    /* RNET_SIM_LOSS_PCT on every seat ("" = none). A lost BEGIN is never
     * answered, so a lossy scenario grades the ledger as the harnesses do:
     * residual covered by watchdogs, and chain stalls are not a failure. */
    const char *loss_pct;
    /* Seats with a player (bit i = seat i; 0 = every seat). A sparse room
     * (e.g. 0x5: seats 0 and 2 of 4) runs one process per occupied seat;
     * seat 0 must be occupied (it relays). */
    uint32_t occupied_mask;
    /* Observers (spectators, local_slot == slot_count) dialing seat 0's hub.
     * Each must open, follow and refuse nothing, keep up, drain, and agree
     * with seat 0's timeline -- a spectator never predicts. */
    int observers;
    /* Session hold: every seat sets rnet_rb_driver_set_hold(hold_at) 30 ticks
     * before it, must stop exactly there, settle, agree on the state, stay
     * held for a second without the match dropping, then resume. 0 = none. */
    uint32_t hold_at;
} Scenario;

static int write_all(int fd, const void *buf, size_t n)
{
    const char *p = (const char *)buf;
    while (n) {
        ssize_t k = write(fd, p, n);
        if (k <= 0) {
            if (k < 0 && errno == EINTR) continue;
            return 0;
        }
        p += k;
        n -= (size_t)k;
    }
    return 1;
}

static int read_all(int fd, void *buf, size_t n)
{
    char *p = (char *)buf;
    while (n) {
        ssize_t k = read(fd, p, n);
        if (k <= 0) {
            if (k < 0 && errno == EINTR) continue;
            return 0;
        }
        p += k;
        n -= (size_t)k;
    }
    return 1;
}

static int stop_requested(int fd)
{
    char c;
    return read(fd, &c, 1) == 1;
}

static int modset_ok(const char *want, char *reason, uint32_t cap)
{
    (void)want;
    if (reason && cap)
        reason[0] = '\0';
    return 0;
}

static int seats_of(const Scenario *sc)
{
    return sc->seats >= 2 ? sc->seats : 2;
}

static void run_child(const Scenario *sc, int slot, unsigned port_base,
                      uint32_t session_id, int fd, int ready_fd, int stop_fd)
{
    RNetConfig rc;
    RNetHostVTable hv;
    RNetSession *s = NULL;
    RNetRbDriver *d = NULL;
    RNetRbDriverConfig cfg;
    RNetRbHost host;
    int local = slot, slots = seats_of(sc), delay = 8;   /* the SNES harness's D */
    char bind[64], peer[64], logname[128], seed[16];
    uint32_t start_ms, next_tick_ms;
    Report *r = (Report *)calloc(1, sizeof(Report));
    int started;

    memset(&g_c, 0, sizeof(g_c));
    g_c.slot = slot;
    g_c.mode = slot == 0 ? sc->mode_a : sc->mode_b;
    if (slot >= slots)
        snprintf(logname, sizeof(logname), "rb_driver_test_%s_observer.log", sc->name);
    else if (slots == 2)
        snprintf(logname, sizeof(logname), "rb_driver_test_%s_%s.log", sc->name,
                 slot == 0 ? "initiator" : "follower");
    else
        snprintf(logname, sizeof(logname), "rb_driver_test_%s_seat%d.log", sc->name, slot);
    g_c.log = fopen(logname, "w");

    setenv("RNET_SIM_LATENCY_MS", sc->latency_ms, 1);
    setenv("RNET_SIM_JITTER_MS", sc->jitter_ms, 1);
    setenv("RNET_SIM_LOSS_PCT", sc->loss_pct ? sc->loss_pct : "", 1);
    snprintf(seed, sizeof(seed), "%d", 11 + slot * 12);
    setenv("RNET_SIM_SEED", seed, 1);
    /* Pinned per peer, not inherited: a knob exported for one side must never
     * reach the other (the SNES harness learned that twice). */
    if (slot == 0 && sc->force_mispredict > 0) {
        char v[16];
        snprintf(v, sizeof(v), "%d", sc->force_mispredict);
        setenv("RNET_RB_FORCE_MISPREDICT", v, 1);
    } else {
        setenv("RNET_RB_FORCE_MISPREDICT", "0", 1);
    }
    setenv("RBE_RB_TIMESYNC", sc->timesync_off ? "0" : "1", 1);
    setenv("RNET_RB_FORCE_BOOT_FORK", (slot == 0 && sc->force_boot_fork) ? "1" : "0", 1);
    setenv("RNET_RB_FORCE_MOD_MISMATCH", (slot == 0 && sc->force_mod_mismatch) ? "1" : "0", 1);

    rnet_config_init_defaults(&rc);
    rc.slot_count = (rnet_u8)slots;
    rc.local_slot = (rnet_u8)slot;   /* slot == slots: an observer */
    rc.input_delay = (rnet_u8)delay;
    rc.session_id = session_id;
    rc.occupied_mask = sc->occupied_mask;
    memset(&hv, 0, sizeof(hv));
    hv.sample_local = sample_local;
    hv.publish = publish_unused;
    s = rnet_session_create(&rc, &hv);
    snprintf(bind, sizeof(bind), "127.0.0.1:%u", port_base + (unsigned)slot);
    if (!s)
        goto report;
    if (slots == 2) {
        snprintf(peer, sizeof(peer), "127.0.0.1:%u", port_base + (unsigned)(1 - slot));
        started = rnet_session_start_lan(s, bind, peer) == 0;
    } else if (slot == 0) {
        started = rnet_session_start_lan_hub(s, bind) == 0;   /* seat 0 relays */
    } else if (slot >= slots) {
        /* An observer: its own port past the seats', dialing the hub. */
        snprintf(bind, sizeof(bind), "127.0.0.1:%u",
                 port_base + 4u + (unsigned)(slot - slots));
        snprintf(peer, sizeof(peer), "127.0.0.1:%u", port_base);
        started = rnet_session_start_lan(s, bind, peer) == 0;
    } else {
        snprintf(peer, sizeof(peer), "127.0.0.1:%u", port_base);
        started = rnet_session_start_lan(s, bind, peer) == 0;
    }
    if (!started)
        goto report;

    start_ms = mono_ms();
    while (!rnet_session_is_running(s) && (uint32_t)(mono_ms() - start_ms) < 5000u) {
        rnet_session_pump(s);
        sleep_ms(1);
    }
    if (!rnet_session_is_running(s))
        goto report;
    r->running = 1;

    d = rnet_rb_driver_create();
    memset(&cfg, 0, sizeof(cfg));
    cfg.session = &s;
    cfg.local_slot = &local;
    cfg.slot_count = &slots;
    cfg.input_delay = &delay;
    cfg.occupied_mask = sc->occupied_mask;
    cfg.replay_mode = g_c.mode ? RNET_RB_REPLAY_INCREMENTAL : RNET_RB_REPLAY_INLINE;
    cfg.part_names[0] = "acc_lo";
    cfg.part_names[1] = "acc_hi";
    cfg.part_names[2] = "tick";
    cfg.snap_depth = TOY_SNAPS;
    cfg.log_prefix = "rb_test";
    fill_host(&host);
    /* Identity only where a scenario is about it: the ordinary scenarios stay
     * exactly what they were. Same build, same content -- any difference the
     * peers see is the injected one. */
    if (sc->refusal || slots > 2)
        rnet_rb_driver_set_identity(d, 0x0b0b0b0bu, 0x0c0c0c0cu);
    if (sc->modset)
        rnet_rb_driver_set_modset(d, "mods none\n", modset_ok, NULL);
    if (!rnet_rb_driver_start(d, &cfg, &host))
        goto report;

    /* Paced like a frontend: one poll per 60 Hz frame, whatever it returns
     * (a stall presents the held frame and waits for the next one), except
     * that an INCREMENTAL replay tick is followed at once by the next poll --
     * that is the shape a host with an expensive tick runs, pumping between
     * ticks. The injector counts remote rows (one per seat per tick).
     *
     * Stopping is the driver's own coordinated stop: once every peer has
     * played its ticks the parent says so, each asks its driver to quiesce
     * (or, with quiesce_one_side, only the initiator does and the others
     * must follow the marker), and each keeps running -- live and replay
     * alike -- until the driver reports DRAINED. An uncoordinated stop left
     * the peer that finished last opening an episode after the other had
     * exited; the ledger below is exact because this stop is.
     *
     * lag_ms: the last seat stops polling once, at tick 60, pumping its
     * session so it stays linked, and resumes that many ms behind. */
    start_ms = mono_ms();
    next_tick_ms = start_ms;
    {
        int ready_sent = 0, asked = 0;
        uint32_t lagged = 0;
        uint32_t refused_ms = 0;
        fcntl(stop_fd, F_SETFL, O_NONBLOCK);
        while ((uint32_t)(mono_ms() - start_ms) < 40000u) {
            RNetRbAdmit a;
            RNetRbQuiesce q;
            rnet_session_pump(s);
            if (g_c.n_lobby) {
                /* An ordinary scenario treats a refusal as the end (and fails
                 * on it below). A refusal scenario keeps polling for a second,
                 * as a host that has not yet acted on the request would, to
                 * prove the driver itself admits nothing more. */
                if (!sc->refusal)
                    break;
                if (!refused_ms) {
                    refused_ms = mono_ms();
                    r->refused_after_ms = refused_ms - start_ms;
                    r->sim_at_refusal = rnet_rb_driver_sim_tick(d);
                } else if ((uint32_t)(mono_ms() - refused_ms) >= 1000u) {
                    break;
                }
            }
            if (sc->lag_ms && slot == slots - 1 && rnet_rb_driver_sim_tick(d) >= 60u &&
                rnet_rb_driver_sim_tick(d) % 10u == 0u &&
                lagged != rnet_rb_driver_sim_tick(d)) {
                uint32_t t0 = mono_ms();
                lagged = rnet_rb_driver_sim_tick(d);
                while ((uint32_t)(mono_ms() - t0) < sc->lag_ms) {
                    rnet_session_pump(s);
                    sleep_ms(1);
                }
                next_tick_ms = mono_ms();
            }
            if (sc->hold_at && !r->hold_released) {
                uint32_t sim = rnet_rb_driver_sim_tick(d);
                static uint32_t settled_ms;
                static int hold_set;
                if (!hold_set && sim + 30u >= sc->hold_at) {
                    rnet_rb_driver_set_hold(d, sc->hold_at);
                    hold_set = 1;
                }
                if (hold_set && sim > r->hold_max_sim)
                    r->hold_max_sim = sim;
                if (hold_set && !r->hold_settled && rnet_rb_driver_hold_settled(d)) {
                    r->hold_settled = 1;
                    r->hold_sim = sim;
                    r->hold_digest = toy_digest(&g_c.state);
                    settled_ms = mono_ms();
                }
                if (r->hold_settled && (uint32_t)(mono_ms() - settled_ms) >= 1000u) {
                    rnet_rb_driver_clear_hold(d);
                    r->hold_released = 1;
                }
            }
            if (!ready_sent && rnet_rb_driver_sim_tick(d) >= sc->ticks) {
                (void)write_all(ready_fd, "R", 1);
                ready_sent = 1;
            }
            if (ready_sent && !asked && stop_requested(stop_fd)) {
                asked = 1;
                if (!(sc->quiesce_one_side && slot != 0))
                    rnet_rb_driver_request_quiesce(d);
            }
            q = rnet_rb_driver_quiesce_state(d);
            if (q == RNET_RB_QUIESCE_DRAINED || q == RNET_RB_QUIESCE_TIMED_OUT)
                break;
            a = rnet_rb_driver_poll_admit(d);
            if (a != RNET_RB_ADMIT_STALL) {
                if (refused_ms && a == RNET_RB_ADMIT_LIVE)
                    r->live_admits_after++;
                if (a == RNET_RB_ADMIT_REPLAY)
                    g_c.replay_admits++;
                toy_step();   /* the same per-tick function for live and replay */
                rnet_rb_driver_finish_frame(d);
                if (a == RNET_RB_ADMIT_REPLAY)
                    continue;
            }
            next_tick_ms += 16u + (slot == slots - 1 ? sc->slow_ms : 0u);
            if ((int32_t)(next_tick_ms - mono_ms()) > 0)
                sleep_ms(next_tick_ms - mono_ms());
            else
                next_tick_ms = mono_ms();
        }
        if (!ready_sent)
            (void)write_all(ready_fd, "R", 1);
        /* The relay leaves last: a guest's final marker to another guest
         * goes through seat 0, so seat 0 keeps pumping for a moment. */
        if (slots > 2 && slot == 0) {
            uint32_t t0 = mono_ms();
            while ((uint32_t)(mono_ms() - t0) < 700u) {
                rnet_session_pump(s);
                sleep_ms(2);
            }
        }
    }

report:
    if (d) {
        r->sim = rnet_rb_driver_sim_tick(d);
        r->confirmed = rnet_rb_driver_confirmed_through(d);
        r->resim_ticks = rnet_rb_driver_resim_ticks(d);
        r->episodes_driver = rnet_rb_driver_episode_count(d);
        r->quiesce_state = (int)rnet_rb_driver_quiesce_state(d);
        r->sim_after_hold = rnet_rb_driver_sim_tick(d);
        snprintf(r->refusal, sizeof(r->refusal), "%s",
                 rnet_rb_driver_refusal(d) ? rnet_rb_driver_refusal(d) : "");
    }
    r->n_refusal_lines = g_c.n_refusal_lines;
    r->n_quiesced = g_c.n_quiesced;
    r->n_quiesce_timeout = g_c.n_quiesce_timeout;
    r->n_drain_unopened = g_c.n_drain_unopened;
    r->n_tiphold_end = g_c.n_tiphold_end;
    r->n_ep_init = g_c.n_ep_init;
    r->n_ep_follow = g_c.n_ep_follow;
    r->n_refused = g_c.n_refused;
    r->n_abort = g_c.n_abort;
    r->n_fork = g_c.n_fork;
    r->n_timeout = g_c.n_timeout;
    r->n_extend = g_c.n_tiphold_extend;
    r->n_unapplied = g_c.n_unapplied;
    r->n_lobby = g_c.n_lobby;
    r->n_forced = g_c.n_forced;
    r->replay_admits = g_c.replay_admits;
    r->n_deferred = g_c.n_deferred;
    r->n_at_tip = g_c.n_at_tip;
    r->n_chain_stall = g_c.n_chain_stall;
    memcpy(r->ep_init, g_c.ep_init, sizeof(r->ep_init));
    memcpy(r->ep_follow, g_c.ep_follow, sizeof(r->ep_follow));
    memcpy(r->ep_refused, g_c.ep_refused, sizeof(r->ep_refused));
    r->n_ep_init_ids = g_c.n_ep_init_ids;
    r->n_ep_follow_ids = g_c.n_ep_follow_ids;
    r->n_ep_refused_ids = g_c.n_ep_refused_ids;
    memcpy(r->timeline, g_c.timeline, sizeof(r->timeline));
    (void)write_all(fd, r, sizeof(*r));
    if (s)
        rnet_session_send_bye(s);
    rnet_rb_driver_destroy(d);
    if (s)
        rnet_session_destroy(s);
    if (g_c.log)
        fclose(g_c.log);
    free(r);
    _exit(0);
}

static uint32_t count_epoch(const uint32_t *ids, uint32_t n, uint32_t e)
{
    uint32_t i, k = 0;
    for (i = 0; i < n; ++i)
        if (ids[i] == e)
            k++;
    return k;
}

static void run_scenario(const Scenario *sc, unsigned port_base)
{
    /* One process per occupied seat; nseats counts processes, seat_of names
     * each one's seat. */
    int seat_of[MAX_PROCS];
    int nseats = 0, nprocs, nobs = sc->observers;
    int pipes[MAX_PROCS][2], stops[MAX_PROCS][2], ready[2];
    pid_t pids[MAX_PROCS];
    Report *rr[MAX_PROCS];
    int got[MAX_PROCS];
    char c;
    uint32_t session_id = 0x52424456u ^ (uint32_t)getpid() ^ port_base;
    uint32_t upto, t, first_bad = 0;
    uint32_t sum_init = 0, sum_follow = 0, sum_refused = 0, sum_fork = 0, sum_lobby = 0;
    uint32_t sum_timeout = 0, sum_deferred = 0, sum_at_tip = 0, sum_stall = 0;
    int64_t ledger;
    char msg[320];
    int k, j;

    for (k = 0; k < seats_of(sc); ++k)
        if (!sc->occupied_mask || (sc->occupied_mask & (1u << k)))
            seat_of[nseats++] = k;
    nprocs = nseats;
    for (k = 0; k < nobs && nprocs < MAX_PROCS; ++k)
        seat_of[nprocs++] = seats_of(sc);   /* the observer sentinel */
    if (pipe(ready) != 0) {
        expect_true(0, "pipe");
        return;
    }
    for (k = 0; k < nprocs; ++k) {
        if (pipe(pipes[k]) != 0 || pipe(stops[k]) != 0) {
            expect_true(0, "pipe");
            return;
        }
        rr[k] = (Report *)calloc(1, sizeof(Report));
    }
    fflush(stdout);
    fflush(stderr);
    for (k = 0; k < nprocs; ++k) {
        pids[k] = fork();
        if (pids[k] == 0) {
            close(pipes[k][0]);
            run_child(sc, seat_of[k], port_base, session_id, pipes[k][1], ready[1],
                      stops[k][0]);
        }
    }
    for (k = 0; k < nprocs; ++k)
        close(pipes[k][1]);
    close(ready[1]);
    /* Every seat has played its ticks (or given up): stop them together. */
    for (k = 0; k < nprocs; ++k)
        (void)read_all(ready[0], &c, 1);
    for (k = 0; k < nprocs; ++k)
        (void)write_all(stops[k][1], "S", 1);
    for (k = 0; k < nprocs; ++k)
        got[k] = read_all(pipes[k][0], rr[k], sizeof(Report));
    for (k = 0; k < nprocs; ++k) {
        waitpid(pids[k], NULL, 0);
        close(pipes[k][0]);
        close(stops[k][0]);
        close(stops[k][1]);
    }
    close(ready[0]);

    for (k = nseats; k < nprocs; ++k) {
        const Report *r = rr[k];
        uint32_t t2, bad2 = 0, upto2 = r->confirmed < rr[0]->confirmed ? r->confirmed
                                                                       : rr[0]->confirmed;
        printf("%-22s observer init=%u follow=%u refused=%u fork=%u sim=%u (seat0 %u) "
               "confirmed=%u quiesce=%d replay_admits=%u\n", sc->name, r->n_ep_init,
               r->n_ep_follow, r->n_refused, r->n_fork, r->sim, rr[0]->sim, r->confirmed,
               r->quiesce_state, r->replay_admits);
        snprintf(msg, sizeof(msg), "%s: observer reported and ran", sc->name);
        expect_true(got[k] && r->running, msg);
        snprintf(msg, sizeof(msg), "%s: observer opened, followed and refused nothing "
                 "(%u/%u/%u)", sc->name, r->n_ep_init, r->n_ep_follow, r->n_refused);
        expect_true(r->n_ep_init + r->n_ep_follow + r->n_refused == 0u &&
                        r->replay_admits == 0u, msg);
        snprintf(msg, sizeof(msg), "%s: observer 0 forks", sc->name);
        expect_true(r->n_fork == 0u, msg);
        snprintf(msg, sizeof(msg), "%s: observer kept up (sim %u, want >= %u)", sc->name,
                 r->sim, sc->ticks);
        expect_true(r->sim >= sc->ticks, msg);
        snprintf(msg, sizeof(msg), "%s: observer drained (state %d)", sc->name,
                 r->quiesce_state);
        expect_true(r->quiesce_state == RNET_RB_QUIESCE_DRAINED, msg);
        if (upto2 >= TOY_TICKS) upto2 = TOY_TICKS - 1u;
        for (t2 = 1; t2 <= upto2; ++t2)
            if (rr[0]->timeline[t2] != r->timeline[t2]) { bad2 = t2; break; }
        snprintf(msg, sizeof(msg), "%s: observer's timeline agrees with seat 0's through "
                 "tick %u (first diff %u)", sc->name, upto2, bad2);
        expect_true(bad2 == 0u && upto2 >= sc->ticks / 2u, msg);
    }

    for (k = 0; k < nseats; ++k) {
        const Report *r = rr[k];
        printf("%-22s seat%d init=%u follow=%u refused=%u abort=%u timeout=%u fork=%u "
               "extend=%u unapplied=%u forced=%u replay_admits=%u resim=%llu sim=%u "
               "confirmed=%u quiesce=%d unopened=%u tiphold_end=%u deferred=%u "
               "at_tip=%u stalls=%u\n",
               sc->name, seat_of[k], r->n_ep_init, r->n_ep_follow, r->n_refused, r->n_abort,
               r->n_timeout, r->n_fork, r->n_extend, r->n_unapplied, r->n_forced,
               r->replay_admits, (unsigned long long)r->resim_ticks, r->sim, r->confirmed,
               r->quiesce_state, r->n_drain_unopened, r->n_tiphold_end, r->n_deferred,
               r->n_at_tip, r->n_chain_stall);
        sum_init += r->n_ep_init;
        sum_follow += r->n_ep_follow;
        sum_refused += r->n_refused;
        sum_fork += r->n_fork;
        sum_lobby += r->n_lobby;
        sum_timeout += r->n_timeout;
        sum_deferred += r->n_deferred;
        sum_at_tip += r->n_at_tip;
        sum_stall += r->n_chain_stall;
        snprintf(msg, sizeof(msg), "%s: seat %d reported", sc->name, seat_of[k]);
        expect_true(got[k], msg);
        snprintf(msg, sizeof(msg), "%s: seat %d reached RUNNING", sc->name, seat_of[k]);
        expect_true(r->running, msg);
    }

    if (sc->refusal) {
        printf("%-22s refusal=%s/%s after=%u/%u ms sim@refusal=%u/%u "
               "sim+1s=%u/%u live_after=%u/%u lobby=%u/%u\n",
               sc->name, rr[0]->refusal, rr[1]->refusal, rr[0]->refused_after_ms,
               rr[1]->refused_after_ms, rr[0]->sim_at_refusal, rr[1]->sim_at_refusal,
               rr[0]->sim_after_hold, rr[1]->sim_after_hold, rr[0]->live_admits_after,
               rr[1]->live_admits_after, rr[0]->n_lobby, rr[1]->n_lobby);
        for (k = 0; k < nseats; ++k) {
            const Report *r = rr[k];
            const char *who = k ? "follower" : "initiator";
            snprintf(msg, sizeof(msg), "%s: %s asked for the lobby exactly once (%u)",
                     sc->name, who, r->n_lobby);
            expect_true(r->n_lobby == 1u, msg);
            snprintf(msg, sizeof(msg), "%s: %s says why (\"%s\", want \"%s\")",
                     sc->name, who, r->refusal, sc->refusal);
            expect_true(strcmp(r->refusal, sc->refusal) == 0 && r->n_refusal_lines == 1u,
                        msg);
            snprintf(msg, sizeof(msg), "%s: %s refused within the bound (%u ms)",
                     sc->name, who, r->refused_after_ms);
            expect_true(r->refused_after_ms < 6000u, msg);
            snprintf(msg, sizeof(msg), "%s: %s admitted nothing after refusing "
                     "(sim %u -> %u, %u live admits)", sc->name, who,
                     r->sim_at_refusal, r->sim_after_hold, r->live_admits_after);
            expect_true(r->sim_after_hold == r->sim_at_refusal &&
                            r->live_admits_after == 0u,
                        msg);
            if (sc->force_boot_fork) {
                /* Tick 0's digest decides it: tick 1 must never run. */
                snprintf(msg, sizeof(msg), "%s: %s held at tick 1 (sim %u)", sc->name,
                         who, r->sim_at_refusal);
                expect_true(r->sim_at_refusal <= 1u, msg);
            }
            snprintf(msg, sizeof(msg), "%s: %s opened no episode", sc->name, who);
            expect_true(r->n_ep_init + r->n_ep_follow == 0u, msg);
        }
        goto done;
    }
    if (sc->hold_at) {
        for (k = 0; k < nseats; ++k) {
            snprintf(msg, sizeof(msg), "%s: seat %d stopped at the hold tick and settled "
                     "(sim %u, furthest %u, hold %u)", sc->name, seat_of[k], rr[k]->hold_sim,
                     rr[k]->hold_max_sim, sc->hold_at);
            expect_true(rr[k]->hold_settled && rr[k]->hold_sim == sc->hold_at &&
                            rr[k]->hold_max_sim == sc->hold_at && rr[k]->hold_released,
                        msg);
            snprintf(msg, sizeof(msg), "%s: seat %d held the same state as seat 0 "
                     "(%08x vs %08x)", sc->name, seat_of[k], rr[k]->hold_digest, rr[0]->hold_digest);
            expect_true(rr[k]->hold_digest == rr[0]->hold_digest, msg);
        }
    }
    for (k = 0; k < nseats; ++k) {
        snprintf(msg, sizeof(msg), "%s: seat %d ran the match (sim %u)", sc->name, seat_of[k], rr[k]->sim);
        expect_true(rr[k]->sim >= (k == 0 ? sc->ticks : sc->ticks / 2u), msg);
        snprintf(msg, sizeof(msg), "%s: seat %d drained (state %d, %u quiesced lines)",
                 sc->name, seat_of[k], rr[k]->quiesce_state, rr[k]->n_quiesced);
        expect_true(rr[k]->quiesce_state == RNET_RB_QUIESCE_DRAINED &&
                        rr[k]->n_quiesced == 1u,
                    msg);
    }
    snprintf(msg, sizeof(msg), "%s: episodes ran (nothing exercised otherwise)", sc->name);
    expect_true(sum_init > 0u && sum_follow > 0u, msg);
    snprintf(msg, sizeof(msg), "%s: 0 forks", sc->name);
    expect_true(sum_fork == 0u, msg);
    snprintf(msg, sizeof(msg), "%s: no match refused to start", sc->name);
    expect_true(sum_lobby == 0u, msg);
    /* The exact ledger, by role: every episode one seat opens is followed or
     * refused by EVERY other seat, exactly once. No scenario here drops a
     * datagram, and the drain leaves nothing in flight, so there is no third
     * category -- the residual is 0, not "at most the timeouts". Graded per
     * initiator/follower pair by epoch; the sum is printed too. */
    ledger = (int64_t)sum_init * (nseats - 1) - (int64_t)sum_follow - (int64_t)sum_refused;
    snprintf(msg, sizeof(msg), "%s: episode ledger is exact (residual %lld, timeouts %u)",
             sc->name, (long long)ledger, sum_timeout);
    if (sc->loss_pct && sc->loss_pct[0])
        expect_true(ledger >= 0 && ledger <= (int64_t)sum_timeout, msg);
    else
        expect_true(ledger == 0, msg);
    for (k = 0; k < nseats; ++k) {
        for (j = 0; j < nseats; ++j) {
            uint32_t i, bad = 0, n = 0;
            if (j == k)
                continue;
            for (i = 0; i < rr[k]->n_ep_init_ids; ++i) {
                uint32_t e = rr[k]->ep_init[i];
                uint32_t ans = count_epoch(rr[j]->ep_follow, rr[j]->n_ep_follow_ids, e) +
                               count_epoch(rr[j]->ep_refused, rr[j]->n_ep_refused_ids, e);
                n++;
                if (ans != 1u)
                    bad++;
            }
            snprintf(msg, sizeof(msg), "%s: pair %d->%d answered every episode exactly "
                     "once (%u of %u wrong)", sc->name, seat_of[k], seat_of[j], bad, n);
            expect_true(bad == 0u || (sc->loss_pct && sc->loss_pct[0] &&
                                      bad <= sum_timeout), msg);
        }
    }
    if (sc->expect_no_refusal) {
        snprintf(msg, sizeof(msg), "%s: a follower behind the load tick followed it "
                 "(deferred %u, at our tip %u, refused %u)", sc->name, sum_deferred,
                 sum_at_tip, sum_refused);
        expect_true(sum_deferred + sum_at_tip > 0u && sum_refused == 0u, msg);
    }
    /* No advisory stall on a link that loses nothing: an aborted episode no
     * longer leaves a stale digest behind, and nothing else should stall. */
    snprintf(msg, sizeof(msg), "%s: no chain stall (%u)", sc->name, sum_stall);
    expect_true(sum_stall == 0u || (sc->loss_pct && sc->loss_pct[0]), msg);
    if (sc->mode_a || sc->mode_b) {
        uint32_t ra = 0;
        for (k = 0; k < nseats; ++k)
            if (k == 0 ? sc->mode_a : sc->mode_b)
                ra += rr[k]->replay_admits;
        snprintf(msg, sizeof(msg), "%s: the incremental peer replayed tick by tick", sc->name);
        expect_true(ra > 0u, msg);
    }
    if (!sc->mode_a) {
        snprintf(msg, sizeof(msg), "%s: an inline peer never hands back a replay tick",
                 sc->name);
        expect_true(rr[0]->replay_admits == 0u, msg);
    }

    /* Every final timeline agrees with seat 0's through what every side
     * confirmed. This is the engine's own state, not the driver's hash chain. */
    upto = rr[0]->confirmed;
    for (k = 1; k < nseats; ++k)
        if (rr[k]->confirmed < upto)
            upto = rr[k]->confirmed;
    if (upto >= TOY_TICKS) upto = TOY_TICKS - 1u;
    snprintf(msg, sizeof(msg), "%s: the confirmed frontier advanced (%u)", sc->name, upto);
    expect_true(upto >= sc->ticks / 3u, msg);
    for (k = 1; k < nseats; ++k) {
        first_bad = 0;
        for (t = 1; t <= upto; ++t) {
            if (rr[0]->timeline[t] != rr[k]->timeline[t]) {
                first_bad = t;
                break;
            }
        }
        snprintf(msg, sizeof(msg), "%s: seat %d's timeline agrees with seat 0's through "
                 "tick %u (first diff %u)", sc->name, seat_of[k], upto, first_bad);
        expect_true(first_bad == 0u, msg);
    }
done:
    for (k = 0; k < nprocs; ++k)
        free(rr[k]);
}

int main(int argc, char **argv)
{
    /* The SNES sweep's gating latencies and its injector interval (45), at a
     * 60 Hz frontend cadence, so a pass here means what a pass there means.
     * RNET_SIM_LATENCY_MS is one-way per peer: RTT is double. */
    static const Scenario scenarios[] = {
        /* name                  mode A, B   latency jitter force  ticks  one-side */
        { "inline-loopback",     0, 0,       "0",   "0",   45,    420u,  0 },
        { "mixed-rtt60",         0, 1,       "30",  "8",   45,    420u,  0 },
        { "incremental-rtt200",  1, 1,       "100", "25",  45,    420u,  0 },
        /* The stop propagates: only the initiator is asked. */
        { "drain-one-side-rtt200", 0, 0,     "100", "25",  45,    420u,  1 },
        /* Refusals: both peers name the same reason and admit nothing more,
         * whether or not the host has acted on the request yet. */
        { "refuse-boot-fork",    0, 0,       "30",  "8",   0,     420u,  0,
          1, 0, "boot_digest_mismatch" },
        { "refuse-mod-mismatch", 0, 1,       "30",  "8",   0,     420u,  0,
          0, 1, "mod_set_mismatch" },
        /* The follower runs 3-4 ticks behind on a 0 ms link, so the
         * initiator's load tick is often one it has not simulated yet: it must
         * follow (deferred, or at its tip), never refuse -- and the chain must
         * not stall on the episode. */
        { "follower-behind-0ms", 1, 1,       "0",   "0",   15,    420u,  0,
          0, 0, NULL, 2, 50u, 0, 1, 1, 0u },
        { "follower-behind-inline", 0, 0,    "0",   "0",   15,    420u,  0,
          0, 0, NULL, 2, 50u, 0, 1, 1, 0u },
        /* More than two seats, seat 0 relaying: the per-peer chain, the
         * per-seat mod-set and identity handshakes, per-pair ledger. */
        { "3seat-loopback",      1, 1,       "0",   "0",   45,    420u,  0,
          0, 0, NULL, 3, 0u, 1, 0 },
        { "3seat-behind",        1, 1,       "0",   "0",   30,    420u,  0,
          0, 0, NULL, 3, 50u, 1, 1, 1, 0u },
        { "4seat-rtt200",        1, 1,       "100", "25",  45,    420u,  0,
          0, 0, NULL, 4, 0u, 1, 0 },
        { "4seat-mixed-rtt60",   0, 1,       "30",  "8",   45,    420u,  1,
          0, 0, NULL, 4, 0u, 1, 0 },
        /* Four seats losing 2 % of what each receives: a commit needs three
         * POSTs at each of four peers, so a lost one is common -- re-sent
         * POSTs keep that from costing the 2 s watchdog. */
        { "4seat-loss2",         1, 1,       "0",   "0",   45,    420u,  0,
          0, 0, NULL, 4, 0u, 1, 0, 0, 0u, "2" },
        /* A sparse room: seats 0 and 2 of 4, seats 1 and 3 empty. Nobody
         * sits in an empty seat, so nobody seals its rows or sends its
         * POST; an episode that waits on one never completes. */
        { "sparse-0+2-of-4",     1, 1,       "30",  "8",   45,    420u,  0,
          0, 0, NULL, 4, 0u, 1, 0, 0, 0u, NULL, 0x5u },
        { "sparse-0+2-of-4-inline", 0, 0,    "0",   "0",   45,    420u,  0,
          0, 0, NULL, 4, 0u, 1, 0, 0, 0u, NULL, 0x5u },
        /* A spectator watching four seats through seat 0's hub, with real
         * latency so the players mispredict and roll back around it. */
        { "4seat-observer-rtt60", 1, 1,      "30",  "8",   45,    420u,  0,
          0, 0, NULL, 4, 0u, 1, 0, 0, 0u, NULL, 0u, 1 },
        /* A paused menu: every seat stops on one tick with predictions still
         * being corrected around it (forced mispredicts, jitter, loss), holds
         * for a second, and resumes in the same epoch. */
        { "hold-rtt60",          0, 1,       "30",  "8",   45,    420u,  0,
          0, 0, NULL, 0, 0u, 1, 0, 0, 0u, NULL, 0u, 0, 200u },
        { "hold-4seat-loss2",    1, 1,       "30",  "8",   45,    420u,  0,
          0, 0, NULL, 4, 0u, 1, 0, 0, 0u, "2", 0u, 0, 200u },
    };
    /* Four ports per scenario from a per-process base; kept inside
     * 20000..60003 however many scenarios there are (a base near the top of
     * the old range plus a late scenario's offset used to run past 65535, and
     * that scenario's sessions never started). */
    unsigned pid_off = ((unsigned)getpid() % 4000u) * 8u;
    size_t i;

    in_process_tests();
    /* An argument runs only the scenarios whose name contains it. */
    for (i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
        if (argc < 2 || strstr(scenarios[i].name, argv[1]))
            run_scenario(&scenarios[i],
                         20000u + (pid_off + (unsigned)i * 1994u) % 40000u);

    if (g_failures == 0) {
        printf("rb_driver_test: ok\n");
        return 0;
    }
    fprintf(stderr, "rb_driver_test: %d failure(s)\n", g_failures);
    return 1;
}

#endif /* !_WIN32 */
