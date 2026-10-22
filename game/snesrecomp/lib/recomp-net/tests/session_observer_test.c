/*
 * A spectator runs the match and cannot touch it.
 *
 * Three real UDP peers over loopback: two seated players and one observer.
 * The host hubs, so every datagram it fans out reaches the observer -- which
 * is the shape the lobby SFU produces, with the relay standing in for the hub.
 *
 * What this has to prove, and what a contract test could not:
 *   1. the observer reaches RUNNING and publishes the same resolved inputs the
 *      players do, tick for tick -- it really is simulating the same match;
 *   2. it publishes them without ever being sampled for input;
 *   3. the players' view is byte-identical to a two-player match, so the
 *      observer's presence changes nothing about what they simulate.
 */
#include "recomp_net/recomp_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
static unsigned test_pid(void) { return (unsigned)_getpid(); }
#else
#include <unistd.h>
static unsigned test_pid(void) { return (unsigned)getpid(); }
#endif

enum { kHashCapacity = 512 };

typedef struct HostCtx
{
    rnet_u8 slot;
    int sampled;            /* times sample_local was called */
    int published;
    int overflow;
    rnet_u32 hashes[kHashCapacity];
} HostCtx;

static int g_failures;
static rnet_u64 g_now_ms = 1000;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    g_failures++;
}

static void sample_local(rnet_u32 tick, RNetInputSample *out, void *opaque)
{
    HostCtx *ctx = (HostCtx *)opaque;
    ctx->sampled++;
    memset(out, 0, sizeof(*out));
    out->size = 4;
    out->bytes[0] = ctx->slot;
    out->bytes[1] = (rnet_u8)tick;
    out->bytes[2] = (rnet_u8)(tick >> 8);
    out->bytes[3] = (rnet_u8)(0xa5u ^ ctx->slot);
    out->valid = 1;
}

static void publish(rnet_u32 tick, const RNetInputSample *by_slot, int slots,
                    void *opaque)
{
    HostCtx *ctx = (HostCtx *)opaque;
    rnet_u32 hash = rnet_checksum(&tick, sizeof(tick));
    int slot;
    for (slot = 0; slot < slots; ++slot)
    {
        hash ^= rnet_checksum(&by_slot[slot], sizeof(by_slot[slot]));
        hash *= 16777619u;
    }
    if (ctx->published < kHashCapacity)
        ctx->hashes[ctx->published] = hash;
    else
        ctx->overflow = 1;
    ctx->published++;
}

static rnet_u64 now_ms(void *opaque)
{
    (void)opaque;
    return g_now_ms;
}

int main(void)
{
    RNetConfig ca, cb, cc;
    RNetHostVTable va, vb, vc;
    RNetSession *a = NULL, *b = NULL, *c = NULL;
    HostCtx ha, hb, hc;
    char bind_a[64], bind_b[64], bind_c[64];
    const unsigned base_port = 42300u + (test_pid() % 900u) * 3u;
    int i;
    int common;

    memset(&ha, 0, sizeof(ha));
    memset(&hb, 0, sizeof(hb));
    memset(&hc, 0, sizeof(hc));
    ha.slot = 0;
    hb.slot = 1;
    hc.slot = 9; /* would be visible in a hash if it ever reached the sim */

    memset(&va, 0, sizeof(va));
    va.sample_local = sample_local; va.publish = publish; va.now_ms = now_ms;
    va.ctx = &ha;
    vb = va; vb.ctx = &hb;
    vc = va; vc.ctx = &hc;

    rnet_config_init_defaults(&ca);
    ca.slot_count = 2;
    ca.input_delay = 3;
    ca.session_id = 0x4f425352u ^ test_pid();
    cb = ca;
    cc = ca;
    ca.local_slot = 0;
    cb.local_slot = 1;
    /* The observer: no seat, and a wire id above the seats -- the number the
     * relay uses to know it must not forward anything this peer sends. */
    cc.local_slot = 2;
    cc.wire_slot = 2;

    snprintf(bind_a, sizeof(bind_a), "127.0.0.1:%u", base_port);
    snprintf(bind_b, sizeof(bind_b), "127.0.0.1:%u", base_port + 1U);
    snprintf(bind_c, sizeof(bind_c), "127.0.0.1:%u", base_port + 2U);

    a = rnet_session_create(&ca, &va);
    b = rnet_session_create(&cb, &vb);
    c = rnet_session_create(&cc, &vc);
    if (a == NULL || b == NULL || c == NULL)
    {
        fail("session create");
        goto done;
    }
    if (!rnet_session_is_observer(c)) fail("observer reports itself");
    if (rnet_session_is_observer(a) || rnet_session_is_observer(b))
        fail("a seated peer is not an observer");

    /* Host hubs so its fan-out reaches both the guest and the gallery. */
    if (rnet_session_start_lan_hub(a, bind_a) != 0 ||
        rnet_session_start_lan(b, bind_b, bind_a) != 0 ||
        rnet_session_start_lan(c, bind_c, bind_a) != 0)
    {
        fail("LAN start");
        goto done;
    }

    /* Bring all three up before driving ticks. */
    for (i = 0; i < 20000; ++i)
    {
        g_now_ms++;
        rnet_session_pump(a);
        rnet_session_pump(b);
        rnet_session_pump(c);
        if (rnet_session_is_running(a) && rnet_session_is_running(b) &&
            rnet_session_is_running(c))
            break;
    }
    if (!rnet_session_is_running(a) || !rnet_session_is_running(b))
        fail("the players did not reach RUNNING");
    if (!rnet_session_is_running(c))
        fail("the observer did not reach RUNNING");

    for (i = 0; i < 60000 && (ha.published < 40 || hb.published < 40 ||
                              hc.published < 40); ++i)
    {
        g_now_ms++;
        rnet_session_pump(a);
        rnet_session_pump(b);
        rnet_session_pump(c);
        if (ha.published < 40 &&
            rnet_session_try_admit(a, rnet_session_sim_tick(a)))
            rnet_session_advance(a);
        if (hb.published < 40 &&
            rnet_session_try_admit(b, rnet_session_sim_tick(b)))
            rnet_session_advance(b);
        if (hc.published < 40 &&
            rnet_session_try_admit(c, rnet_session_sim_tick(c)))
            rnet_session_advance(c);
    }

    if (ha.published < 40 || hb.published < 40)
        fail("the players did not run");
    if (hc.published < 40)
        fail("the observer did not run");

    /* It never contributed. Not "its input was ignored" -- it was never asked
     * for input at all, which is the only version of this that survives a
     * spectator with a controller in their hands. */
    if (hc.sampled != 0)
        fail("the observer was sampled for input");
    if (ha.sampled == 0 || hb.sampled == 0)
        fail("the players were not sampled");

    /* And it saw the same match. Same resolved inputs, tick for tick: if the
     * observer's own pad had reached any slot, or if it had resolved a slot
     * differently, these hashes would diverge. */
    common = ha.published < hb.published ? ha.published : hb.published;
    if (hc.published < common) common = hc.published;
    if (common < 40) common = 0;
    for (i = 0; i < common; ++i)
    {
        if (ha.hashes[i] != hb.hashes[i])
        {
            fail("the two players disagree");
            break;
        }
        if (hc.hashes[i] != ha.hashes[i])
        {
            fprintf(stderr, "  first divergence at published %d\n", i);
            fail("the observer simulated a different match");
            break;
        }
    }
    if (ha.overflow || hb.overflow || hc.overflow)
        fail("hash capacity overflow");

done:
    rnet_session_destroy(a);
    rnet_session_destroy(b);
    rnet_session_destroy(c);
    if (g_failures == 0)
    {
        printf("session_observer_test: ok (%d/%d/%d published, observer "
               "sampled %d times)\n",
               ha.published, hb.published, hc.published, hc.sampled);
        return 0;
    }
    fprintf(stderr, "session_observer_test: %d failure(s)\n", g_failures);
    return 1;
}
