#include "recomp_net/ice_rtt.h"

#include "ice/rnet_ice_internal.h"
#include "platform/rnet_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same wire form as the waiting room's link timing (rnet_host_ice.c):
 * MAGIC 'P'|'Q' <sender's monotonic ms> <ms the replier held the ping>. */
#define RNET_ICE_RTT_MAGIC "RNETIR2"
#define RNET_ICE_RTT_MAGIC_LEN 7
/* Short: a probe takes a handful of samples and closes. */
#define RNET_ICE_RTT_PING_MS 250ULL
#define RNET_ICE_RTT_RELAY_FALLBACK_MS 3500ULL

struct RNetIceRttProbe
{
    RNetIceAgent *agent;
    RNetIceRttSignalEmitFn emit;
    void *user;
    int pinger; /* the offerer pings; the answerer only replies */
    int rtt_ms; /* best sample */
    int samples;
    rnet_u64 next_ping_ms;
    rnet_u64 open_ms;
    int relay_fallback_tried;
    char path[16];
};

#if defined(RNET_ENABLE_ICE)

static void ice_rtt_emit_bridge(const RNetSignal *msg, void *user)
{
    RNetIceRttProbe *p = (RNetIceRttProbe *)user;
    if (!p || !p->emit || !msg)
        return;
    p->emit(msg, p->user);
}

static void ice_rtt_send(RNetIceRttProbe *p, char kind, unsigned long long ts, unsigned hold)
{
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "%s%c%llu %u", RNET_ICE_RTT_MAGIC, kind, ts, hold);
    if (n > 0 && (size_t)n < sizeof(buf))
        (void)rnet_ice_agent_send(p->agent, (const rnet_u8 *)buf, (size_t)n);
}

/* Timed from the datagram's arrival on libjuice's thread, less the time the
 * far end held the ping before its pump replied: the figure is the link's,
 * not either side's frame cadence. */
static void ice_rtt_drain(RNetIceRttProbe *p, rnet_u64 now)
{
    rnet_u8 buf[64];
    size_t n = 0;
    rnet_u64 arrival = 0;
    while (rnet_ice_agent_recv_at(p->agent, buf, sizeof(buf) - 1, &n, &arrival) == 0 && n > 0) {
        unsigned long long ts;
        unsigned long hold = 0;
        char *end;
        if (n <= RNET_ICE_RTT_MAGIC_LEN || memcmp(buf, RNET_ICE_RTT_MAGIC, RNET_ICE_RTT_MAGIC_LEN) != 0)
            continue;
        buf[n] = '\0';
        ts = strtoull((const char *)buf + RNET_ICE_RTT_MAGIC_LEN + 1, &end, 10);
        if (end && *end == ' ')
            hold = strtoul(end + 1, NULL, 10);
        if (buf[RNET_ICE_RTT_MAGIC_LEN] == 'P') {
            ice_rtt_send(p, 'Q', ts, now >= arrival ? (unsigned)(now - arrival) : 0u);
        } else if (buf[RNET_ICE_RTT_MAGIC_LEN] == 'Q' && ts <= arrival && arrival - ts < 60000u) {
            rnet_u64 span = arrival - ts;
            int ms = (int)(span > hold ? span - hold : 0);
            if (p->rtt_ms < 0 || ms < p->rtt_ms)
                p->rtt_ms = ms;
            p->samples++;
        }
    }
}

int rnet_ice_rtt_open(RNetIceRttProbe **out, const RNetIceConfig *ice,
                      RNetIceRttSignalEmitFn emit, void *user)
{
    RNetIceRttProbe *p;
    RNetIceConfig local;
    if (!out || !ice)
        return -1;
    *out = NULL;
    p = (RNetIceRttProbe *)calloc(1, sizeof(*p));
    if (!p)
        return -1;
    local = *ice;
    p->emit = emit;
    p->user = user;
    p->pinger = ice->controlling ? 1 : 0;
    p->rtt_ms = -1;
    p->open_ms = rnet_os_monotonic_ms();
    p->next_ping_ms = 0;
    snprintf(p->path, sizeof(p->path), "pending");
    p->agent = rnet_ice_agent_create(&local, ice_rtt_emit_bridge, p);
    if (!p->agent) {
        free(p);
        return -1;
    }
    if (rnet_ice_agent_start_gathering(p->agent) != 0) {
        rnet_ice_agent_destroy(p->agent);
        free(p);
        return -1;
    }
    *out = p;
    return 0;
}

void rnet_ice_rtt_close(RNetIceRttProbe **probe)
{
    if (!probe || !*probe)
        return;
    rnet_ice_agent_destroy((*probe)->agent);
    free(*probe);
    *probe = NULL;
}

void rnet_ice_rtt_push_signal(RNetIceRttProbe *probe, const RNetSignal *msg)
{
    if (!probe || !probe->agent || !msg)
        return;
    rnet_ice_agent_push_signal(probe->agent, msg);
}

void rnet_ice_rtt_pump(RNetIceRttProbe *probe)
{
    RNetIceState st;
    rnet_u64 now;

    if (!probe || !probe->agent)
        return;

    rnet_ice_agent_poll(probe->agent);
    st = rnet_ice_agent_state(probe->agent);
    now = rnet_os_monotonic_ms();

    /* Auto relay fallback when host/srflx cannot complete (CGNAT). */
    if (!probe->relay_fallback_tried &&
        !rnet_ice_agent_is_force_relay(probe->agent) &&
        rnet_ice_agent_has_turn(probe->agent) &&
        st != RNET_ICE_STATE_CONNECTED && st != RNET_ICE_STATE_COMPLETED &&
        st != RNET_ICE_STATE_FAILED &&
        (now - probe->open_ms) >= RNET_ICE_RTT_RELAY_FALLBACK_MS) {
        if (rnet_ice_agent_restart_force_relay(probe->agent) == 0) {
            probe->relay_fallback_tried = 1;
            probe->open_ms = now;
            probe->rtt_ms = -1;
            probe->samples = 0;
            snprintf(probe->path, sizeof(probe->path), "pending");
        } else {
            probe->relay_fallback_tried = 1;
        }
        rnet_ice_agent_poll(probe->agent);
        st = rnet_ice_agent_state(probe->agent);
    }

    if (st == RNET_ICE_STATE_CONNECTED || st == RNET_ICE_STATE_COMPLETED) {
        rnet_ice_agent_selected_info(probe->agent, probe->path, sizeof(probe->path),
                                     NULL, 0, NULL, 0);
        ice_rtt_drain(probe, now);
        if (probe->pinger && now >= probe->next_ping_ms) {
            ice_rtt_send(probe, 'P', (unsigned long long)rnet_os_monotonic_ms(), 0u);
            probe->next_ping_ms = now + RNET_ICE_RTT_PING_MS;
        }
    } else if (st == RNET_ICE_STATE_FAILED) {
        snprintf(probe->path, sizeof(probe->path), "failed");
    }
}

RNetIceState rnet_ice_rtt_state(const RNetIceRttProbe *probe)
{
    if (!probe || !probe->agent)
        return RNET_ICE_STATE_IDLE;
    return rnet_ice_agent_state(probe->agent);
}

int rnet_ice_rtt_ms(const RNetIceRttProbe *probe)
{
    return probe ? probe->rtt_ms : -1;
}

int rnet_ice_rtt_samples(const RNetIceRttProbe *probe)
{
    return probe ? probe->samples : 0;
}

void rnet_ice_rtt_selected_path(const RNetIceRttProbe *probe, char *path_out,
                                size_t path_len)
{
    if (!path_out || path_len == 0)
        return;
    if (!probe) {
        snprintf(path_out, path_len, "none");
        return;
    }
    snprintf(path_out, path_len, "%s", probe->path[0] ? probe->path : "pending");
}

#else /* !RNET_ENABLE_ICE */

int rnet_ice_rtt_open(RNetIceRttProbe **out, const RNetIceConfig *ice,
                      RNetIceRttSignalEmitFn emit, void *user)
{
    (void)ice;
    (void)emit;
    (void)user;
    if (out)
        *out = NULL;
    return -1;
}

void rnet_ice_rtt_close(RNetIceRttProbe **probe)
{
    if (probe)
        *probe = NULL;
}

void rnet_ice_rtt_push_signal(RNetIceRttProbe *probe, const RNetSignal *msg)
{
    (void)probe;
    (void)msg;
}

void rnet_ice_rtt_pump(RNetIceRttProbe *probe)
{
    (void)probe;
}

RNetIceState rnet_ice_rtt_state(const RNetIceRttProbe *probe)
{
    (void)probe;
    return RNET_ICE_STATE_IDLE;
}

int rnet_ice_rtt_ms(const RNetIceRttProbe *probe)
{
    (void)probe;
    return -1;
}

int rnet_ice_rtt_samples(const RNetIceRttProbe *probe)
{
    (void)probe;
    return 0;
}

void rnet_ice_rtt_selected_path(const RNetIceRttProbe *probe, char *path_out,
                                size_t path_len)
{
    (void)probe;
    if (path_out && path_len)
        snprintf(path_out, path_len, "none");
}

#endif /* RNET_ENABLE_ICE */
