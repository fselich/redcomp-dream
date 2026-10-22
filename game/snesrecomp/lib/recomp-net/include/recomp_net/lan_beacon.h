#ifndef RECOMP_NET_LAN_BEACON_H
#define RECOMP_NET_LAN_BEACON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Local UDP broadcast discovery for lobby hosts.
 *
 * Hosts announce lobby_id + LAN game endpoint on a well-known discovery port.
 * Guests listen and map lobby_id -> RFC1918 endpoint for list RTT / join
 * without publishing private IPs to the matchmaking hub, and LAN/Direct IP
 * rooms use the same beacon as their ONLY cross-machine discovery: the file
 * registry (lan_lobby.h) is visible to one machine, so without this a remote
 * peer could join a LAN room only by typing its IP.
 *
 * Wire (text lines, UTF-8):
 *   RNETBC1
 *   ANNOUNCE
 *   <lobby_id>
 *   <game_endpoint>   e.g. 192.168.1.42:7777
 *   <game_name>       optional; empty line ok
 *   -- V2 rows, each optional; a V1 listener stops reading above them --
 *   <game_version>
 *   <room_name>
 *   pw=<0|1> players=<n> max=<n> started=<0|1>
 */

#define RNET_LAN_BEACON_DEFAULT_PORT 48777
#define RNET_LAN_BEACON_ID_MAX 40
#define RNET_LAN_BEACON_ENDPOINT_MAX 64
#define RNET_LAN_BEACON_GAME_MAX 64
#define RNET_LAN_BEACON_VERSION_MAX 32
#define RNET_LAN_BEACON_NAME_MAX 64

typedef struct RNetLanBeacon RNetLanBeacon;

/* One announced room, as published or as heard. Fields a V1 publisher never
 * sent read back empty / 0; has_password, player_count, max_slots and started
 * are the host's seat table at the moment it announced, so a browser can draw
 * the row before anyone joins. */
typedef struct RNetLanBeaconRoom {
    char lobby_id[RNET_LAN_BEACON_ID_MAX];
    char endpoint[RNET_LAN_BEACON_ENDPOINT_MAX];
    char game_name[RNET_LAN_BEACON_GAME_MAX];
    char game_version[RNET_LAN_BEACON_VERSION_MAX];
    char room_name[RNET_LAN_BEACON_NAME_MAX];
    int has_password;
    int player_count;
    int max_slots;
    int started;
} RNetLanBeaconRoom;

/* Publisher (waiting-room host). discovery_port 0 -> default. */
int rnet_lan_beacon_publish_open(RNetLanBeacon **out, unsigned short discovery_port);
int rnet_lan_beacon_publish_set(RNetLanBeacon *beacon, const char *lobby_id,
                                const char *game_endpoint, const char *game_name);
/* Full-row variant of publish_set. Returns -1 (and announces nothing) when
 * lobby_id is empty or the endpoint is not a private IPv4:port -- a loopback
 * or WAN endpoint is not something a LAN broadcast should hand out. */
int rnet_lan_beacon_publish_set_room(RNetLanBeacon *beacon,
                                     const RNetLanBeaconRoom *room);
/* Send at most once per ~1s while set. Returns 0 ok. */
int rnet_lan_beacon_publish_tick(RNetLanBeacon *beacon);

/* Listener (lobby list browser). discovery_port 0 -> default. */
int rnet_lan_beacon_listen_open(RNetLanBeacon **out, unsigned short discovery_port);
/* Drain announces into the cache. Returns count newly updated this call. */
int rnet_lan_beacon_listen_pump(RNetLanBeacon *beacon);
/* Feed one datagram to the listener's cache as if it had arrived on the
 * socket. Returns 1 when it parsed as an announce. For tests and for hosts
 * that carry announces over some other channel. */
int rnet_lan_beacon_listen_inject(RNetLanBeacon *beacon, const char *pkt,
                                  size_t len);

/* Lookup a fresh announce (seen within ~5s). Returns 1 and fills endpoint. */
int rnet_lan_beacon_lookup(const RNetLanBeacon *beacon, const char *lobby_id,
                           char *endpoint_out, size_t endpoint_cap);
/* Enumerate the fresh announces (seen within ~5s): count, then get by index
 * in [0, count). Both skip stale rows, so an index is only stable within one
 * pump; callers rebuild their list each time rather than caching indices.
 * get returns 1 and fills *out, 0 past the end. */
int rnet_lan_beacon_count(const RNetLanBeacon *beacon);
int rnet_lan_beacon_get(const RNetLanBeacon *beacon, int index,
                        RNetLanBeaconRoom *out);

/* Format an announce datagram for room into buf (NUL-terminated). Returns
 * its length, or -1 when it does not fit or room is not announceable. What
 * publish_tick sends; public so a test can check the wire without a socket. */
int rnet_lan_beacon_format_announce(const RNetLanBeaconRoom *room, char *buf,
                                    size_t cap);

void rnet_lan_beacon_close(RNetLanBeacon **beacon);

#ifdef __cplusplus
}
#endif

#endif /* RECOMP_NET_LAN_BEACON_H */
