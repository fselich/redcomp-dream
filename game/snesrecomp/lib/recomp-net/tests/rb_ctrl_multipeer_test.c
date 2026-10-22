/* Episode-control receive queues with more than two seats, over real UDP
 * and (second pass) the deterministic latency/jitter/loss simulator.
 *
 * Regression for a 4-seat WAN race: three guests retransmitting RB_POST and
 * RB_SEAL_ROWS while the host sat in an inline replay filled the 32-entry
 * queue with copies of the same messages; the host then refused new ones
 * (1,344 drops) and the session ended. Also covers RB_RESOLVED, which carried
 * no sender: with three peers the host took the fastest peer's frontier as
 * everyone's and opened episodes the slower followers had to refuse. */
#include "recomp_net/recomp_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <process.h>
#include <stdlib.h>
static unsigned test_pid(void) { return (unsigned)_getpid(); }
static void set_env(const char *k, const char *v) { _putenv_s(k, v ? v : ""); }
#else
#include <unistd.h>
static unsigned test_pid(void) { return (unsigned)getpid(); }
static void set_env(const char *k, const char *v)
{
    if (v)
        setenv(k, v, 1);
    else
        unsetenv(k);
}
#endif

enum { kSeats = 4 };

static int g_failures;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s\n", msg);                                \
            ++g_failures;                                                      \
        } else {                                                               \
            printf("ok:   %s\n", msg);                                         \
        }                                                                      \
    } while (0)

static void sample_local(rnet_u32 tick, RNetInputSample *out, void *opaque)
{
    (void)opaque;
    memset(out, 0, sizeof(*out));
    out->size = 2;
    out->bytes[1] = (rnet_u8)tick;
    out->valid = 1;
}

static void publish(rnet_u32 tick, const RNetInputSample *by_slot, int slots, void *opaque)
{
    (void)tick;
    (void)by_slot;
    (void)slots;
    (void)opaque;
}

static void pause_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void pump_guests(RNetSession **s)
{
    int i;
    for (i = 1; i < kSeats; ++i)
        rnet_session_pump(s[i]);
}

static int run_pass(const char *label, unsigned port_base)
{
    RNetConfig cfg[kSeats];
    RNetHostVTable vt[kSeats];
    RNetSession *s[kSeats] = {NULL, NULL, NULL, NULL};
    char bind[kSeats][64];
    RNetRbFrame rows[RNET_RB_SEAL_ROWS_CHUNK_MAX];
    int i, r, ok = 0;
    int posts[kSeats] = {0}, seals[kSeats] = {0}, resolved_n[kSeats] = {0};
    rnet_u32 resolved_v[kSeats] = {0};
    char msg[160];

    memset(rows, 0, sizeof(rows));
    for (i = 0; i < kSeats; ++i)
    {
        rnet_config_init_defaults(&cfg[i]);
        cfg[i].local_slot = (rnet_u8)i;
        cfg[i].slot_count = kSeats;
        cfg[i].session_id = 0x52424351u ^ test_pid() ^ port_base;
        memset(&vt[i], 0, sizeof(vt[i]));
        vt[i].sample_local = sample_local;
        vt[i].publish = publish;
        s[i] = rnet_session_create(&cfg[i], &vt[i]);
        snprintf(bind[i], sizeof(bind[i]), "127.0.0.1:%u", port_base + (unsigned)i);
        if (!s[i])
            goto out;
    }
    if (rnet_session_start_lan_hub(s[0], bind[0]) != 0)
        goto out;
    for (i = 1; i < kSeats; ++i)
        if (rnet_session_start_lan(s[i], bind[i], bind[0]) != 0)
            goto out;
    for (r = 0; r < 8000; ++r)
    {
        int all = 1;
        for (i = 0; i < kSeats; ++i)
        {
            rnet_session_pump(s[i]);
            if (!rnet_session_is_running(s[i]))
                all = 0;
        }
        if (all)
            break;
        pause_ms(1);
    }
    snprintf(msg, sizeof(msg), "%s: four seats running", label);
    CHECK(r < 8000, msg);
    if (r >= 8000)
        goto out;

    /* The host does not drain (inline replay) while every guest retransmits
     * the same POST and SEAL chunk 60 times and advertises a rising RESOLVED.
     * Guest g's frontier tops out at 100 * g (sent ten times: the link loses
     * some). */
    for (r = 0; r < 60; ++r)
    {
        for (i = 1; i < kSeats; ++i)
        {
            (void)rnet_session_send_rb_post(s[i], 7u, 500u, 0xabc0u + (rnet_u32)i, 0u, 1u);
            (void)rnet_session_send_rb_seal_rows(s[i], 7u, 480u, 500u, (rnet_u8)i, 480u,
                                                 rows, 20u);
            (void)rnet_session_send_rb_resolved(
                s[i], (rnet_u32)(100 * i) - (rnet_u32)(r < 50 ? 50 - r : 0));
        }
        pump_guests(s);
        rnet_session_pump(s[0]); /* receive only -- no take */
        pause_ms(2);
    }
    /* Let delayed (simulated) datagrams land. */
    for (r = 0; r < 400; ++r)
    {
        pump_guests(s);
        rnet_session_pump(s[0]);
        pause_ms(1);
    }

    {
        rnet_u32 e, t, dm, id, mm, tt, rb;
        rnet_u8 m, sl;
        rnet_u16 rc;
        while (rnet_session_take_rb_post(s[0], &e, &t, &dm, &id, &m))
        {
            int f = rnet_session_rb_last_take_from(s[0]);
            if (f > 0 && f < kSeats)
                posts[f]++;
        }
        while (rnet_session_take_rb_seal_rows(s[0], &e, &mm, &tt, &sl, &rb, rows, &rc))
        {
            int f = rnet_session_rb_last_take_from(s[0]);
            if (f > 0 && f < kSeats)
                seals[f]++;
        }
        while (rnet_session_take_rb_resolved(s[0], &t))
        {
            int f = rnet_session_rb_last_take_from(s[0]);
            if (f > 0 && f < kSeats)
            {
                resolved_n[f]++;
                if (t > resolved_v[f])
                    resolved_v[f] = t;
            }
        }
    }
    snprintf(msg, sizeof(msg), "%s: no episode-control datagram refused (dropped=%u)", label,
             (unsigned)rnet_session_rb_ctrl_dropped(s[0]));
    CHECK(rnet_session_rb_ctrl_dropped(s[0]) == 0u, msg);
    for (i = 1; i < kSeats; ++i)
    {
        snprintf(msg, sizeof(msg), "%s: seat %d POST retransmits coalesced (%d queued)", label, i,
                 posts[i]);
        CHECK(posts[i] == 1, msg);
        snprintf(msg, sizeof(msg), "%s: seat %d SEAL retransmits coalesced (%d queued)", label, i,
                 seals[i]);
        CHECK(seals[i] == 1, msg);
        snprintf(msg, sizeof(msg), "%s: seat %d RESOLVED attributed, one entry, highest (%d, %u)",
                 label, i, resolved_n[i], (unsigned)resolved_v[i]);
        CHECK(resolved_n[i] == 1 && resolved_v[i] == (rnet_u32)(100 * i), msg);
    }

    /* A new episode must still get in when the queue holds an older one. */
    for (r = 0; r < 40; ++r)
    {
        for (i = 1; i < kSeats; ++i)
            (void)rnet_session_send_rb_post(s[i], 8u + (rnet_u32)r, 600u + (rnet_u32)r, 1u, 0u, 1u);
        pump_guests(s);
        rnet_session_pump(s[0]);
        pause_ms(1);
    }
    for (r = 0; r < 400; ++r)
    {
        pump_guests(s);
        rnet_session_pump(s[0]);
        pause_ms(1);
    }
    {
        rnet_u32 e, t, dm, id, last_e = 0;
        rnet_u8 m;
        while (rnet_session_take_rb_post(s[0], &e, &t, &dm, &id, &m))
            if (e > last_e)
                last_e = e;
        snprintf(msg, sizeof(msg), "%s: newest episode POST survives a full queue (epoch %u)",
                 label, (unsigned)last_e);
        CHECK(last_e == 47u, msg);
    }
    ok = 1;
out:
    if (!ok)
    {
        snprintf(msg, sizeof(msg), "%s: setup (ports %u..%u)",
                 label, port_base, port_base + kSeats - 1u);
        CHECK(0, msg);
    }
    for (i = 0; i < kSeats; ++i)
        if (s[i])
            rnet_session_destroy(s[i]);
    return ok;
}

int main(void)
{
    /* Reserve eight ports in 20000..51999, including the impaired pass.
     * Starting at 44000 let high PID residues produce ports above 65535. */
    unsigned base = 20000u + (test_pid() % 4000u) * 8u;
    set_env("RNET_SIM_LATENCY_MS", NULL);
    set_env("RNET_SIM_LOSS_PCT", NULL);
    (void)run_pass("clean link", base);
    /* Impaired: 40 ms +-20 ms jitter (reorders) and 10% loss, fixed seed. */
    set_env("RNET_SIM_LATENCY_MS", "40");
    set_env("RNET_SIM_JITTER_MS", "20");
    set_env("RNET_SIM_LOSS_PCT", "10");
    set_env("RNET_SIM_SEED", "4242");
    (void)run_pass("impaired link", base + 4u);
    if (g_failures)
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("ALL PASS\n");
    return 0;
}
