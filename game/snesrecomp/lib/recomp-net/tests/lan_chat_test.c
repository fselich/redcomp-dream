/*
 * LAN lobby chat over the real RNETDJ1 waiting room.
 *
 * A host and a guest on loopback, joined for real. What this has to prove is
 * the ordering discipline, which is the only part that can go quietly wrong:
 *
 *   1. the HOST is the authority. A guest's line comes back stamped with the
 *      seat name the host holds, not one the guest supplied;
 *   2. the guest does NOT keep its own copy at send time -- it keeps the
 *      host's echo, so both sides hold the same lines in the same order;
 *   3. a line from anywhere other than the room is ignored;
 *   4. newlines cannot survive into a line, because the wire is newline
 *      delimited and a line that carried one could forge a datagram field.
 */
#include "recomp_net/lan_direct.h"
#include "recomp_net/lan_lobby.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
static unsigned test_pid(void) { return (unsigned)_getpid(); }
#else
#include <pthread.h>
#include <unistd.h>
static unsigned test_pid(void) { return (unsigned)getpid(); }
#endif

/* rnet_lan_direct_guest_join blocks until the host answers, and only
 * rnet_lan_direct_host_pump answers. One thread cannot do both, so the host is
 * pumped from a second one for the duration of the join and joined again
 * before anything else touches it -- after that the test is single-threaded
 * and the handles are used from one thread only, as the library expects. */
struct PumpArgs {
    RNetLanDirectHost *host;
    RNetLanLobby *room;
    volatile int stop;
};

static void pump_host_until_stop(struct PumpArgs *a)
{
    while (!a->stop)
        (void)rnet_lan_direct_host_pump(a->host, a->room, NULL);
}

#ifdef _WIN32
static DWORD WINAPI pump_thread(LPVOID p)
{
    pump_host_until_stop((struct PumpArgs *)p);
    return 0;
}
#else
static void *pump_thread(void *p)
{
    pump_host_until_stop((struct PumpArgs *)p);
    return NULL;
}
#endif

static int g_failures;

static void ck(int cond, const char *what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_failures++; }
}

/* Pump both ends a few times so a datagram has room to land. */
static void settle(RNetLanDirectHost *h, RNetLanDirectGuest *g,
                   RNetLanLobby *hroom, RNetLanLobby *groom)
{
    int i;
    for (i = 0; i < 200; ++i) {
        (void)rnet_lan_direct_host_pump(h, hroom, NULL);
        (void)rnet_lan_direct_guest_pump(g, groom, NULL);
    }
}

int main(void)
{
    RNetLanLobby hroom;
    RNetLanLobby groom;
    RNetLanDirectHost *host = NULL;
    RNetLanDirectGuest *guest = NULL;
    RNetLanChatLine line;
    char bind_h[64];
    char bind_g[64];
    const unsigned port = 41100u + (test_pid() % 700u) * 2u;

    memset(&hroom, 0, sizeof(hroom));
    snprintf(hroom.game, sizeof(hroom.game), "%s", "TestGame");
    snprintf(hroom.game_version, sizeof(hroom.game_version), "%s", "1.0.0");
    snprintf(hroom.host_name, sizeof(hroom.host_name), "%s", "Hostess");
    snprintf(bind_h, sizeof(bind_h), "127.0.0.1:%u", port);
    snprintf(bind_g, sizeof(bind_g), "127.0.0.1:%u", port + 1u);

    if (rnet_lan_direct_host_open(&host, bind_h, &hroom) != RNET_LAN_DIRECT_OK) {
        printf("FAIL: host open\n");
        return 1;
    }
    memset(&groom, 0, sizeof(groom));
    {
        struct PumpArgs args;
        int join_rc;
#ifdef _WIN32
        HANDLE th;
#else
        pthread_t th;
#endif
        args.host = host;
        args.room = &hroom;
        args.stop = 0;
#ifdef _WIN32
        th = CreateThread(NULL, 0, pump_thread, &args, 0, NULL);
#else
        (void)pthread_create(&th, NULL, pump_thread, &args);
#endif
        join_rc = rnet_lan_direct_guest_join(bind_h, "TestGame", "1.0.0", "",
                                             "Guesty", bind_g, 3000, &groom,
                                             &guest);
        args.stop = 1;
#ifdef _WIN32
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
#else
        (void)pthread_join(th, NULL);
#endif
        if (join_rc != RNET_LAN_DIRECT_OK) {
            printf("FAIL: guest join (%d)\n", join_rc);
            rnet_lan_direct_host_close(&host);
            return 1;
        }
    }
    settle(host, guest, &hroom, &groom);
    ck(hroom.joiner_name[0] != '\0', "the guest is seated");

    /* --- host speaks ------------------------------------------------- */
    ck(rnet_lan_direct_host_send_chat(host, "hostid", "Hostess", "hello") ==
           RNET_LAN_DIRECT_OK,
       "host sends");
    ck(rnet_lan_direct_host_take_chat(host, &line) &&
           strcmp(line.text, "hello") == 0,
       "the host keeps its own line");
    settle(host, guest, &hroom, &groom);
    ck(rnet_lan_direct_guest_take_chat(guest, &line), "the guest receives it");
    ck(strcmp(line.text, "hello") == 0, "with the text intact");
    ck(strcmp(line.from, "Hostess") == 0, "and the host's name on it");

    /* --- guest speaks -------------------------------------------------- */
    ck(rnet_lan_direct_guest_send_chat(guest, "guestid", "hi back") ==
           RNET_LAN_DIRECT_OK,
       "guest sends");
    /* Nothing kept locally at send time: the echo is the copy it keeps. */
    ck(!rnet_lan_direct_guest_take_chat(guest, &line),
       "the guest keeps no copy of its own send");
    settle(host, guest, &hroom, &groom);

    ck(rnet_lan_direct_host_take_chat(host, &line), "the host receives it");
    ck(strcmp(line.text, "hi back") == 0, "with the text intact");
    /* Stamped from the seat table -- the guest never supplied a name. */
    ck(strcmp(line.from, "Guesty") == 0,
       "stamped with the seat name the HOST holds");
    ck(rnet_lan_direct_guest_take_chat(guest, &line),
       "and the sender gets it back as the host's echo");
    ck(strcmp(line.from, "Guesty") == 0, "the echo carries the same name");

    /* --- a line cannot smuggle a newline ------------------------------- */
    ck(rnet_lan_direct_host_send_chat(host, "hostid", "Hostess",
                                      "one\ntwo") == RNET_LAN_DIRECT_OK,
       "a multi-line send is accepted");
    ck(rnet_lan_direct_host_take_chat(host, &line), "and kept");
    ck(strchr(line.text, '\n') == NULL,
       "with the newline flattened, not carried onto the wire");
    ck(strcmp(line.text, "one two") == 0, "as a single readable line");

    /* --- empty is not a line ------------------------------------------- */
    ck(rnet_lan_direct_host_send_chat(host, "hostid", "Hostess", "   \n") !=
           RNET_LAN_DIRECT_OK ||
           !rnet_lan_direct_host_take_chat(host, &line) ||
           line.text[0] != '\0',
       "whitespace-only does not become an empty line in the log");

    /* --- draining is oldest-first and stops when empty ------------------ */
    while (rnet_lan_direct_host_take_chat(host, &line)) { }
    ck(!rnet_lan_direct_host_take_chat(host, &line),
       "an empty queue reports empty");

    rnet_lan_direct_guest_close(&guest);
    rnet_lan_direct_host_close(&host);

    if (g_failures == 0) {
        printf("lan_chat_test: ok\n");
        return 0;
    }
    fprintf(stderr, "lan_chat_test: %d failure(s)\n", g_failures);
    return 1;
}
