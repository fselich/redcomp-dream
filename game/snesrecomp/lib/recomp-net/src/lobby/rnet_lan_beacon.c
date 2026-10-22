#include "recomp_net/lan_beacon.h"

#include "platform/rnet_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RNET_BC_MAGIC "RNETBC1"
#define RNET_BC_MAX_PKT 512
#define RNET_BC_CACHE_MAX 32
#define RNET_BC_INTERVAL_MS 1000ull
#define RNET_BC_STALE_MS 5000ull

typedef struct RNetLanBeaconEntry {
    RNetLanBeaconRoom room;   /* room.lobby_id[0] == 0 marks a free slot */
    rnet_u64 last_seen_ms;
} RNetLanBeaconEntry;

struct RNetLanBeacon {
    rnet_socket sock;
    unsigned short discovery_port;
    int is_publisher;
    RNetLanBeaconRoom room;   /* publisher: what tick announces */
    rnet_u64 next_send_ms;
    RNetLanBeaconEntry cache[RNET_BC_CACHE_MAX];
    int cache_count;
};

static void copy_trunc(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    dst[0] = '\0';
    if (!src)
        return;
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static void trim_crlf(char *s)
{
    size_t n;
    if (!s)
        return;
    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

static const char *next_line(char **cursor)
{
    char *start;
    char *nl;
    if (!cursor || !*cursor || !**cursor)
        return NULL;
    start = *cursor;
    nl = strchr(start, '\n');
    if (nl) {
        *nl = '\0';
        *cursor = nl + 1;
    } else {
        *cursor = start + strlen(start);
    }
    trim_crlf(start);
    return start;
}

static int endpoint_looks_ok(const char *ep)
{
    const char *colon;
    unsigned a, b, c, d;
    char extra;
    int port;
    if (!ep || !ep[0])
        return 0;
    colon = strrchr(ep, ':');
    if (!colon || colon == ep || !colon[1])
        return 0;
    if (sscanf(ep, "%u.%u.%u.%u:%d%c", &a, &b, &c, &d, &port, &extra) != 5)
        return 0;
    if (a > 255 || b > 255 || c > 255 || d > 255 || port <= 0 || port > 65535)
        return 0;
    /* RFC1918 only — never broadcast loopback / WAN via discovery. */
    if (a == 10)
        return 1;
    if (a == 172 && b >= 16 && b <= 31)
        return 1;
    if (a == 192 && b == 168)
        return 1;
    return 0;
}

/* A text field rides one line of the datagram, so a newline inside it would
 * shift every row after it. Room names come from a text box. */
static void copy_line_field(char *dst, size_t cap, const char *src)
{
    size_t i;
    copy_trunc(dst, cap, src);
    for (i = 0; dst && dst[i]; ++i)
        if (dst[i] == '\n' || dst[i] == '\r')
            dst[i] = ' ';
}

int rnet_lan_beacon_format_announce(const RNetLanBeaconRoom *room, char *buf,
                                    size_t cap)
{
    RNetLanBeaconRoom clean;
    int n;
    if (!buf || cap < 32 || !room || !room->lobby_id[0] ||
        !endpoint_looks_ok(room->endpoint))
        return -1;
    memset(&clean, 0, sizeof(clean));
    copy_line_field(clean.lobby_id, sizeof(clean.lobby_id), room->lobby_id);
    copy_line_field(clean.game_name, sizeof(clean.game_name), room->game_name);
    copy_line_field(clean.game_version, sizeof(clean.game_version),
                    room->game_version);
    copy_line_field(clean.room_name, sizeof(clean.room_name), room->room_name);
    n = snprintf(buf, cap,
                 "%s\nANNOUNCE\n%s\n%s\n%s\n%s\n%s\npw=%d players=%d max=%d "
                 "started=%d\n",
                 RNET_BC_MAGIC, clean.lobby_id, room->endpoint, clean.game_name,
                 clean.game_version, clean.room_name, room->has_password ? 1 : 0,
                 room->player_count, room->max_slots, room->started ? 1 : 0);
    if (n < 0 || (size_t)n >= cap)
        return -1;
    return n;
}

static int parse_announce(char *pkt, RNetLanBeaconRoom *out)
{
    char *cursor = pkt;
    const char *magic = next_line(&cursor);
    const char *op = next_line(&cursor);
    const char *id = next_line(&cursor);
    const char *ep = next_line(&cursor);
    const char *game = next_line(&cursor);
    /* V2 rows. A V1 publisher stops at game_name; each of these is NULL then
     * and the room reads back with empty strings / zeros. */
    const char *version = next_line(&cursor);
    const char *name = next_line(&cursor);
    const char *flags = next_line(&cursor);
    if (!magic || strcmp(magic, RNET_BC_MAGIC) != 0)
        return 0;
    if (!op || strcmp(op, "ANNOUNCE") != 0)
        return 0;
    if (!id || !id[0] || !ep || !endpoint_looks_ok(ep))
        return 0;
    memset(out, 0, sizeof(*out));
    copy_trunc(out->lobby_id, sizeof(out->lobby_id), id);
    copy_trunc(out->endpoint, sizeof(out->endpoint), ep);
    copy_trunc(out->game_name, sizeof(out->game_name), game ? game : "");
    copy_trunc(out->game_version, sizeof(out->game_version),
               version ? version : "");
    copy_trunc(out->room_name, sizeof(out->room_name), name ? name : "");
    if (flags) {
        int pw = 0, players = 0, max = 0, started = 0;
        if (sscanf(flags, "pw=%d players=%d max=%d started=%d", &pw, &players,
                   &max, &started) == 4) {
            out->has_password = pw != 0;
            out->player_count = players;
            out->max_slots = max;
            out->started = started != 0;
        }
    }
    return 1;
}

static int entry_fresh(const RNetLanBeaconEntry *e, rnet_u64 now)
{
    return e->room.lobby_id[0] && now - e->last_seen_ms <= RNET_BC_STALE_MS;
}

static void cache_upsert(RNetLanBeacon *b, const RNetLanBeaconRoom *room,
                         rnet_u64 now)
{
    int i;
    int free_i = -1;
    int oldest_i = 0;
    rnet_u64 oldest = 0;
    if (!b || !room)
        return;
    for (i = 0; i < RNET_BC_CACHE_MAX; ++i) {
        if (!b->cache[i].room.lobby_id[0]) {
            if (free_i < 0)
                free_i = i;
            continue;
        }
        if (strcmp(b->cache[i].room.lobby_id, room->lobby_id) == 0) {
            b->cache[i].room = *room;
            b->cache[i].last_seen_ms = now;
            return;
        }
        if (free_i < 0 &&
            (oldest == 0 || b->cache[i].last_seen_ms < oldest)) {
            oldest = b->cache[i].last_seen_ms;
            oldest_i = i;
        }
    }
    i = free_i >= 0 ? free_i : oldest_i;
    b->cache[i].room = *room;
    b->cache[i].last_seen_ms = now;
    if (b->cache_count < RNET_BC_CACHE_MAX)
        ++b->cache_count;
}

static RNetLanBeacon *beacon_alloc(unsigned short discovery_port, int is_publisher)
{
    RNetLanBeacon *b = (RNetLanBeacon *)calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    b->sock = RNET_SOCKET_INVALID;
    b->discovery_port =
        discovery_port ? discovery_port : (unsigned short)RNET_LAN_BEACON_DEFAULT_PORT;
    b->is_publisher = is_publisher ? 1 : 0;
    return b;
}

int rnet_lan_beacon_publish_open(RNetLanBeacon **out, unsigned short discovery_port)
{
    RNetLanBeacon *b;
    struct sockaddr_in addr;

    if (!out)
        return -1;
    *out = NULL;
    rnet_os_startup();
    b = beacon_alloc(discovery_port, 1);
    if (!b)
        return -1;
    b->sock = rnet_os_socket_create_dgram();
    if (!rnet_os_socket_valid(b->sock)) {
        free(b);
        return -1;
    }
    (void)rnet_os_setsockopt_broadcast(b->sock, 1);
    (void)rnet_os_set_nonblocking(b->sock);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;
    if (rnet_os_bind(b->sock, &addr) != 0) {
        rnet_lan_beacon_close(&b);
        return -1;
    }
    *out = b;
    return 0;
}

int rnet_lan_beacon_publish_set_room(RNetLanBeacon *beacon,
                                     const RNetLanBeaconRoom *room)
{
    if (!beacon || !beacon->is_publisher)
        return -1;
    if (!room || !room->lobby_id[0] || !endpoint_looks_ok(room->endpoint)) {
        memset(&beacon->room, 0, sizeof(beacon->room));
        return -1;
    }
    beacon->room = *room;
    beacon->next_send_ms = 0;
    return 0;
}

int rnet_lan_beacon_publish_set(RNetLanBeacon *beacon, const char *lobby_id,
                                const char *game_endpoint, const char *game_name)
{
    RNetLanBeaconRoom room;
    memset(&room, 0, sizeof(room));
    copy_trunc(room.lobby_id, sizeof(room.lobby_id), lobby_id);
    copy_trunc(room.endpoint, sizeof(room.endpoint), game_endpoint);
    copy_trunc(room.game_name, sizeof(room.game_name), game_name);
    return rnet_lan_beacon_publish_set_room(beacon, &room);
}

int rnet_lan_beacon_publish_tick(RNetLanBeacon *beacon)
{
    char buf[RNET_BC_MAX_PKT];
    struct sockaddr_in dst;
    rnet_u64 now;
    int len;
    if (!beacon || !beacon->is_publisher || !rnet_os_socket_valid(beacon->sock))
        return -1;
    if (!beacon->room.lobby_id[0] || !beacon->room.endpoint[0])
        return 0;
    now = rnet_os_monotonic_ms();
    if (beacon->next_send_ms && now < beacon->next_send_ms)
        return 0;
    len = rnet_lan_beacon_format_announce(&beacon->room, buf, sizeof(buf));
    if (len < 0)
        return -1;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    dst.sin_port = htons(beacon->discovery_port);
    /* rnet_os_sendto returns bytes sent (>=0) or -1. */
    if (rnet_os_sendto(beacon->sock, buf, (size_t)len, &dst) < 0)
        return -1;
    beacon->next_send_ms = now + RNET_BC_INTERVAL_MS;
    return 0;
}

int rnet_lan_beacon_listen_open(RNetLanBeacon **out, unsigned short discovery_port)
{
    RNetLanBeacon *b;
    struct sockaddr_in addr;

    if (!out)
        return -1;
    *out = NULL;
    rnet_os_startup();
    b = beacon_alloc(discovery_port, 0);
    if (!b)
        return -1;
    b->sock = rnet_os_socket_create_dgram();
    if (!rnet_os_socket_valid(b->sock)) {
        free(b);
        return -1;
    }
    /* reuseaddr: two launchers on one machine both listen on the discovery
     * port, and UDP delivers a broadcast to every socket bound to it. */
    (void)rnet_os_setsockopt_reuseaddr(b->sock, 1);
    (void)rnet_os_setsockopt_broadcast(b->sock, 1);
    (void)rnet_os_set_nonblocking(b->sock);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(b->discovery_port);
    if (rnet_os_bind(b->sock, &addr) != 0) {
        rnet_lan_beacon_close(&b);
        return -1;
    }
    *out = b;
    return 0;
}

int rnet_lan_beacon_listen_inject(RNetLanBeacon *beacon, const char *pkt,
                                  size_t len)
{
    char buf[RNET_BC_MAX_PKT];
    RNetLanBeaconRoom room;
    if (!beacon || beacon->is_publisher || !pkt)
        return 0;
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    memcpy(buf, pkt, len);
    buf[len] = '\0';
    if (!parse_announce(buf, &room))
        return 0;
    cache_upsert(beacon, &room, rnet_os_monotonic_ms());
    return 1;
}

int rnet_lan_beacon_listen_pump(RNetLanBeacon *beacon)
{
    int updated = 0;
    if (!beacon || beacon->is_publisher || !rnet_os_socket_valid(beacon->sock))
        return 0;
    for (;;) {
        char buf[RNET_BC_MAX_PKT];
        struct sockaddr_in src;
        int would_block = 0;
        int n;
        memset(&src, 0, sizeof(src));
        n = rnet_os_recvfrom(beacon->sock, buf, sizeof(buf) - 1, &src, &would_block);
        if (n <= 0)
            break;   /* would-block, error, or an empty datagram: nothing more now */
        updated += rnet_lan_beacon_listen_inject(beacon, buf, (size_t)n);
    }
    return updated;
}

int rnet_lan_beacon_lookup(const RNetLanBeacon *beacon, const char *lobby_id,
                           char *endpoint_out, size_t endpoint_cap)
{
    rnet_u64 now;
    int i;
    if (!beacon || !lobby_id || !lobby_id[0] || !endpoint_out || endpoint_cap == 0)
        return 0;
    endpoint_out[0] = '\0';
    now = rnet_os_monotonic_ms();
    for (i = 0; i < RNET_BC_CACHE_MAX; ++i) {
        if (!beacon->cache[i].room.lobby_id[0])
            continue;
        if (strcmp(beacon->cache[i].room.lobby_id, lobby_id) != 0)
            continue;
        if (!entry_fresh(&beacon->cache[i], now))
            return 0;
        copy_trunc(endpoint_out, endpoint_cap, beacon->cache[i].room.endpoint);
        return endpoint_out[0] ? 1 : 0;
    }
    return 0;
}

int rnet_lan_beacon_count(const RNetLanBeacon *beacon)
{
    rnet_u64 now;
    int i;
    int n = 0;
    if (!beacon)
        return 0;
    now = rnet_os_monotonic_ms();
    for (i = 0; i < RNET_BC_CACHE_MAX; ++i)
        if (entry_fresh(&beacon->cache[i], now))
            ++n;
    return n;
}

int rnet_lan_beacon_get(const RNetLanBeacon *beacon, int index,
                        RNetLanBeaconRoom *out)
{
    rnet_u64 now;
    int i;
    if (!beacon || !out || index < 0)
        return 0;
    now = rnet_os_monotonic_ms();
    for (i = 0; i < RNET_BC_CACHE_MAX; ++i) {
        if (!entry_fresh(&beacon->cache[i], now))
            continue;
        if (index-- == 0) {
            *out = beacon->cache[i].room;
            return 1;
        }
    }
    return 0;
}

void rnet_lan_beacon_close(RNetLanBeacon **beacon)
{
    if (!beacon || !*beacon)
        return;
    rnet_os_socket_destroy(&(*beacon)->sock);
    free(*beacon);
    *beacon = NULL;
}
