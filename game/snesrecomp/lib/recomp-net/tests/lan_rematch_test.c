/*
 * A LAN / Direct IP room across a soft return: the socket lifecycle a rematch
 * walks, over the real RNETDJ1 waiting room on loopback, single-threaded.
 *
 * What a rematch does to the room, in order: START goes out, BOTH sockets
 * close (the game session takes the port), the match runs, and then the host
 * re-opens its listener while the guest -- which has no socket left -- asks
 * for its seat again. This proves the pieces that makes work:
 *
 *   1. the non-blocking join (join_begin / join_poll) seats a guest while the
 *      caller keeps pumping the host on the same thread;
 *   2. START carries the session id the host allocated, so both peers launch
 *      the same fresh id and a rematch does not reuse the last one;
 *   3. a guest asking while the host is NOT listening yet keeps asking
 *      (PENDING), and is seated once the host re-opens with the seat freed;
 *   4. a seated guest whose JOIN_OK was lost and asks again is answered, not
 *      refused its own seat as "full"; another guest IS refused "full";
 *   5. a LEAVE from anyone but the seated guest does not empty the seat;
 *   6. the registry file carries the session id, and a file written without
 *      one reads as 0.
 */
#include "recomp_net/lan_direct.h"
#include "recomp_net/lan_lobby.h"

#include "platform/rnet_platform.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
static unsigned test_pid(void) { return (unsigned)_getpid(); }
#else
#include <unistd.h>
static unsigned test_pid(void) { return (unsigned)getpid(); }
#endif

static void nap_ms(unsigned ms) { rnet_os_sleep_micros(ms * 1000u); }

static int g_failures;

static void ck(int cond, const char *what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_failures++; }
}

/* Pump the host and poll the joining guest until the join settles (or ~2 s). */
static int join_while_pumping(RNetLanDirectHost *h, RNetLanLobby *hroom,
                              RNetLanDirectGuest *g, RNetLanLobby *groom)
{
    int i;
    int rc = RNET_LAN_DIRECT_PENDING;
    for (i = 0; i < 2000 && rc == RNET_LAN_DIRECT_PENDING; ++i) {
        if (h)
            (void)rnet_lan_direct_host_pump(h, hroom, NULL);
        rc = rnet_lan_direct_guest_join_poll(g, groom);
        if (rc == RNET_LAN_DIRECT_PENDING)
            nap_ms(1);
    }
    return rc;
}

/* Pump both until the guest sees an event (1 = START), or ~1 s. */
static int guest_event(RNetLanDirectHost *h, RNetLanLobby *hroom,
                       RNetLanDirectGuest *g, RNetLanLobby *groom)
{
    int i;
    for (i = 0; i < 1000; ++i) {
        int ev;
        (void)rnet_lan_direct_host_pump(h, hroom, NULL);
        ev = rnet_lan_direct_guest_pump(g, groom, NULL);
        if (ev != 0)
            return ev;
        nap_ms(1);
    }
    return 0;
}

static void settle_host(RNetLanDirectHost *h, RNetLanLobby *hroom)
{
    int i;
    for (i = 0; i < 50; ++i) {
        (void)rnet_lan_direct_host_pump(h, hroom, NULL);
        nap_ms(1);
    }
}

static void registry_case(unsigned port)
{
    char path[256];
    RNetLanLobby w;
    RNetLanLobby r;
    FILE *f;
    printf("  registry file: the session id round-trips; absent reads 0\n");
    snprintf(path, sizeof(path), "lan_rematch_test_%u.txt", port);
    memset(&w, 0, sizeof(w));
    snprintf(w.name, sizeof(w.name), "%s", "Room");
    snprintf(w.game, sizeof(w.game), "%s", "TestGame");
    snprintf(w.game_version, sizeof(w.game_version), "%s", "1.0.0");
    snprintf(w.endpoint, sizeof(w.endpoint), "127.0.0.1:%u", port);
    snprintf(w.host_name, sizeof(w.host_name), "%s", "Hostess");
    w.input_delay = 3;
    w.session_id = 3000000001u;
    ck(rnet_lan_lobby_publish(path, &w) == RNET_LAN_LOBBY_OK, "publish");
    ck(rnet_lan_lobby_read(path, "TestGame", "1.0.0", &r) == RNET_LAN_LOBBY_OK,
       "read back");
    ck(r.session_id == 3000000001u, "the session id survives the file");
    ck(rnet_lan_lobby_set_started(path, 1) == RNET_LAN_LOBBY_OK &&
           rnet_lan_lobby_read(path, NULL, NULL, &r) == RNET_LAN_LOBBY_OK &&
           r.started && r.session_id == 3000000001u,
       "and a read-modify-write (set_started) keeps it");

    /* A V3 writer's file: the same lines, no session line. */
    f = fopen(path, "wb");
    ck(f != NULL, "write an old-format file");
    if (f) {
        fprintf(f, "RNET_LAN_LOBBY_3\nRoom\nTestGame\n1.0.0\n127.0.0.1:%u\n"
                   "Hostess\n\n\n1\n0\n3\n0\n4\n", port);
        fclose(f);
    }
    ck(rnet_lan_lobby_read(path, "TestGame", "1.0.0", &r) == RNET_LAN_LOBBY_OK,
       "an old-format file still reads");
    ck(r.session_id == 0u, "with no session id (0: the caller decides)");
    ck(r.started == 1 && r.input_delay == 3, "and its other fields intact");
    remove(path);
}

int main(void)
{
    RNetLanLobby hroom;
    RNetLanLobby groom;
    RNetLanLobby other_room;
    RNetLanDirectHost *host = NULL;
    RNetLanDirectGuest *guest = NULL;
    RNetLanDirectGuest *other = NULL;
    char bind_h[64];
    char bind_g[64];
    int rc;
    const unsigned port = 42600u + (test_pid() % 700u) * 3u;

    memset(&hroom, 0, sizeof(hroom));
    snprintf(hroom.game, sizeof(hroom.game), "%s", "TestGame");
    snprintf(hroom.game_version, sizeof(hroom.game_version), "%s", "1.0.0");
    snprintf(hroom.host_name, sizeof(hroom.host_name), "%s", "Hostess");
    snprintf(bind_h, sizeof(bind_h), "127.0.0.1:%u", port);
    snprintf(bind_g, sizeof(bind_g), "127.0.0.1:%u", port + 1u);
    snprintf(hroom.endpoint, sizeof(hroom.endpoint), "%s", bind_h);
    hroom.session_variant = 37;
    snprintf(hroom.mod_set, sizeof(hroom.mod_set), "test.coop@1/coop player1=zero");

    /* --- 1. the non-blocking join, single-threaded -------------------- */
    printf("  non-blocking join while the host is pumped on the same thread\n");
    if (rnet_lan_direct_host_open(&host, bind_h, &hroom) != RNET_LAN_DIRECT_OK) {
        printf("FAIL: host open\n");
        return 1;
    }
    rc = rnet_lan_direct_guest_join_begin(bind_h, "TestGame", "1.0.0", "",
                                          "Guesty", bind_g, &guest);
    ck(rc == RNET_LAN_DIRECT_OK && guest != NULL, "join_begin");
    if (!guest) {
        rnet_lan_direct_host_close(&host);
        return 1;
    }
    ck(rnet_lan_direct_guest_pump(guest, &groom, NULL) == 0,
       "an unseated guest's pump reports nothing");
    ck(rnet_lan_direct_guest_send_chat(guest, "g", "hi") != RNET_LAN_DIRECT_OK,
       "and cannot talk in a room it is not in");
    rc = join_while_pumping(host, &hroom, guest, &groom);
    ck(rc == RNET_LAN_DIRECT_OK, "seated");
    ck(strcmp(hroom.joiner_name, "Guesty") == 0, "the host holds the seat");
    ck(strcmp(groom.host_name, "Hostess") == 0, "the guest has the room");
    ck(groom.session_variant == 37, "JOIN_OK carries the opaque engine variant");
    ck(!strcmp(groom.mod_set, hroom.mod_set), "JOIN_OK carries the host mod settings");
    snprintf(hroom.mod_set, sizeof(hroom.mod_set), "test.view@1/wide aspect=21:9");
    hroom.session_variant = 9;
    ck(rnet_lan_direct_host_notify_caps(host, &hroom) == RNET_LAN_DIRECT_OK,
       "host changes the session variant in the waiting room");
    {
        int i;
        for (i = 0; i < 100 && groom.session_variant != 9; ++i) {
            (void)rnet_lan_direct_guest_pump(guest, &groom, NULL);
            nap_ms(1);
        }
    }
    ck(groom.session_variant == 9, "CAPS updates the guest's variant");
    ck(!strcmp(groom.mod_set, hroom.mod_set), "CAPS updates the guest mod settings");
    ck(rnet_lan_direct_guest_join_poll(guest, &groom) == RNET_LAN_DIRECT_OK,
       "polling a seated handle stays OK");
    /* The seated guest asks again from the same address -- what a guest does
     * when the JOIN_OK was lost (here: its handle, and the answer, dropped). */
    rnet_lan_direct_guest_close(&guest);
    rc = rnet_lan_direct_guest_join_begin(bind_h, "TestGame", "1.0.0", "",
                                          "Guesty", bind_g, &guest);
    rc = rc == RNET_LAN_DIRECT_OK
             ? join_while_pumping(host, &hroom, guest, &groom) : rc;
    ck(rc == RNET_LAN_DIRECT_OK,
       "the seated guest asking again is answered, not refused full");
    if (!guest) {
        rnet_lan_direct_host_close(&host);
        return 1;
    }

    /* --- 4. idempotent re-ask; a second guest is refused -------------- */
    printf("  a second guest is refused; a stray LEAVE is ignored\n");
    rc = rnet_lan_direct_guest_join_begin(bind_h, "TestGame", "1.0.0", "",
                                          "Intruder", NULL, &other);
    ck(rc == RNET_LAN_DIRECT_OK, "second join_begin");
    rc = join_while_pumping(host, &hroom, other, &other_room);
    ck(rc == RNET_LAN_DIRECT_ERR_FULL, "a second guest is refused full");
    ck(rnet_lan_direct_guest_join_poll(other, &other_room) ==
           RNET_LAN_DIRECT_ERR_FULL,
       "and the refusal sticks");
    /* --- 5. only the seated guest can LEAVE --------------------------- */
    ck(rnet_lan_direct_guest_leave(other) == RNET_LAN_DIRECT_OK,
       "an unseated leave is a no-op");
    rnet_lan_direct_guest_close(&other);
    /* A raw LEAVE datagram from a socket that holds no seat. */
    {
        rnet_socket raw = rnet_os_socket_create_dgram();
        struct sockaddr_in dst;
        const char leave[] = "RNETDJ1\nLEAVE\n";
        ck(rnet_os_socket_valid(raw) &&
               rnet_os_resolve_sockaddr("127.0.0.1", (rnet_u16)port, &dst) == 0 &&
               rnet_os_sendto(raw, leave, sizeof(leave) - 1, &dst) > 0,
           "send a stray LEAVE");
        settle_host(host, &hroom);
        rnet_os_socket_destroy(&raw);
    }
    ck(strcmp(hroom.joiner_name, "Guesty") == 0,
       "a LEAVE from anyone but the seated guest does not empty the seat");

    /* --- 2. START carries the session id ------------------------------ */
    printf("  START carries the host's session id\n");
    hroom.started = 1;
    hroom.session_id = 0x7000001u;
    hroom.session_variant = 42;
    snprintf(hroom.mod_set, sizeof(hroom.mod_set), "test.view@1/wide aspect=32:9");
    ck(rnet_lan_direct_host_notify_start(host, &hroom) == RNET_LAN_DIRECT_OK,
       "notify_start");
    ck(guest_event(host, &hroom, guest, &groom) == 1, "the guest hears START");
    ck(groom.session_id == 0x7000001u, "with the host's session id");
    ck(groom.session_variant == 42, "START settles the variant even if the last CAPS was lost");
    ck(!strcmp(groom.mod_set, hroom.mod_set), "START settles mod settings even if CAPS was lost");

    /* --- 3. the soft return: both sockets closed, the guest asks first -- */
    printf("  rematch: the guest asks before the host listens again\n");
    rnet_lan_direct_guest_close(&guest);
    rnet_lan_direct_host_close(&host);
    rc = rnet_lan_direct_guest_join_begin(bind_h, "TestGame", "1.0.0", "",
                                          "Guesty", bind_g, &guest);
    ck(rc == RNET_LAN_DIRECT_OK, "rejoin begin with nobody listening");
    {
        int i;
        int pending = 1;
        for (i = 0; i < 20; ++i) {
            if (rnet_lan_direct_guest_join_poll(guest, &groom) !=
                RNET_LAN_DIRECT_PENDING)
                pending = 0;
            nap_ms(5);
        }
        ck(pending, "keeps asking (PENDING) while the host is away");
    }
    /* What the backend's prepare_rematch does: re-open with the seat freed. */
    hroom.started = 0;
    hroom.joiner_name[0] = '\0';
    ck(rnet_lan_direct_host_open(&host, bind_h, &hroom) == RNET_LAN_DIRECT_OK,
       "the host re-opens");
    rc = join_while_pumping(host, &hroom, guest, &groom);
    ck(rc == RNET_LAN_DIRECT_OK, "the guest is seated again");
    ck(strcmp(hroom.joiner_name, "Guesty") == 0, "in the freed seat");
    hroom.started = 1;
    hroom.session_id = 0x7000002u;
    ck(rnet_lan_direct_host_notify_start(host, &hroom) == RNET_LAN_DIRECT_OK,
       "the rematch START reaches the guest's new socket");
    ck(guest_event(host, &hroom, guest, &groom) == 1, "heard");
    ck(groom.session_id == 0x7000002u, "with the rematch's own session id");

    rnet_lan_direct_guest_close(&guest);
    rnet_lan_direct_host_close(&host);

    /* A seat still marked taken refuses the returning guest: the reason the
     * host MUST free it when it re-opens. */
    printf("  a re-open that keeps the old seat refuses the returning guest\n");
    hroom.started = 0;
    snprintf(hroom.joiner_name, sizeof(hroom.joiner_name), "%s", "Guesty");
    ck(rnet_lan_direct_host_open(&host, bind_h, &hroom) == RNET_LAN_DIRECT_OK,
       "re-open with the stale seat");
    rc = rnet_lan_direct_guest_join_begin(bind_h, "TestGame", "1.0.0", "",
                                          "Guesty", bind_g, &guest);
    rc = rc == RNET_LAN_DIRECT_OK
             ? join_while_pumping(host, &hroom, guest, &groom) : rc;
    ck(rc == RNET_LAN_DIRECT_ERR_FULL, "refused full");
    rnet_lan_direct_guest_close(&guest);
    rnet_lan_direct_host_close(&host);

    registry_case(port);

    if (g_failures == 0) {
        printf("lan_rematch_test: ok\n");
        return 0;
    }
    fprintf(stderr, "lan_rematch_test: %d failure(s)\n", g_failures);
    return 1;
}
