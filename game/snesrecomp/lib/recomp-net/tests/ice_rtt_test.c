#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
/*
 * Room-list latency probe (recomp_net/ice_rtt.h) over REAL libjuice agents
 * on loopback: a browser's pinger and a host's responder, their signals
 * carried by an in-memory queue that maps LOCAL_* to REMOTE_* exactly as the
 * lobby client's probe_signal handler does.
 *
 *   1  the pair links and the pinger collects samples; the responder, which
 *      only replies, collects none;
 *   2  the figure is the link's: loopback reads a few ms at most, although
 *      both ends are pumped only every 18 ms (a launcher frame) -- arrival
 *      timing and the replier's hold report take the frame cadence out.
 *
 * Requires RNET_ENABLE_ICE. Exit status is non-zero on any failure.
 */
#include "recomp_net/recomp_net.h"
#include "recomp_net/ice_rtt.h"
#include "platform/rnet_platform.h"

#include <stdio.h>
#include <string.h>

#define TIMEOUT_MS 15000
#define QMAX 256

static int g_failures;

static void check(int cond, const char *what)
{
    if (cond)
        printf("PASS: %s\n", what);
    else
    {
        printf("FAIL: %s\n", what);
        g_failures++;
    }
}

typedef struct Msg
{
    int to;
    RNetSignal sig;
} Msg;

static Msg g_q[QMAX];
static int g_qn;

/* user = index of the RECEIVER. */
static void emit(const RNetSignal *msg, void *user)
{
    Msg *m;
    if (msg->type != RNET_SIGNAL_LOCAL_SDP && msg->type != RNET_SIGNAL_LOCAL_CANDIDATE &&
        msg->type != RNET_SIGNAL_GATHERING_DONE)
        return;
    if (g_qn >= QMAX)
        return;
    m = &g_q[g_qn++];
    m->to = (int)(size_t)user;
    m->sig = *msg;
    m->sig.type = msg->type == RNET_SIGNAL_LOCAL_SDP ? RNET_SIGNAL_REMOTE_SDP
                : msg->type == RNET_SIGNAL_LOCAL_CANDIDATE ? RNET_SIGNAL_REMOTE_CANDIDATE
                                                           : RNET_SIGNAL_GATHERING_DONE;
}

int main(void)
{
    RNetIceRttProbe *p[2] = {NULL, NULL};
    RNetIceConfig cfg;
    rnet_u64 t0;
    int i;

    rnet_ice_config_init_defaults(&cfg);
    cfg.stun_host = NULL; /* loopback host candidates only */
    cfg.bind_address = "127.0.0.1";
    cfg.controlling = 1;
    check(rnet_ice_rtt_open(&p[0], &cfg, emit, (void *)(size_t)1) == 0, "pinger opens");
    cfg.controlling = 0;
    check(rnet_ice_rtt_open(&p[1], &cfg, emit, (void *)(size_t)0) == 0, "responder opens");
    if (!p[0] || !p[1])
        return 1;

    t0 = rnet_os_monotonic_ms();
    while (rnet_os_monotonic_ms() - t0 < TIMEOUT_MS && rnet_ice_rtt_samples(p[0]) < 4)
    {
        int n = g_qn;
        g_qn = 0;
        for (i = 0; i < n; ++i)
            rnet_ice_rtt_push_signal(p[g_q[i].to], &g_q[i].sig);
        rnet_ice_rtt_pump(p[0]);
        rnet_ice_rtt_pump(p[1]);
        rnet_os_sleep_micros(18000);
    }
    {
        char path[16];
        rnet_ice_rtt_selected_path(p[0], path, sizeof(path));
        printf("pinger: %d samples, best %d ms, path %s\n", rnet_ice_rtt_samples(p[0]),
               rnet_ice_rtt_ms(p[0]), path);
    }
    check(rnet_ice_rtt_samples(p[0]) >= 4, "1: the pinger timed replies over the link");
    check(rnet_ice_rtt_samples(p[1]) == 0, "1: the responder only replies");
    check(rnet_ice_rtt_ms(p[0]) >= 0 && rnet_ice_rtt_ms(p[0]) <= 5,
          "2: loopback reads as loopback, not as frame cadence");

    rnet_ice_rtt_close(&p[0]);
    rnet_ice_rtt_close(&p[1]);
    if (g_failures)
        printf("ice_rtt_test: %d failure(s)\n", g_failures);
    else
        printf("ice_rtt_test: ok\n");
    return g_failures ? 1 : 0;
}
