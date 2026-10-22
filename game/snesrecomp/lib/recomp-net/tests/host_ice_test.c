#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
/*
 * Host relay over ICE, waiting room to match (recomp_net/host_ice.h).
 *
 * One host RNetHostIce and two guest RNetHostIce instances, each driven with
 * the view the lobby client would build, over REAL libjuice agents on
 * loopback. The only in-memory part is the lobby: a queue that delivers each
 * `signal` to the addressed instance exactly as rnet_lobby_client.c's handler
 * does (sender id and seat looked up by the receiver's table, never taken from
 * the payload). It is a LIBRARY-level test, so the three instances share one
 * process; there is no oracle here, only "a datagram sent by a real session
 * arrives at another real session over the agents the waiting room connected".
 *
 * Scenarios:
 *   1  waiting room: both guests reach COMPLETED, each reports path_report
 *      "direct" with an ice type, the host counts two completed peers;
 *   2  guards: signals that must be refused or held, never applied;
 *   3  a seat move rebuilds only that guest's pair and it reconnects;
 *   4  handover: waiting-room agents sit idle >= 20 s after COMPLETED, are
 *      adopted by real sessions (host: rnet_session_start_ice_hub_adopt,
 *      guests: rnet_session_adopt_ice_agent), and a datagram sent by guest 1
 *      arrives at guest 2 through the host hub afterwards.
 *
 * Requires RNET_ENABLE_ICE. Exit status is non-zero on any failure.
 */
#include "recomp_net/recomp_net.h"
#include "recomp_net/host_ice.h"
#include "platform/rnet_platform.h"

#include <stdio.h>
#include <string.h>

#define TIMEOUT_MS 20000
#define NODES 3
#define QMAX 512

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
    int from;
    int to;
    int type;
    int flag;
    char text[2048];
} Msg;

typedef struct Node
{
    const char *pid;
    int slot;            /* the seat as every OTHER node's table sees it */
    RNetHostIce *h;
    int idx;
    char reports[8][96];
    int nreports;
} Node;

static Node g_n[NODES];
static Msg g_q[QMAX];
static int g_qn;
static int g_guest_slot[NODES]; /* seat of guest idx (1..2) */

static int node_of(const char *pid)
{
    int i;
    for (i = 0; i < NODES; ++i)
        if (!strcmp(g_n[i].pid, pid)) return i;
    return -1;
}

static int wire_signal(const char *to, int type, int flag, const char *text, void *ctx)
{
    Node *me = (Node *)ctx;
    int t = node_of(to);
    if (t < 0 || g_qn >= QMAX) return -1;
    g_q[g_qn].from = me->idx;
    g_q[g_qn].to = t;
    g_q[g_qn].type = type;
    g_q[g_qn].flag = flag;
    snprintf(g_q[g_qn].text, sizeof(g_q[g_qn].text), "%s", text);
    g_qn++;
    return 0;
}

static int wire_json(const char *json, void *ctx)
{
    Node *me = (Node *)ctx;
    if (me->nreports < 8)
        snprintf(me->reports[me->nreports++], sizeof(me->reports[0]), "%s", json);
    return 0;
}

static void update_node(int i)
{
    RNetHostIceView v;
    RNetHostIcePeer peers[2];
    memset(&v, 0, sizeof(v));
    v.active = 1;
    v.is_host = (i == 0);
    v.local_slot = i == 0 ? 0 : g_guest_slot[i];
    if (i == 0)
    {
        peers[0].player_id = g_n[1].pid; peers[0].slot = g_guest_slot[1];
        peers[1].player_id = g_n[2].pid; peers[1].slot = g_guest_slot[2];
        v.peer_count = 2;
    }
    else
    {
        peers[0].player_id = g_n[0].pid; peers[0].slot = 0;
        v.peer_count = 1;
    }
    v.peers = peers;
    v.stun_host = "";              /* loopback host candidates only */
    v.bind_address = "127.0.0.1";
    v.send_signal = wire_signal;
    v.send_json = wire_json;
    v.ctx = &g_n[i];
    rnet_host_ice_update(g_n[i].h, &v);
}

/* Deliver like the lobby handler: the receiver's table supplies the seat. */
static void deliver(void)
{
    int i;
    static Msg batch[QMAX];
    int n = g_qn;
    memcpy(batch, g_q, sizeof(Msg) * (size_t)n);
    g_qn = 0;
    for (i = 0; i < n; ++i)
    {
        int from_slot = batch[i].from == 0 ? 0 : g_guest_slot[batch[i].from];
        (void)rnet_host_ice_push_signal(g_n[batch[i].to].h, g_n[batch[i].from].pid, from_slot,
                                        batch[i].type, batch[i].flag, batch[i].text);
    }
}

static void step(void)
{
    int i;
    for (i = 0; i < NODES; ++i) update_node(i);
    deliver();
    rnet_os_sleep_micros(2000);
}

static int wait_completed(void)
{
    int i;
    for (i = 0; i < TIMEOUT_MS / 2; ++i)
    {
        RNetHostIceStatus hs, a, b;
        step();
        rnet_host_ice_status(g_n[0].h, &hs);
        rnet_host_ice_status(g_n[1].h, &a);
        rnet_host_ice_status(g_n[2].h, &b);
        if (hs.completed == 2 && a.completed == 1 && b.completed == 1) return 1;
    }
    return 0;
}

static void setup(void)
{
    int i;
    memset(g_n, 0, sizeof(g_n));
    g_qn = 0;
    g_n[0].pid = "player-host"; g_n[1].pid = "player-g1"; g_n[2].pid = "player-g2";
    g_guest_slot[1] = 1; g_guest_slot[2] = 2;
    for (i = 0; i < NODES; ++i)
    {
        g_n[i].idx = i;
        g_n[i].h = rnet_host_ice_create();
    }
}

static void teardown(void)
{
    int i;
    for (i = 0; i < NODES; ++i) rnet_host_ice_destroy(&g_n[i].h);
}

/* ---- sessions for the handover scenario ---- */
static void sample_local(rnet_u32 tick, RNetInputSample *out, void *ctx)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->size = 2;
    out->bytes[0] = (rnet_u8)tick;
    out->valid = 1;
}
static void publish(rnet_u32 tick, const RNetInputSample *by_slot, int slots, void *ctx)
{
    (void)tick; (void)by_slot; (void)slots; (void)ctx;
}

static RNetSession *mk_session(int slot)
{
    RNetConfig c;
    RNetHostVTable vt;
    rnet_config_init_defaults(&c);
    c.slot_count = 3;
    c.input_delay = 3;
    c.session_id = 0x48494345u;
    c.local_slot = (rnet_u8)slot;
    memset(&vt, 0, sizeof(vt));
    vt.sample_local = sample_local;
    vt.publish = publish;
    return rnet_session_create(&c, &vt);
}

static int ack_arrives(RNetSession *ss[3], int from, int to, const char *tag)
{
    int i;
    char reason[64];
    rnet_u8 status = 0;
    if (rnet_session_send_modset_ack(ss[from], 7, tag) != 0) return 0;
    for (i = 0; i < 4000; ++i)
    {
        int k;
        for (k = 0; k < 3; ++k) rnet_session_pump(ss[k]);
        rnet_os_sleep_micros(2000);
        if ((i % 100) == 0) (void)rnet_session_send_modset_ack(ss[from], 7, tag);
        reason[0] = 0;
        while (rnet_session_take_modset_ack(ss[to], &status, reason, sizeof(reason)))
            if (rnet_session_rb_last_take_from(ss[to]) == from && status == 7 && !strcmp(reason, tag))
                return 1;
    }
    return 0;
}

static int sessions_running(RNetSession *ss[3])
{
    int i, k;
    for (i = 0; i < 3000; ++i)
    {
        int all = 1;
        for (k = 0; k < 3; ++k)
        {
            rnet_session_pump(ss[k]);
            if (!rnet_session_is_running(ss[k])) all = 0;
        }
        if (all) return 1;
        rnet_os_sleep_micros(2000);
    }
    return 0;
}

static void scenario_1(void)
{
    int g;
    RNetHostIceStatus hs;
    printf("--- 1: waiting room over ICE ---\n");
    setup();
    check(wait_completed(), "1: host and both guests COMPLETED");
    for (g = 0; g < 50; ++g) step(); /* the pump after COMPLETED sends the report */
    rnet_host_ice_status(g_n[0].h, &hs);
    check(hs.role == 1 && hs.peer_count == 2 && hs.completed == 2, "1: host serves two peers, both completed");
    for (g = 1; g <= 2; ++g)
    {
        char what[96];
        RNetHostIceStatus st;
        int ok = 0, i;
        rnet_host_ice_status(g_n[g].h, &st);
        for (i = 0; i < g_n[g].nreports; ++i)
            if (strstr(g_n[g].reports[i], "\"path\":\"direct\"") &&
                strstr(g_n[g].reports[i], "\"ice\":\"host\"")) ok = 1;
        snprintf(what, sizeof(what), "1: guest %d sent path_report direct with ice=host", g);
        check(ok, what);
        check(st.peer_count == 1 && st.peer[0].state == (int)RNET_ICE_STATE_COMPLETED,
              "1: guest has one agent (to the host), COMPLETED");
    }
    {
        /* A duplicate offer for the live negotiation must not touch the link. */
        RNetHostIceStatus after;
        int i;
        for (i = 0; i < 500; ++i) step();
        rnet_host_ice_status(g_n[0].h, &after);
        check(after.completed == 2, "1: links stay COMPLETED while idle");
    }
    {
        /* The waiting room's latency is the link's own round trip, not a
         * ping through the lobby server: on loopback it is a few ms. */
        int k, r;
        for (k = 1; k <= 2; ++k)
        {
            r = rnet_host_ice_peer_rtt_ms(g_n[0].h, g_guest_slot[k]);
            printf("  host -> guest %d rtt %d ms\n", k, r);
            check(r >= 0 && r < 50, "1: host measures each guest's direct round trip");
            r = rnet_host_ice_peer_rtt_ms(g_n[k].h, 0); /* the host's seat */
            printf("  guest %d -> host rtt %d ms\n", k, r);
            check(r >= 0 && r < 50, "1: a guest measures its direct round trip to the host");
        }
        check(rnet_host_ice_peer_rtt_ms(g_n[0].h, 7) == -1, "1: no agent, no round trip");
    }
    teardown();
}

static void scenario_2(void)
{
    RNetHostIceStatus before, after;
    int i;
    printf("--- 2: refused and held signals ---\n");
    setup();
    check(wait_completed(), "2: waiting room connected");
    rnet_host_ice_status(g_n[0].h, &before);
    /* guest accepts the host only */
    check(rnet_host_ice_push_signal(g_n[1].h, g_n[2].pid, 2, RNET_LOBBY_SIG_HOSTICE_BASE + 1, 1, "x") == -1,
          "2: a guest refuses a signal from another guest");
    /* bad types and unseated senders */
    check(rnet_host_ice_push_signal(g_n[0].h, g_n[1].pid, 1, RNET_LOBBY_SIG_HOSTICE_BASE + 6, 1, "") == -1,
          "2: SET_CONTROLLING is refused (roles are fixed)");
    check(rnet_host_ice_push_signal(g_n[0].h, g_n[1].pid, 1, RNET_LOBBY_SIG_HOSTICE_BASE + 2, 1, "") == -1,
          "2: a REMOTE_* wire type is refused (only LOCAL_* is ever sent)");
    check(rnet_host_ice_push_signal(g_n[0].h, g_n[1].pid, 1, 120 + 1, 1, "x") == -1,
          "2: a type outside the range is refused");
    check(rnet_host_ice_push_signal(g_n[0].h, g_n[1].pid, -1, RNET_LOBBY_SIG_HOSTICE_BASE + 3, 1, "x") == -1,
          "2: an unseated sender (slot -1) is refused");
    /* a signal from the right player at the WRONG seat is held, never applied */
    check(rnet_host_ice_push_signal(g_n[0].h, g_n[1].pid, 2, RNET_LOBBY_SIG_HOSTICE_BASE + 1, 77,
                                    "v=0 bogus") == 0,
          "2: wrong-seat signal accepted for holding");
    check(rnet_host_ice_held_count(g_n[0].h, g_n[1].pid) == 1, "2: it is held, not delivered");
    /* a peer with no agent yet: held in its own bucket */
    check(rnet_host_ice_push_signal(g_n[0].h, "player-late", 3, RNET_LOBBY_SIG_HOSTICE_BASE + 1, 5, "a") == 0 &&
          rnet_host_ice_push_signal(g_n[0].h, "player-late", 3, RNET_LOBBY_SIG_HOSTICE_BASE + 3, 5, "b") == 0 &&
          rnet_host_ice_push_signal(g_n[0].h, "player-other", 4, RNET_LOBBY_SIG_HOSTICE_BASE + 1, 6, "c") == 0,
          "2: signals for peers without agents are held");
    check(rnet_host_ice_held_count(g_n[0].h, "player-late") == 2 &&
          rnet_host_ice_held_count(g_n[0].h, "player-other") == 1,
          "2: each sender has its own bucket (no cross-eviction)");
    for (i = 0; i < 24; ++i)
        (void)rnet_host_ice_push_signal(g_n[0].h, "player-late", 3, RNET_LOBBY_SIG_HOSTICE_BASE + 3, 5, "n");
    check(rnet_host_ice_held_count(g_n[0].h, "player-late") == 24 &&
          rnet_host_ice_held_count(g_n[0].h, "player-other") == 1,
          "2: a full bucket drops the newest and leaves other senders alone");
    for (i = 0; i < 500; ++i) step();
    rnet_host_ice_status(g_n[0].h, &after);
    check(after.completed == before.completed && after.completed == 2,
          "2: none of that disturbed a live link");
    check(after.rejected > before.rejected, "2: refusals are counted");
    teardown();
}

static void scenario_3(void)
{
    RNetHostIceStatus hs;
    int i;
    printf("--- 3: a seat move rebuilds only that pair ---\n");
    setup();
    check(wait_completed(), "3: waiting room connected");
    rnet_host_ice_status(g_n[0].h, &hs);
    {
        int neg_g1 = 0, k;
        for (k = 0; k < hs.peer_count; ++k)
            if (!strcmp(hs.peer[k].player_id, g_n[1].pid)) neg_g1 = hs.peer[k].negotiation;
        g_guest_slot[2] = 3; /* guest 2 moves seat 2 -> 3 (both tables change) */
        g_n[0].nreports = g_n[1].nreports = g_n[2].nreports = 0;
        check(wait_completed(), "3: guest 2 reconnects at its new seat");
        for (i = 0; i < 50; ++i) step();
        rnet_host_ice_status(g_n[0].h, &hs);
        for (k = 0; k < hs.peer_count; ++k)
            if (!strcmp(hs.peer[k].player_id, g_n[1].pid))
                check(hs.peer[k].negotiation == neg_g1, "3: guest 1's pair was not renegotiated");
            else
                check(hs.peer[k].slot == 3 && hs.peer[k].state == (int)RNET_ICE_STATE_COMPLETED,
                      "3: host serves guest 2 at seat 3, COMPLETED");
        for (i = 0, k = 0; i < g_n[2].nreports; ++i)
            if (strstr(g_n[2].reports[i], "\"direct\"")) k = 1;
        check(k, "3: guest 2 reported direct again after the move");
    }
    teardown();
}

static void scenario_4(void)
{
    RNetSession *ss[3];
    RNetIceAdoptSeat seats[2];
    RNetIceAgent *host_dead, *a1, *a2, *g1, *g2;
    int i;
    printf("--- 4: waiting room -> match handover after an idle gap ---\n");
    setup();
    check(wait_completed(), "4: waiting room connected");
    /* The lobby pump keeps running while the players sit in the room; nothing
     * else happens for 21 s. */
    for (i = 0; i < 21000 / 20; ++i)
    {
        step();
        rnet_os_sleep_micros(18000);
    }
    {
        RNetHostIceStatus hs;
        rnet_host_ice_status(g_n[0].h, &hs);
        check(hs.completed == 2, "4: still COMPLETED after a 21 s idle gap");
    }
    /* not-connected seats are never handed over */
    host_dead = rnet_host_ice_take_completed(g_n[0].h, "player-nobody");
    check(host_dead == NULL, "4: take_completed returns NULL for an unknown peer");

    a1 = rnet_host_ice_take_completed(g_n[0].h, g_n[1].pid);
    a2 = rnet_host_ice_take_completed(g_n[0].h, g_n[2].pid);
    g1 = rnet_host_ice_take_completed(g_n[1].h, g_n[0].pid);
    g2 = rnet_host_ice_take_completed(g_n[2].h, g_n[0].pid);
    check(a1 && a2 && g1 && g2, "4: four COMPLETED agents taken");
    check(rnet_host_ice_take_completed(g_n[0].h, g_n[1].pid) == NULL, "4: an agent is taken once");

    ss[0] = mk_session(0); ss[1] = mk_session(1); ss[2] = mk_session(2);
    check(ss[0] && ss[1] && ss[2], "4: three sessions created");
    seats[0].slot = 1; seats[0].agent = a1;
    seats[1].slot = 1; seats[1].agent = a2; /* duplicate slot */
    check(rnet_session_start_ice_hub_adopt(ss[0], seats, 2) == -1, "4: duplicate seat refused, nothing adopted");
    seats[1].slot = 2;
    check(rnet_session_start_ice_hub_adopt(ss[0], seats, 2) == 0, "4: host adopts both agents (mask 0x6)");
    check(rnet_session_adopt_ice_agent(ss[1], g1) == 0, "4: guest 1 adopts its agent");
    check(rnet_session_adopt_ice_agent(ss[2], g2) == 0, "4: guest 2 adopts its agent");
    check(rnet_session_ice_state(ss[0]) == RNET_ICE_STATE_COMPLETED &&
          rnet_session_ice_state(ss[1]) == RNET_ICE_STATE_COMPLETED,
          "4: sessions report COMPLETED with no renegotiation");
    check(sessions_running(ss), "4: all three sessions reach RUNNING over the adopted agents");
    check(ack_arrives(ss, 1, 2, "after-gap-g1-g2"), "4: guest 1 datagram reaches guest 2 through the hub");
    check(ack_arrives(ss, 2, 1, "after-gap-g2-g1"), "4: guest 2 datagram reaches guest 1 through the hub");
    for (i = 0; i < 3; ++i) rnet_session_destroy(ss[i]);
    teardown();
}

int main(void)
{
    rnet_os_startup();
    scenario_1();
    scenario_2();
    scenario_3();
    scenario_4();
    if (g_failures)
    {
        printf("host_ice_test: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    printf("host_ice_test: ok\n");
    return 0;
}
