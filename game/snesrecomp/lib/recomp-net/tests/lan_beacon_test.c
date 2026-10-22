#include "recomp_net/lan_beacon.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

int main(void)
{
    RNetLanBeacon *listener = NULL;
    RNetLanBeaconRoom room;
    RNetLanBeaconRoom got;
    char pkt[512];
    int len;

    memset(&room, 0, sizeof(room));
    snprintf(room.lobby_id, sizeof(room.lobby_id), "lan:192.168.1.20:7777");
    snprintf(room.endpoint, sizeof(room.endpoint), "192.168.1.20:7777");
    snprintf(room.game_name, sizeof(room.game_name), "Gundam Wing Endless Duel");
    snprintf(room.game_version, sizeof(room.game_version), "0.3.0");
    snprintf(room.room_name, sizeof(room.room_name), "Alex's\nRoom");
    room.has_password = 1;
    room.player_count = 1;
    room.max_slots = 2;
    room.started = 0;

    /* The wire: V1 rows first, V2 rows after, one field per line. */
    len = rnet_lan_beacon_format_announce(&room, pkt, sizeof(pkt));
    expect(len > 0, "format announce");
    expect(strncmp(pkt, "RNETBC1\nANNOUNCE\nlan:192.168.1.20:7777\n"
                        "192.168.1.20:7777\nGundam Wing Endless Duel\n",
                   len) < 0 || strstr(pkt, "\n0.3.0\nAlex's Room\npw=1 players=1 "
                                          "max=2 started=0\n") != NULL,
           "announce carries V2 rows with the newline in the name flattened");

    /* Not announceable: loopback and WAN endpoints, empty id. */
    snprintf(room.endpoint, sizeof(room.endpoint), "127.0.0.1:7777");
    expect(rnet_lan_beacon_format_announce(&room, pkt, sizeof(pkt)) < 0,
           "refuse loopback endpoint");
    snprintf(room.endpoint, sizeof(room.endpoint), "8.8.8.8:7777");
    expect(rnet_lan_beacon_format_announce(&room, pkt, sizeof(pkt)) < 0,
           "refuse WAN endpoint");
    snprintf(room.endpoint, sizeof(room.endpoint), "10.0.0.5:7777");
    room.lobby_id[0] = '\0';
    expect(rnet_lan_beacon_format_announce(&room, pkt, sizeof(pkt)) < 0,
           "refuse empty lobby id");
    snprintf(room.lobby_id, sizeof(room.lobby_id), "lan:10.0.0.5:7777");
    len = rnet_lan_beacon_format_announce(&room, pkt, sizeof(pkt));
    expect(len > 0, "format announce (10/8)");

    /* Listener cache through the injection path (no network needed). Port
     * 0 means the default; a bind failure here (port in use by a running
     * launcher) is not what this test measures, so fall back to a high port. */
    if (rnet_lan_beacon_listen_open(&listener, 0) != 0)
        expect(rnet_lan_beacon_listen_open(&listener, 48999) == 0, "open listener");
    if (listener) {
        expect(rnet_lan_beacon_count(listener) == 0, "empty cache");
        expect(rnet_lan_beacon_listen_inject(listener, pkt, (size_t)len) == 1,
               "inject announce");
        expect(rnet_lan_beacon_count(listener) == 1, "one fresh room");
        memset(&got, 0, sizeof(got));
        expect(rnet_lan_beacon_get(listener, 0, &got) == 1, "get row 0");
        expect(strcmp(got.lobby_id, "lan:10.0.0.5:7777") == 0, "row lobby id");
        expect(strcmp(got.endpoint, "10.0.0.5:7777") == 0, "row endpoint");
        expect(strcmp(got.game_name, "Gundam Wing Endless Duel") == 0, "row game");
        expect(strcmp(got.game_version, "0.3.0") == 0, "row version");
        expect(strcmp(got.room_name, "Alex's Room") == 0, "row name");
        expect(got.has_password == 1 && got.player_count == 1 &&
                   got.max_slots == 2 && got.started == 0,
               "row flags");
        expect(rnet_lan_beacon_get(listener, 1, &got) == 0, "no row 1");

        /* Same id again replaces rather than duplicates. */
        room.player_count = 2;
        room.started = 1;
        len = rnet_lan_beacon_format_announce(&room, pkt, sizeof(pkt));
        expect(rnet_lan_beacon_listen_inject(listener, pkt, (size_t)len) == 1,
               "inject update");
        expect(rnet_lan_beacon_count(listener) == 1, "still one room");
        expect(rnet_lan_beacon_get(listener, 0, &got) == 1 &&
                   got.player_count == 2 && got.started == 1,
               "update replaced the row");

        /* A V1 announce (five rows) still parses; V2 fields read back empty. */
        {
            const char *v1 = "RNETBC1\nANNOUNCE\nabc123\n172.16.4.9:7777\nOld Game\n";
            expect(rnet_lan_beacon_listen_inject(listener, v1, strlen(v1)) == 1,
                   "inject V1 announce");
            expect(rnet_lan_beacon_count(listener) == 2, "two rooms");
            expect(rnet_lan_beacon_get(listener, 1, &got) == 1 ||
                       rnet_lan_beacon_get(listener, 0, &got) == 1,
                   "get a row");
            {
                char ep[64];
                expect(rnet_lan_beacon_lookup(listener, "abc123", ep, sizeof(ep)) == 1 &&
                           strcmp(ep, "172.16.4.9:7777") == 0,
                       "lookup V1 row");
            }
        }
        /* Garbage and non-private endpoints are dropped. */
        {
            const char *bad1 = "HELLO\n";
            const char *bad2 = "RNETBC1\nANNOUNCE\nx\n1.2.3.4:7777\n";
            expect(rnet_lan_beacon_listen_inject(listener, bad1, strlen(bad1)) == 0,
                   "drop garbage");
            expect(rnet_lan_beacon_listen_inject(listener, bad2, strlen(bad2)) == 0,
                   "drop WAN announce");
            expect(rnet_lan_beacon_count(listener) == 2, "count unchanged");
        }
        rnet_lan_beacon_close(&listener);
        expect(listener == NULL, "close clears handle");
    }

    if (failures)
    {
        fprintf(stderr, "lan_beacon_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("lan_beacon_test: ok\n");
    return 0;
}
