#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
/*
 * The real lobby client's host side of "host relay over ICE", end to end on
 * loopback: signal dispatch (rnet_lobby_client.c's own `signal` handler),
 * waiting-room agents, the `launch` handover, and sessions adopting the
 * agents. Requires RNET_ENABLE_ICE.
 *
 * The lobby client is a process-wide singleton, so this process is the HOST;
 * the two guests are RNetHostIce instances driven with the view a guest
 * client builds, and what they say reaches the host through
 * handle_server_json as a genuine {"op":"signal",...} frame with the sender's
 * player id -- the same path a server relay takes. What the host sends goes
 * out through the real rnet_lobby_send_signal_to; this test renames only the
 * WebSocket framing calls so the frame is captured instead of written (no
 * server is involved, so no server behaviour is claimed here).
 *
 *  A  host launch: both guests COMPLETED -> launch -> take_hub returns two
 *     COMPLETED agents with the guests' lobby seats; the host session adopts
 *     them, the guests adopt theirs, all RUNNING, a datagram crosses guest ->
 *     guest through the hub.
 *  B  a launch with a seated guest that has no agent is refused with a
 *     reason naming it; nothing is half-adopted.
 */
#define rnet_ws_tx_queue_text test_ws_queue
#define rnet_ws_tx_flush test_ws_flush
#include "../src/lobby/rnet_lobby_client.c"
#undef rnet_ws_tx_queue_text
#undef rnet_ws_tx_flush

#include "recomp_net/recomp_net.h"
#include "platform/rnet_platform.h"

#include <stdio.h>
#include <string.h>

static int g_failures;
static void check(int cond, const char *what)
{
    if (cond) printf("PASS: %s\n", what);
    else { printf("FAIL: %s\n", what); g_failures++; }
}

typedef struct Out { char to[64]; int type, flag; char text[2048]; } Out;
static Out g_out[512];
static int g_outn;

/* A frame the client would have put on the wire: parse the signal back out. */
int test_ws_queue(RNetWsTx *tx, const char *text, int client_mask)
{
    Out *o;
    (void)tx; (void)client_mask;
    if (!strstr(text, "\"op\":\"signal\"") || g_outn >= 512) return 0;
    o = &g_out[g_outn++];
    rnet_lobby_json_get_str(text, "to_player_id", o->to, sizeof(o->to));
    o->type = rnet_lobby_json_get_int(text, "type", 0);
    o->flag = rnet_lobby_json_get_int(text, "flag", 0);
    rnet_lobby_json_get_str(text, "text", o->text, sizeof(o->text));
    return 0;
}
long test_ws_flush(RNetWsTx *tx, int fd) { (void)tx; (void)fd; return 0; }

static const char *PID[3] = {"h", "g1", "g2"};
static RNetHostIce *g_guest[3];

static int guest_send_signal(const char *to, int type, int flag, const char *text, void *ctx)
{
    static char esc[4608], js[5000];
    const int gi = *(int *)ctx;
    (void)to;
    rnet_lobby_json_escape(text, esc, sizeof(esc));
    snprintf(js, sizeof(js),
             "{\"op\":\"signal\",\"from_player_id\":\"%s\",\"type\":%d,\"flag\":%d,\"text\":\"%s\"}",
             PID[gi], type, flag, esc);
    handle_server_json(js);
    return 0;
}

static int guest_json(const char *json, void *ctx) { (void)json; (void)ctx; return 0; }

static int g_gi[3] = {0, 1, 2};

static void step(int nguests)
{
    int i, g;
    RNetHostIceView v;
    RNetHostIcePeer hp;
    host_ice_step();
    for (g = 1; g <= nguests; ++g)
    {
        memset(&v, 0, sizeof(v));
        hp.player_id = "h"; hp.slot = 0;
        v.active = 1; v.is_host = 0; v.local_slot = g;
        v.peers = &hp; v.peer_count = 1;
        v.stun_host = ""; v.bind_address = "127.0.0.1";
        v.send_signal = guest_send_signal; v.send_json = guest_json; v.ctx = &g_gi[g];
        rnet_host_ice_update(g_guest[g], &v);
    }
    /* host -> guests */
    {
        static Out batch[512];
        int n = g_outn;
        memcpy(batch, g_out, sizeof(Out) * (size_t)n);
        g_outn = 0;
        for (i = 0; i < n; ++i)
            for (g = 1; g <= nguests; ++g)
                if (!strcmp(batch[i].to, PID[g]))
                    (void)rnet_host_ice_push_signal(g_guest[g], "h", 0, batch[i].type,
                                                    batch[i].flag, batch[i].text);
    }
    rnet_os_sleep_micros(2000);
}

static const char *UPDATE2 =
    "{\"op\":\"lobby_update\",\"lobby_id\":\"L\",\"session_id\":3,"
    "\"host_player_id\":\"h\",\"player_count\":3,\"max_slots\":4,"
    "\"match_caps\":{\"v\":1,\"input_delay\":5,\"relay\":\"host\",\"relay_via\":\"ice\"},"
    "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\",\"ready\":true},"
    "{\"slot\":1,\"player_id\":\"g1\",\"display_name\":\"G1\",\"ready\":true},"
    "{\"slot\":2,\"player_id\":\"g2\",\"display_name\":\"G2\",\"ready\":true}]}";
/* the same room with a third guest who never connects */
static const char *UPDATE3 =
    "{\"op\":\"lobby_update\",\"lobby_id\":\"L\",\"session_id\":3,"
    "\"host_player_id\":\"h\",\"player_count\":4,\"max_slots\":4,"
    "\"match_caps\":{\"v\":1,\"input_delay\":5,\"relay\":\"host\",\"relay_via\":\"ice\"},"
    "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\",\"ready\":true},"
    "{\"slot\":1,\"player_id\":\"g1\",\"display_name\":\"G1\",\"ready\":true},"
    "{\"slot\":2,\"player_id\":\"g2\",\"display_name\":\"G2\",\"ready\":true},"
    "{\"slot\":3,\"player_id\":\"g3\",\"display_name\":\"Quiet\",\"ready\":true}]}";
static const char *LAUNCH2 =
    "{\"op\":\"launch\",\"ok\":true,\"lobby_id\":\"L\",\"session_id\":9,"
    "\"transport\":\"host\",\"relay_via\":\"ice\",\"host_endpoint\":\"\","
    "\"guest_endpoint\":\"\",\"player_count\":3,\"max_slots\":4,"
    "\"match_caps\":{\"v\":1,\"input_delay\":5,\"relay\":\"host\",\"relay_via\":\"ice\"},"
    "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\"},"
    "{\"slot\":1,\"player_id\":\"g1\",\"display_name\":\"G1\"},"
    "{\"slot\":2,\"player_id\":\"g2\",\"display_name\":\"G2\"}]}";

static const char *LAUNCH3 =
    "{\"op\":\"launch\",\"ok\":true,\"lobby_id\":\"L\",\"session_id\":9,"
    "\"transport\":\"host\",\"relay_via\":\"ice\",\"host_endpoint\":\"\","
    "\"guest_endpoint\":\"\",\"player_count\":4,\"max_slots\":4,"
    "\"match_caps\":{\"v\":1,\"input_delay\":5,\"relay\":\"host\",\"relay_via\":\"ice\"},"
    "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\"},"
    "{\"slot\":1,\"player_id\":\"g1\",\"display_name\":\"G1\"},"
    "{\"slot\":2,\"player_id\":\"g2\",\"display_name\":\"G2\"},"
    "{\"slot\":3,\"player_id\":\"g3\",\"display_name\":\"Quiet\"}]}";

static void host_room(const char *update)
{
    int i;
    for (i = 0; i < 3; ++i) { rnet_host_ice_destroy(&g_guest[i]); g_guest[i] = rnet_host_ice_create(); }
    rnet_host_ice_destroy(&g_host_ice);
    memset(&g_lc, 0, sizeof(g_lc));
    memset(&g_il, 0, sizeof(g_il));
    g_outn = 0;
    g_lc.fd = 1000;
    g_lc.connected = 1;
    g_lc.handshake_done = 1;   /* sends go through ws_send -> test_ws_queue */
    g_lc.in_lobby = 1;
    g_lc.is_host = 1;
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "h");
    snprintf(g_lc.host_player_id, sizeof(g_lc.host_player_id), "h");
    handle_server_json(update);
}

static int both_connected(void)
{
    int i;
    for (i = 0; i < 6000; ++i)
    {
        RNetHostIceStatus hs, a, b;
        step(2);
        rnet_host_ice_status(g_host_ice, &hs);
        rnet_host_ice_status(g_guest[1], &a);
        rnet_host_ice_status(g_guest[2], &b);
        if (hs.completed == 2 && a.completed == 1 && b.completed == 1) return 1;
    }
    return 0;
}

static void sample_local(rnet_u32 tick, RNetInputSample *out, void *ctx)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->size = 2; out->bytes[0] = (rnet_u8)tick; out->valid = 1;
}
static void publish(rnet_u32 t, const RNetInputSample *b, int n, void *c) { (void)t; (void)b; (void)n; (void)c; }

static RNetSession *mk_session(int slot)
{
    RNetConfig c;
    RNetHostVTable vt;
    rnet_config_init_defaults(&c);
    c.slot_count = 3; c.input_delay = 3; c.session_id = 9; c.local_slot = (rnet_u8)slot;
    memset(&vt, 0, sizeof(vt));
    vt.sample_local = sample_local; vt.publish = publish;
    return rnet_session_create(&c, &vt);
}

static void scenario_a(void)
{
    RNetLobbyIceSeat seats[RNET_HOST_ICE_MAX_PEERS];
    RNetIceAdoptSeat adopt[2];
    RNetSession *ss[3];
    RNetLobbyJoinInfo ji;
    int n, i, k, running = 0, ok = 0;
    char reason[64];
    rnet_u8 status = 0;

    printf("--- A: lobby launch -> take_hub -> sessions ---\n");
    host_room(UPDATE2);
    check(room_relays_via_ice(), "A: the room runs over ICE");
    check(both_connected(), "A: both guests connected through the lobby's signal handler");
    handle_server_json(LAUNCH2);
    check(rnet_lobby_launch_pending() == 1, "A: launch pending");
    check(rnet_lobby_try_fill_launch(&ji) == 1 && ji.transport_ice_hub == 1 && ji.transport_host == 1,
          "A: JoinInfo says transport_ice_hub (no endpoints needed)");
    check(ji.host_endpoint[0] == '\0', "A: and the host_endpoint is empty");
    for (i = 0; i < 200; ++i) step(2);   /* the next pumps must not tear the agents down */
    n = rnet_lobby_ice_take_hub(seats, RNET_HOST_ICE_MAX_PEERS);
    check(n == 2, "A: take_hub returns two seats");
    check(n == 2 && seats[0].lobby_slot == 1 && seats[1].lobby_slot == 2 &&
          !strcmp(seats[0].player_id, "g1") && !strcmp(seats[1].player_id, "g2") &&
          seats[0].agent && seats[1].agent, "A: seats carry lobby slot, player id, agent");
    check(rnet_lobby_ice_take_hub(seats, RNET_HOST_ICE_MAX_PEERS) == -1, "A: a handover is taken once");
    if (n != 2) return;
    ss[0] = mk_session(0); ss[1] = mk_session(1); ss[2] = mk_session(2);
    adopt[0].slot = seats[0].lobby_slot; adopt[0].agent = seats[0].agent;
    adopt[1].slot = seats[1].lobby_slot; adopt[1].agent = seats[1].agent;
    check(rnet_session_start_ice_hub_adopt(ss[0], adopt, 2) == 0, "A: host session adopts the hub agents");
    for (k = 1; k <= 2; ++k)
    {
        RNetIceAgent *a = rnet_host_ice_take_completed(g_guest[k], "h");
        check(a != NULL && rnet_session_adopt_ice_agent(ss[k], a) == 0, "A: guest session adopts its agent");
    }
    for (i = 0; i < 4000 && !running; ++i)
    {
        int all = 1;
        for (k = 0; k < 3; ++k) { rnet_session_pump(ss[k]); if (!rnet_session_is_running(ss[k])) all = 0; }
        running = all;
        rnet_os_sleep_micros(2000);
    }
    check(running, "A: all three sessions RUNNING over the handed-over agents");
    (void)rnet_session_send_modset_ack(ss[1], 7, "lobby-g1-g2");
    for (i = 0; i < 4000 && !ok; ++i)
    {
        for (k = 0; k < 3; ++k) rnet_session_pump(ss[k]);
        if ((i % 100) == 0) (void)rnet_session_send_modset_ack(ss[1], 7, "lobby-g1-g2");
        reason[0] = 0;
        while (rnet_session_take_modset_ack(ss[2], &status, reason, sizeof(reason)))
            if (rnet_session_rb_last_take_from(ss[2]) == 1 && !strcmp(reason, "lobby-g1-g2")) ok = 1;
        rnet_os_sleep_micros(2000);
    }
    check(ok, "A: a datagram from guest 1 reaches guest 2 through the host hub");
    for (k = 0; k < 3; ++k) rnet_session_destroy(ss[k]);
}

static void scenario_b(void)
{
    RNetLobbyIceSeat seats[RNET_HOST_ICE_MAX_PEERS];
    int i;
    printf("--- B: a seated guest with no agent refuses the launch ---\n");
    host_room(UPDATE3);
    /* g3 never signals; g1 and g2 connect */
    for (i = 0; i < 6000; ++i)
    {
        RNetHostIceStatus hs;
        step(2);
        rnet_host_ice_status(g_host_ice, &hs);
        if (hs.completed == 2) break;
    }
    handle_server_json(LAUNCH3);
    check(rnet_lobby_launch_pending() == 0, "B: the launch is refused");
    check(!strcmp(g_lc.join.last_error, "ice_not_connected"), "B: last_error = ice_not_connected");
    check(strstr(rnet_lobby_ice_launch_error(), "Quiet") != NULL, "B: the reason names the missing guest");
    check(rnet_lobby_ice_take_hub(seats, RNET_HOST_ICE_MAX_PEERS) == -1, "B: no smaller room is handed over");
}

int main(void)
{
    rnet_os_startup();
    setenv("RNET_HOST_ICE_BIND", "127.0.0.1", 1);
    setenv("RNET_HOST_ICE_NO_STUN", "1", 1);
    scenario_a();
    scenario_b();
    rnet_host_ice_destroy(&g_host_ice);
    if (g_failures) { printf("lobby_ice_launch_test: %d FAILURE(S)\n", g_failures); return 1; }
    printf("lobby_ice_launch_test: ok\n");
    return 0;
}
