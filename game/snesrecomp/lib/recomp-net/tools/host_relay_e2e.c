/*
 * host_relay_e2e -- two processes, one real lobby server, the host-relay
 * contract end to end (recomp-net-server WS_LOBBY.md "Host relay"; the
 * client half is recomp_net/host_relay.h driven from rnet_lobby_pump).
 *
 * Run a local server (INPUT_RELAY_ALLOW_LOOPBACK=1, PUBLIC_HOST=127.0.0.1),
 * then:
 *
 *   RNET_LOBBY_URL=ws://127.0.0.1:8765 RNET_HOST_RELAY_NO_ROUTER=1 \
 *   RNET_HOST_RELAY_ENDPOINT=127.0.0.1:7777 host_relay_e2e host   &
 *   RNET_LOBBY_URL=ws://127.0.0.1:8765 host_relay_e2e guest
 *
 * The host creates a room asking for the host relay (the default), holds
 * UDP 7777 and advertises RNET_HOST_RELAY_ENDPOINT; the guest joins, probes
 * it, reports; the host sees the proof on its seat row and starts; both
 * expect `launch` transport "host" (JoinInfo.transport_host == 1), the host
 * with peer empty (accept-first), the guest dialling 127.0.0.1:7777.
 *
 * With RNET_HOST_RELAY_ENDPOINT=127.0.0.1:1 (nothing listens) the guest's
 * probe fails and the server must fall back to its relay: both expect
 * transport_host == 0 and force_input_relay == 1. Pass `expect-sfu` as the
 * second argument for that run.
 *
 * Exit 0 when every expectation held; the last line says which failed.
 */
#include "recomp_net/host_relay.h"
#include "recomp_net/lobby_client.h"
#include "recomp_net/address.h"
#include "platform/rnet_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *k_game = "host-relay-e2e";
static const char *k_ver = "1";

static int fail(const char *what)
{
    fprintf(stderr, "e2e FAIL: %s\n", what);
    return 1;
}

static void pump_ms(int ms)
{
    const rnet_u64 until = rnet_os_monotonic_ms() + (rnet_u64)ms;
    while (rnet_os_monotonic_ms() < until) {
        rnet_lobby_pump();
        rnet_os_sleep_micros(5000);
    }
}

static int wait_until(int (*pred)(void), int timeout_ms, const char *what)
{
    const rnet_u64 until = rnet_os_monotonic_ms() + (rnet_u64)timeout_ms;
    while (rnet_os_monotonic_ms() < until) {
        rnet_lobby_pump();
        if (pred()) return 1;
        rnet_os_sleep_micros(5000);
    }
    fprintf(stderr, "e2e: timed out waiting for %s\n", what);
    return 0;
}

static int p_ready(void) { return rnet_lobby_ready(); }
static int p_two_members(void) { return rnet_lobby_member_count() >= 2; }
static int p_launch(void) { return rnet_lobby_launch_pending(); }
static int p_list(void) { return rnet_lobby_list_count() > 0; }

static int p_guest_reported(void)
{
    int i;
    const int n = rnet_lobby_member_count();
    for (i = 0; i < n; ++i) {
        RNetLobbyMember m;
        if (rnet_lobby_member_get(i, &m) && !rnet_lobby_member_is_host(&m) &&
            m.path[0] && m.path_fresh)
            return 1;
    }
    return 0;
}

static int p_host_advertised(void)
{
    RNetHostRelayStatus st;
    return rnet_lobby_host_relay_status(&st) && st.role == 1 && st.advertised[0];
}

static int p_guest_probed(void)
{
    RNetHostRelayStatus st;
    return rnet_lobby_host_relay_status(&st) && st.role == 2 && st.last_report[0];
}

int main(int argc, char **argv)
{
    const char *role = argc > 1 ? argv[1] : "";
    const int expect_sfu = argc > 2 && strcmp(argv[2], "expect-sfu") == 0;
    const int is_host = strcmp(role, "host") == 0;
    RNetLobbyConfig cfg;
    RNetLobbyMatchCaps caps;
    RNetLobbyJoinInfo join;
    char name[32];

    if (!is_host && strcmp(role, "guest") != 0) {
        fprintf(stderr, "usage: host_relay_e2e host|guest [expect-sfu]\n");
        return 2;
    }
    rnet_os_startup();
    memset(&cfg, 0, sizeof(cfg));
    cfg.game_name = k_game;
    cfg.game_version = k_ver;
    cfg.platform = "test";
    cfg.max_players = 4;
    rnet_lobby_configure(&cfg);
    snprintf(name, sizeof(name), "e2e-%s", role);
    rnet_lobby_set_display_name(name);
    if (rnet_lobby_connect(NULL) != 0) return fail("connect");
    if (!wait_until(p_ready, 5000, "welcome")) return fail("welcome");

    memset(&caps, 0, sizeof(caps));
    caps.valid = 1;
    caps.input_delay = 6;
    caps.rollback = 1;
    caps.relay_host = rnet_lobby_relay_host_pref();

    if (is_host) {
        if (rnet_lobby_create("e2e room", k_game, k_ver, "", "0.0.0.0:7777", &caps, 2) != 0)
            return fail("create");
        if (!wait_until(p_host_advertised, 8000, "set_host_endpoint")) return fail("advertise");
        {
            RNetHostRelayStatus st;
            rnet_lobby_host_relay_status(&st);
            printf("host: advertised %s via %s (port %u)\n", st.advertised, st.port.how,
                   (unsigned)st.port.local_port);
        }
        if (!wait_until(p_two_members, 30000, "the guest's seat")) return fail("guest seat");
        if (!wait_until(p_guest_reported, 15000, "the guest's path report"))
            return fail("guest path_report on the seat row");
        {
            RNetLobbyMember m;
            int i;
            for (i = 0; i < rnet_lobby_member_count(); ++i)
                if (rnet_lobby_member_get(i, &m) && !rnet_lobby_member_is_host(&m))
                    printf("host: guest %s path=%s fresh=%d\n", m.display_name, m.path, m.path_fresh);
            if (expect_sfu && strcmp(m.path, "fail") != 0) return fail("expected the guest's probe to fail");
            if (!expect_sfu && strcmp(m.path, "direct") != 0) return fail("expected path direct");
        }
        pump_ms(300);
        if (rnet_lobby_request_start(&caps) != 0) return fail("start");
    } else {
        rnet_lobby_request_list();
        if (!wait_until(p_list, 8000, "the room in the list")) return fail("list");
        {
            RNetLobbyRow row;
            int i, found = 0;
            for (i = 0; i < rnet_lobby_list_count(); ++i)
                if (rnet_lobby_list_get(i, &row) && strcmp(row.game_name, k_game) == 0) { found = 1; break; }
            if (!found) return fail("room not listed");
            if (rnet_lobby_join(row.lobby_id, "", "0.0.0.0:7778") != 0) return fail("join");
        }
        if (!wait_until(p_guest_probed, 15000, "the probe result")) return fail("probe");
        {
            RNetHostRelayStatus st;
            rnet_lobby_host_relay_status(&st);
            printf("guest: probed %s -> %s\n", st.probed, st.last_report);
            if (expect_sfu && strcmp(st.last_report, "fail") != 0) return fail("expected probe fail");
            if (!expect_sfu && strcmp(st.last_report, "direct") != 0) return fail("expected probe direct");
        }
        (void)rnet_lobby_set_ready(1);
    }

    if (!wait_until(p_launch, 30000, "launch")) return fail("launch");
    memset(&join, 0, sizeof(join));
    if (!rnet_lobby_try_fill_launch(&join)) return fail("fill_launch");
    printf("%s: launch transport_host=%d force_input_relay=%d bind=%s peer=%s host_endpoint=%s\n",
           role, join.transport_host, join.force_input_relay, join.bind_hostport,
           join.peer_hostport, join.host_endpoint);
    if (expect_sfu) {
        if (join.transport_host) return fail("expected an SFU launch");
        if (!join.force_input_relay) return fail("expected force_input_relay on the SFU launch");
    } else {
        if (!join.transport_host) return fail("expected transport host");
        if (join.force_input_relay) return fail("force_input_relay must be 0 on a host launch");
        if (is_host) {
            if (join.peer_hostport[0]) return fail("host must accept-first (peer empty)");
            if (!strstr(join.bind_hostport, ":7777")) return fail("host binds its advertised port");
        } else {
            if (strcmp(join.peer_hostport, "127.0.0.1:7777") != 0) return fail("guest dials the host's endpoint");
        }
    }
    /* The host's waiting-room socket must be gone so the game can bind 7777. */
    if (is_host && !expect_sfu) {
        RNetHostRelayStatus st;
        rnet_lobby_host_relay_status(&st);
        if (rnet_udp_port_available(7777) != 1) return fail("port 7777 still held after launch");
    }
    rnet_lobby_leave();
    pump_ms(200);
    rnet_lobby_disconnect();
    printf("%s: ok\n", role);
    return 0;
}
