/*
 * Host relay over ICE (recomp_net/host_ice.h): the waiting-room agents.
 */
#include "recomp_net/host_ice.h"

#include "ice/rnet_ice_internal.h"
#include "nat/rnet_sig_hold.h"
#include "platform/rnet_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HI_REPORT_REFRESH_MS 45000u /* the server trusts a report for 120 s */
#define HI_FAIL_RETRY_MS 20000u
#define HI_CONNECT_TIMEOUT_MS 25000u
#define HI_HOLD_MAX_AGE_MS 30000u

typedef struct HiPeer {
    int used;
    struct RNetHostIce *owner;
    char pid[RNET_HOST_ICE_ID_LEN];
    int slot;          /* the seat this agent serves */
    int local_slot;    /* guest: our own seat when the agent was built */
    RNetIceAgent *agent;
    int gen;           /* negotiation id 1..255; host: 0 until the first SDP */
    rnet_u64 created_ms;
    /* guest reporting */
    char last_report[8];
    rnet_u64 report_due_ms;
    rnet_u64 retry_at_ms;
    /* Round trip over this agent once it is linked (-1 until measured). */
    int rtt_ms;
    rnet_u64 ping_due_ms;
} HiPeer;

static void hi_rtt_pump(HiPeer *p, rnet_u64 now);

struct RNetHostIce {
    int role; /* 0 idle, 1 host, 2 guest */
    HiPeer peer[RNET_HOST_ICE_MAX_PEERS];
    RNetSigHold hold;
    int (*send_signal)(const char *, int, int, const char *, void *);
    int (*send_json)(const char *, void *);
    void *ctx;
    char stun_host[128];
    int stun_set;      /* view said something about STUN (even "") */
    unsigned short stun_port;
    char bind_address[64];
    char host_pid[RNET_HOST_ICE_ID_LEN]; /* guest: the one sender we accept */
    unsigned rejected;
    unsigned reports_sent;
    char last_report[8];
    int unavailable_reported; /* guest, ICE not built in: "fail" sent */
};

int rnet_host_ice_available(void)
{
#if defined(RNET_ENABLE_ICE)
    return 1;
#else
    return 0;
#endif
}

RNetHostIce *rnet_host_ice_create(void)
{
    return (RNetHostIce *)calloc(1, sizeof(RNetHostIce));
}

static void peer_free_agent(HiPeer *p)
{
    if (p->agent != NULL) {
        rnet_ice_agent_destroy(p->agent);
        p->agent = NULL;
    }
}

static void peer_destroy(HiPeer *p)
{
    peer_free_agent(p);
    memset(p, 0, sizeof(*p));
}

static void destroy_all(RNetHostIce *h)
{
    int i;
    for (i = 0; i < RNET_HOST_ICE_MAX_PEERS; ++i)
        peer_destroy(&h->peer[i]);
    rnet_sig_hold_clear(&h->hold);
}

void rnet_host_ice_destroy(RNetHostIce **h)
{
    if (h == NULL || *h == NULL)
        return;
    destroy_all(*h);
    free(*h);
    *h = NULL;
}

void rnet_host_ice_destroy_agent(RNetIceAgent *agent)
{
    rnet_ice_agent_destroy(agent);
}

int rnet_host_ice_sig_is_ours(int wire_type)
{
    return wire_type > RNET_LOBBY_SIG_HOSTICE_BASE && wire_type <= RNET_LOBBY_SIG_HOSTICE_BASE + 6;
}

/* An agent describes itself with LOCAL_*; its peer must be pushed REMOTE_*. */
static void emit_cb(const RNetSignal *msg, void *user)
{
    HiPeer *p = (HiPeer *)user;
    RNetHostIce *h;
    if (p == NULL || msg == NULL || (h = p->owner) == NULL || h->send_signal == NULL)
        return;
    if (msg->type != RNET_SIGNAL_LOCAL_SDP && msg->type != RNET_SIGNAL_LOCAL_CANDIDATE &&
        msg->type != RNET_SIGNAL_GATHERING_DONE)
        return;
    if (p->gen <= 0)
        return; /* an answerer says nothing before it has been offered to */
    (void)h->send_signal(p->pid, RNET_LOBBY_SIG_HOSTICE_BASE + (int)msg->type, p->gen, msg->text,
                         h->ctx);
}

/* Build (or rebuild) the agent for a peer. Never TURN, never force_relay,
 * never a fixed port. */
static int peer_build_agent(RNetHostIce *h, HiPeer *p, int controlling)
{
    RNetIceConfig cfg;
    peer_free_agent(p);
    rnet_ice_config_init_defaults(&cfg);
    if (h->stun_set) {
        cfg.stun_host = h->stun_host[0] ? h->stun_host : NULL;
        if (h->stun_port)
            cfg.stun_port = h->stun_port;
        else if (h->stun_host[0])
            cfg.stun_port = 3478;
    }
    cfg.bind_address = h->bind_address[0] ? h->bind_address : NULL;
    cfg.bind_port = 0;
    cfg.controlling = (rnet_u8)(controlling ? 1 : 0);
    cfg.force_relay = 0;
    p->agent = rnet_ice_agent_create(&cfg, emit_cb, p);
    if (p->agent == NULL)
        return -1;
    p->created_ms = rnet_os_monotonic_ms();
    p->rtt_ms = -1;
    p->ping_due_ms = 0;
    p->last_report[0] = '\0';
    p->report_due_ms = 0;
    p->retry_at_ms = 0;
    /* Answerer records gather_pending and gathers on the offer; the offerer
     * gathers now and its LOCAL_SDP leaves on the next poll. */
    if (rnet_ice_agent_start_gathering(p->agent) != 0) {
        peer_free_agent(p);
        return -1;
    }
    return 0;
}

static int next_gen(int g)
{
    g = g + 1;
    if (g > 255 || g < 1)
        g = 1;
    return g;
}

/* Deliver one signal (already attributed to p's seat) to p's agent. */
static void apply(RNetHostIce *h, HiPeer *p, const RNetSignal *sig)
{
    const int gen = sig->flag;
    if (p->agent == NULL)
        return;
    if (h->role == 1 && sig->type == RNET_SIGNAL_REMOTE_SDP) {
        if (gen == 0)
            return;
        if (gen == p->gen)
            return; /* a repeat of the SDP this agent already answered */
        /* A new negotiation id is a fresh offerer (seat move, retry): answer it
         * with a fresh agent, never by renegotiating a live one. */
        if (p->gen != 0 && peer_build_agent(h, p, 0) != 0)
            return;
        p->gen = gen;
    } else if (gen != p->gen) {
        return; /* stale: from an earlier negotiation */
    }
    if (rnet_ice_agent_is_frozen(p->agent))
        return; /* linked: nothing may rebuild or perturb it */
    rnet_ice_agent_push_signal(p->agent, sig);
}

static HiPeer *find_peer(RNetHostIce *h, const char *pid)
{
    int i;
    for (i = 0; i < RNET_HOST_ICE_MAX_PEERS; ++i)
        if (h->peer[i].used && strcmp(h->peer[i].pid, pid) == 0)
            return &h->peer[i];
    return NULL;
}

int rnet_host_ice_push_signal(RNetHostIce *h, const char *from_player_id, int from_slot,
                              int wire_type, int flag, const char *text)
{
    RNetSignal sig;
    HiPeer *p;
    int t;
    if (h == NULL || h->role == 0 || from_player_id == NULL || from_player_id[0] == '\0' ||
        from_slot < 0 || !rnet_host_ice_sig_is_ours(wire_type) || flag < 0 || flag > 255) {
        if (h != NULL)
            h->rejected++;
        return -1;
    }
    t = wire_type - RNET_LOBBY_SIG_HOSTICE_BASE;
    if (t == (int)RNET_SIGNAL_SET_CONTROLLING || t == (int)RNET_SIGNAL_REMOTE_SDP ||
        t == (int)RNET_SIGNAL_REMOTE_CANDIDATE) {
        /* Roles are fixed; and only LOCAL_* / GATHERING_DONE are ever emitted. */
        h->rejected++;
        return -1;
    }
    if (h->role == 2 && strcmp(from_player_id, h->host_pid) != 0) {
        h->rejected++; /* a guest hears the host and nobody else */
        return -1;
    }
    memset(&sig, 0, sizeof(sig));
    sig.type = t == (int)RNET_SIGNAL_LOCAL_SDP ? RNET_SIGNAL_REMOTE_SDP
             : t == (int)RNET_SIGNAL_LOCAL_CANDIDATE ? RNET_SIGNAL_REMOTE_CANDIDATE
                                                     : (RNetSignalType)t;
    sig.flag = (rnet_u8)flag;
    sig.peer_slot = 0xFF;
    if (text != NULL)
        snprintf(sig.text, sizeof(sig.text), "%s", text);

    p = find_peer(h, from_player_id);
    if (p != NULL && p->agent != NULL && p->slot == from_slot) {
        apply(h, p, &sig);
        return 0;
    }
    /* No agent yet, or the sender's seat is not the one the agent serves:
     * keep it until the agent for that seat exists (update replays only the
     * entries whose seat matches). */
    if (rnet_sig_hold_push(&h->hold, from_player_id, from_slot, &sig,
                           (rnet_u64)rnet_os_monotonic_ms()) != 0)
        return -1;
    return 0;
}

static void replay_hold(RNetHostIce *h, HiPeer *p)
{
    int i;
    const int n = rnet_sig_hold_count(&h->hold, p->pid);
    for (i = 0; i < n; ++i) {
        const RNetSigHoldEntry *e = rnet_sig_hold_get(&h->hold, p->pid, i);
        if (e != NULL && e->slot == p->slot)
            apply(h, p, &e->sig);
    }
    rnet_sig_hold_drop(&h->hold, p->pid);
}

static int send_report(RNetHostIce *h, const char *path, const char *ice)
{
    char json[96];
    if (h->send_json == NULL)
        return -1;
    if (ice != NULL && ice[0])
        snprintf(json, sizeof(json), "{\"op\":\"path_report\",\"path\":\"%s\",\"ice\":\"%s\"}", path,
                 ice);
    else
        snprintf(json, sizeof(json), "{\"op\":\"path_report\",\"path\":\"%s\"}", path);
    if (h->send_json(json, h->ctx) != 0)
        return -1;
    snprintf(h->last_report, sizeof(h->last_report), "%s", path);
    h->reports_sent++;
    return 0;
}

static void guest_report(RNetHostIce *h, HiPeer *p, rnet_u64 now)
{
    const RNetIceState st = rnet_ice_agent_state(p->agent);
    if (st == RNET_ICE_STATE_COMPLETED) {
        if (strcmp(p->last_report, "direct") != 0 || now >= p->report_due_ms) {
            char path[16];
            const char *ice = NULL;
            path[0] = '\0';
            rnet_ice_agent_selected_info(p->agent, path, sizeof(path), NULL, 0, NULL, 0);
            if (!strcmp(path, "host") || !strcmp(path, "srflx") || !strcmp(path, "prflx"))
                ice = path;
            if (send_report(h, "direct", ice) == 0) {
                snprintf(p->last_report, sizeof(p->last_report), "direct");
                p->report_due_ms = now + HI_REPORT_REFRESH_MS;
            }
        }
        return;
    }
    if (st == RNET_ICE_STATE_FAILED ||
        (now >= p->created_ms && now - p->created_ms > HI_CONNECT_TIMEOUT_MS)) {
        if (p->retry_at_ms == 0) {
            if (strcmp(p->last_report, "fail") != 0 && send_report(h, "fail", NULL) == 0)
                snprintf(p->last_report, sizeof(p->last_report), "fail");
            p->retry_at_ms = now + HI_FAIL_RETRY_MS;
        } else if (now >= p->retry_at_ms) {
            /* A fresh negotiation: new agent, new id. The host rebinds on it. */
            const int g = next_gen(p->gen);
            if (peer_build_agent(h, p, 1) == 0)
                p->gen = g;
        }
    }
}

void rnet_host_ice_update(RNetHostIce *h, const RNetHostIceView *v)
{
    int i, j;
    rnet_u64 now;
    if (h == NULL)
        return;
    if (v == NULL || !v->active || v->peers == NULL) {
        destroy_all(h);
        h->role = 0;
        return;
    }
    now = (rnet_u64)rnet_os_monotonic_ms();
    {
        const int role = v->is_host ? 1 : 2;
        if (role != h->role) {
            destroy_all(h);
            h->role = role;
            h->last_report[0] = '\0';
            h->unavailable_reported = 0;
        }
    }
    h->send_signal = v->send_signal;
    h->send_json = v->send_json;
    h->ctx = v->ctx;
    h->stun_set = v->stun_host != NULL;
    snprintf(h->stun_host, sizeof(h->stun_host), "%s", v->stun_host ? v->stun_host : "");
    h->stun_port = v->stun_port;
    snprintf(h->bind_address, sizeof(h->bind_address), "%s", v->bind_address ? v->bind_address : "");
    {
        /* Test aids (documented in host_ice.h), so a loopback test can drive the
         * real lobby client without a network. */
        const char *e = getenv("RNET_HOST_ICE_BIND");
        if (e != NULL && e[0] != '\0' && h->bind_address[0] == '\0')
            snprintf(h->bind_address, sizeof(h->bind_address), "%s", e);
        e = getenv("RNET_HOST_ICE_NO_STUN");
        if (e != NULL && e[0] == '1')
        {
            h->stun_set = 1;
            h->stun_host[0] = '\0';
        }
    }
    h->host_pid[0] = '\0';
    if (h->role == 2 && v->peer_count > 0 && v->peers[0].player_id != NULL)
        snprintf(h->host_pid, sizeof(h->host_pid), "%s", v->peers[0].player_id);

    /* Remove agents whose peer left, moved, or (guest) whose own seat moved. */
    for (i = 0; i < RNET_HOST_ICE_MAX_PEERS; ++i) {
        HiPeer *p = &h->peer[i];
        int keep = 0;
        if (!p->used)
            continue;
        for (j = 0; j < v->peer_count; ++j) {
            const RNetHostIcePeer *w = &v->peers[j];
            if (w->player_id != NULL && strcmp(w->player_id, p->pid) == 0 && w->slot == p->slot &&
                (h->role == 1 || v->local_slot == p->local_slot)) {
                keep = 1;
                break;
            }
        }
        if (!keep)
            peer_destroy(p);
    }
    /* Add agents for peers we do not serve yet. */
    for (j = 0; j < v->peer_count && j < RNET_HOST_ICE_MAX_PEERS; ++j) {
        const RNetHostIcePeer *w = &v->peers[j];
        HiPeer *p;
        if (w->player_id == NULL || w->player_id[0] == '\0' || w->slot < 0 ||
            find_peer(h, w->player_id) != NULL)
            continue;
        if (!rnet_host_ice_available()) {
            if (h->role == 2 && !h->unavailable_reported &&
                send_report(h, "fail", NULL) == 0)
                h->unavailable_reported = 1;
            continue;
        }
        p = NULL;
        for (i = 0; i < RNET_HOST_ICE_MAX_PEERS; ++i)
            if (!h->peer[i].used) {
                p = &h->peer[i];
                break;
            }
        if (p == NULL)
            break;
        memset(p, 0, sizeof(*p));
        p->used = 1;
        p->owner = h;
        snprintf(p->pid, sizeof(p->pid), "%s", w->player_id);
        p->slot = w->slot;
        p->local_slot = v->local_slot;
        p->gen = h->role == 2 ? next_gen(0) : 0;
        if (peer_build_agent(h, p, h->role == 2) != 0) {
            memset(p, 0, sizeof(*p));
            continue;
        }
        replay_hold(h, p);
    }
    /* Drive every agent. */
    for (i = 0; i < RNET_HOST_ICE_MAX_PEERS; ++i) {
        HiPeer *p = &h->peer[i];
        if (!p->used || p->agent == NULL)
            continue;
        rnet_ice_agent_poll(p->agent);
        if (rnet_ice_agent_state(p->agent) == RNET_ICE_STATE_COMPLETED) {
            rnet_ice_agent_freeze(p->agent); /* linked: no later signal rebuilds it */
            hi_rtt_pump(p, now);
        }
        if (h->role == 2)
            guest_report(h, p, now);
    }
    rnet_sig_hold_expire(&h->hold, now, HI_HOLD_MAX_AGE_MS);
}

/* Waiting-room latency over the linked agent itself. The lobby's own figure
 * is a ping through the lobby server's WebSocket (rnet_lobby_member_latency_ms
 * fell back to nothing else), so two peers on one machine read ~100 ms while
 * their link is loopback. Nothing else reads these agents before launch; the
 * match session that adopts them drops a stray ping, which does not decode
 * with its protocol magic. */
#define HI_RTT_MAGIC "RNETHP1"
#define HI_RTT_MAGIC_LEN 7
#define HI_RTT_PING_MS 1000u
static void hi_rtt_send(HiPeer *p, char kind, unsigned long long ts, unsigned hold)
{
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "%s%c%llu %u", HI_RTT_MAGIC, kind, ts, hold);
    if (n > 0 && (size_t)n < sizeof(buf))
        (void)rnet_ice_agent_send(p->agent, (const rnet_u8 *)buf, (size_t)n);
}
static void hi_rtt_pump(HiPeer *p, rnet_u64 now)
{
    rnet_u8 buf[64];
    size_t n = 0;
    rnet_u64 arrival = 0;
    /* Both ends pump at the launcher's frame rate, so a reply sent on the
     * next pump and read on the one after that counted up to two frames of
     * waiting as latency (~25 ms between two instances on one machine). Time
     * from the datagram's arrival on libjuice's thread, and have the replier
     * report how long it held the ping, so the figure is the link's. */
    while (rnet_ice_agent_recv_at(p->agent, buf, sizeof(buf) - 1, &n, &arrival) == 0 && n > 0) {
        unsigned long long ts;
        unsigned long hold = 0;
        char *end;
        if (n <= HI_RTT_MAGIC_LEN || memcmp(buf, HI_RTT_MAGIC, HI_RTT_MAGIC_LEN) != 0)
            continue;
        buf[n] = '\0';
        ts = strtoull((const char *)buf + HI_RTT_MAGIC_LEN + 1, &end, 10);
        if (end && *end == ' ')
            hold = strtoul(end + 1, NULL, 10);
        if (buf[HI_RTT_MAGIC_LEN] == 'P') {
            hi_rtt_send(p, 'Q', ts, now >= arrival ? (unsigned)(now - arrival) : 0u);
        } else if (buf[HI_RTT_MAGIC_LEN] == 'Q' && ts <= arrival && arrival - ts < 60000u) {
            rnet_u64 span = arrival - ts;
            int ms = (int)(span > hold ? span - hold : 0);
            /* Light smoothing: one late reply should not jump the readout. */
            p->rtt_ms = p->rtt_ms < 0 ? ms : (p->rtt_ms * 3 + ms + 2) / 4;
        }
    }
    if (now >= p->ping_due_ms) {
        hi_rtt_send(p, 'P', (unsigned long long)rnet_os_monotonic_ms(), 0u);
        p->ping_due_ms = now + HI_RTT_PING_MS;
    }
}

int rnet_host_ice_peer_rtt_ms(const RNetHostIce *h, int slot)
{
    int i;
    if (h == NULL)
        return -1;
    for (i = 0; i < RNET_HOST_ICE_MAX_PEERS; ++i) {
        const HiPeer *p = &h->peer[i];
        if (!p->used || p->agent == NULL)
            continue;
        /* Host: one agent per guest seat. Guest: its one agent, which serves
         * the host's seat. Either way, the row of the peer at the far end. */
        if (p->slot == slot)
            return p->rtt_ms;
    }
    return -1;
}

void rnet_host_ice_status(const RNetHostIce *h, RNetHostIceStatus *out)
{
    int i;
    if (out == NULL)
        return;
    memset(out, 0, sizeof(*out));
    if (h == NULL)
        return;
    out->role = h->role;
    out->held_dropped = h->hold.dropped;
    out->rejected = h->rejected;
    out->reports_sent = h->reports_sent;
    snprintf(out->last_report, sizeof(out->last_report), "%s", h->last_report);
    for (i = 0; i < RNET_HOST_ICE_MAX_PEERS; ++i) {
        const HiPeer *p = &h->peer[i];
        RNetHostIcePeerStatus *s;
        if (!p->used)
            continue;
        s = &out->peer[out->peer_count++];
        snprintf(s->player_id, sizeof(s->player_id), "%s", p->pid);
        s->slot = p->slot;
        s->state = p->agent ? (int)rnet_ice_agent_state(p->agent) : (int)RNET_ICE_STATE_IDLE;
        s->negotiation = p->gen;
        if (s->state == (int)RNET_ICE_STATE_COMPLETED) {
            char path[16];
            out->completed++;
            path[0] = '\0';
            rnet_ice_agent_selected_info(p->agent, path, sizeof(path), NULL, 0, NULL, 0);
            snprintf(s->path, sizeof(s->path), "%.7s", path);
        }
    }
}

int rnet_host_ice_held_count(const RNetHostIce *h, const char *player_id)
{
    return h ? rnet_sig_hold_count(&h->hold, player_id) : 0;
}

int rnet_host_ice_peer_completed(const RNetHostIce *h, const char *player_id)
{
    int i;
    if (h == NULL || player_id == NULL)
        return 0;
    for (i = 0; i < RNET_HOST_ICE_MAX_PEERS; ++i)
        if (h->peer[i].used && h->peer[i].agent != NULL && strcmp(h->peer[i].pid, player_id) == 0)
            return rnet_ice_agent_state(h->peer[i].agent) == RNET_ICE_STATE_COMPLETED;
    return 0;
}

RNetIceAgent *rnet_host_ice_take_completed(RNetHostIce *h, const char *player_id)
{
    HiPeer *p;
    RNetIceAgent *a;
    if (h == NULL || player_id == NULL || (p = find_peer(h, player_id)) == NULL ||
        p->agent == NULL || rnet_ice_agent_state(p->agent) != RNET_ICE_STATE_COMPLETED)
        return NULL;
    a = p->agent;
    p->agent = NULL;
    /* The emit callback points at this peer; the session re-points it on
     * adoption, but cut it now so a poll in between cannot signal. */
    rnet_ice_agent_set_emit(a, NULL, NULL);
    rnet_ice_agent_freeze(a);
    memset(p, 0, sizeof(*p));
    return a;
}
