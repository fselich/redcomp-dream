#include "recomp_net/lan_direct.h"

#include "platform/rnet_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RNET_DJ_MAGIC "RNETDJ1"
#define RNET_DJ_MAX_PKT 2048

/* Received chat, oldest at head. Sized so a burst between two pumps is kept
 * rather than dropped; when it does overflow the OLDEST goes, because losing
 * what was just said is the worse failure. */
struct RNetLanChatQueue {
    RNetLanChatLine line[RNET_LAN_CHAT_QUEUE];
    int head;
    int count;
};

struct RNetLanDirectHost {
    rnet_socket sock;
    struct sockaddr_in guest;
    int guest_known;
    char bind_hostport[64];
    struct RNetLanChatQueue chat;
    int swap_req_pending; /* the seated guest asked to trade seats */
};

struct RNetLanDirectGuest {
    rnet_socket sock;
    struct sockaddr_in host;
    char host_hostport[64];
    struct RNetLanChatQueue chat;
    int swap_res_pending; /* the host answered a swap request */
    int swap_res_accept;
    /* Between join_begin and the JOIN_OK: not seated, and nothing but the
     * answer to JOIN_REQ is read (join_poll owns the socket until then). */
    int joining;
    int join_failed;          /* the JOIN_NAK's rc, once one arrived */
    rnet_u64 join_sent_ms;
    char join_req[RNET_DJ_MAX_PKT];
    char expected_game[RNET_LAN_LOBBY_GAME_MAX];
    char expected_version[RNET_LAN_LOBBY_VERSION_MAX];
    char player_name[RNET_LAN_LOBBY_PLAYER_MAX];
};

static void chat_queue_push(struct RNetLanChatQueue *q, const char *player_id,
                            const char *from, const char *text)
{
    RNetLanChatLine *m;
    int idx;
    if (!q || !text || !text[0])
        return;
    if (q->count < RNET_LAN_CHAT_QUEUE) {
        idx = (q->head + q->count) % RNET_LAN_CHAT_QUEUE;
        q->count++;
    } else {
        idx = q->head;
        q->head = (q->head + 1) % RNET_LAN_CHAT_QUEUE;
    }
    m = &q->line[idx];
    memset(m, 0, sizeof(*m));
    snprintf(m->player_id, sizeof(m->player_id), "%s", player_id ? player_id : "");
    snprintf(m->from, sizeof(m->from), "%s", from ? from : "");
    snprintf(m->text, sizeof(m->text), "%s", text);
}

static int chat_queue_take(struct RNetLanChatQueue *q, RNetLanChatLine *out)
{
    if (!q || !out || q->count <= 0)
        return 0;
    *out = q->line[q->head];
    q->head = (q->head + 1) % RNET_LAN_CHAT_QUEUE;
    q->count--;
    return 1;
}

/* A chat line is one line on a newline-delimited wire, so it cannot contain
 * one. Control bytes are dropped and newlines become spaces rather than the
 * line being refused: a peer that pastes two lines should be heard, not
 * silently ignored -- and must not be able to forge a second datagram field. */
static void chat_sanitize(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    if (!out || cap == 0)
        return;
    for (; in && *in && o + 1 < cap; ++in) {
        const unsigned char c = (unsigned char)*in;
        if (c == '\n' || c == '\r') {
            out[o++] = ' ';
            continue;
        }
        if (c < 0x20)
            continue;
        out[o++] = (char)c;
    }
    out[o] = '\0';
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

static int append_line(char *buf, size_t cap, size_t *o, const char *line)
{
    size_t n;
    if (!buf || !o || !line)
        return 0;
    n = strlen(line);
    if (*o + n + 2 >= cap)
        return 0;
    memcpy(buf + *o, line, n);
    *o += n;
    buf[(*o)++] = '\n';
    buf[*o] = '\0';
    return 1;
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

static int send_text(rnet_socket sock, const struct sockaddr_in *dst,
                     const char *text)
{
    size_t n;
    if (!rnet_os_socket_valid(sock) || !dst || !text)
        return RNET_LAN_DIRECT_ERR_IO;
    n = strlen(text);
    if (n == 0 || n > RNET_DJ_MAX_PKT)
        return RNET_LAN_DIRECT_ERR_IO;
    if (rnet_os_sendto(sock, text, n, dst) < 0)
        return RNET_LAN_DIRECT_ERR_IO;
    return RNET_LAN_DIRECT_OK;
}

static int build_join_req(char *buf, size_t cap, const char *game,
                          const char *version, const char *password,
                          const char *player)
{
    size_t o = 0;
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, "JOIN_REQ") ||
        !append_line(buf, cap, &o, game ? game : "") ||
        !append_line(buf, cap, &o, version ? version : "") ||
        !append_line(buf, cap, &o, password ? password : "") ||
        !append_line(buf, cap, &o, player && player[0] ? player : "Player"))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

static int build_chat(char *buf, size_t cap, const char *player_id,
                      const char *from, const char *text)
{
    size_t o = 0;
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, "CHAT") ||
        !append_line(buf, cap, &o, player_id ? player_id : "") ||
        !append_line(buf, cap, &o, from ? from : "") ||
        !append_line(buf, cap, &o, text ? text : ""))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

/* Room state the guest cannot otherwise learn between join and start: who
 * sits where. Sent by the host whenever the room changes. */
static int build_room(char *buf, size_t cap, const RNetLanLobby *room)
{
    size_t o = 0;
    char host_slot[8];
    char started[8];
    snprintf(host_slot, sizeof(host_slot), "%d", room && room->host_slot == 1 ? 1 : 0);
    snprintf(started, sizeof(started), "%d", room && room->started ? 1 : 0);
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, "ROOM") ||
        !append_line(buf, cap, &o, host_slot) ||
        !append_line(buf, cap, &o, room ? room->host_name : "") ||
        !append_line(buf, cap, &o, room ? room->joiner_name : "") ||
        !append_line(buf, cap, &o, started))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

static int build_swap_req(char *buf, size_t cap)
{
    size_t o = 0;
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, "SWAPREQ"))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

static int build_swap_res(char *buf, size_t cap, int accept)
{
    size_t o = 0;
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, "SWAPRES") ||
        !append_line(buf, cap, &o, accept ? "1" : "0"))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

static int build_chat_req(char *buf, size_t cap, const char *player_id,
                          const char *text)
{
    size_t o = 0;
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, "CHATREQ") ||
        !append_line(buf, cap, &o, player_id ? player_id : "") ||
        !append_line(buf, cap, &o, text ? text : ""))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

static int clamp_direct_input_delay(int delay)
{
    if (delay < 2)
        return 2;
    if (delay > 20)
        return 20;
    return delay;
}

static int parse_direct_input_delay_line(const char *line, int def)
{
    long v;
    char *end;
    if (!line || !line[0])
        return def;
    v = strtol(line, &end, 10);
    if (end == line || *end != '\0')
        return def;
    return clamp_direct_input_delay((int)v);
}

static int clamp_direct_prediction(int p)
{
    if (p < 2)
        return 2;
    if (p > 16)
        return 16;
    return p;
}

static int parse_direct_prediction_line(const char *line, int def)
{
    long v;
    char *end;
    if (!line || !line[0])
        return clamp_direct_prediction(def);
    v = strtol(line, &end, 10);
    if (end == line || *end != '\0')
        return clamp_direct_prediction(def);
    return clamp_direct_prediction((int)v);
}

static int parse_direct_bool_line(const char *line, int def)
{
    long v;
    char *end;
    if (!line || !line[0])
        return def ? 1 : 0;
    v = strtol(line, &end, 10);
    if (end == line || *end != '\0')
        return def ? 1 : 0;
    return v != 0 ? 1 : 0;
}

static int build_join_ok(char *buf, size_t cap, const RNetLanLobby *room)
{
    char slot[8];
    char delay_line[16];
    char rollback_line[8];
    char pred_line[16];
    char variant_line[16];
    size_t o = 0;
    if (!room)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    snprintf(variant_line, sizeof(variant_line), "%d", room->session_variant);
    snprintf(slot, sizeof(slot), "%d", room->host_slot == 1 ? 1 : 0);
    snprintf(delay_line, sizeof(delay_line), "%d",
             clamp_direct_input_delay(room->input_delay >= 2 ? room->input_delay
                                                             : 2));
    snprintf(rollback_line, sizeof(rollback_line), "%d",
             room->rollback ? 1 : 0);
    snprintf(pred_line, sizeof(pred_line), "%d",
             clamp_direct_prediction(room->input_prediction >= 2
                                         ? room->input_prediction
                                         : 4));
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, "JOIN_OK") ||
        !append_line(buf, cap, &o, room->endpoint) ||
        !append_line(buf, cap, &o, room->host_name) ||
        !append_line(buf, cap, &o, room->joiner_name) ||
        !append_line(buf, cap, &o, slot) ||
        !append_line(buf, cap, &o, room->name) ||
        !append_line(buf, cap, &o, room->game) ||
        !append_line(buf, cap, &o, room->game_version) ||
        !append_line(buf, cap, &o, delay_line) ||
        !append_line(buf, cap, &o, rollback_line) ||
        !append_line(buf, cap, &o, pred_line) ||
        !append_line(buf, cap, &o, variant_line) ||
        !append_line(buf, cap, &o, room->mod_set))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

static int build_join_nak(char *buf, size_t cap, const char *code)
{
    size_t o = 0;
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, "JOIN_NAK") ||
        !append_line(buf, cap, &o, code ? code : "reject"))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

static int build_simple(char *buf, size_t cap, const char *op)
{
    size_t o = 0;
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, op ? op : ""))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

static int build_start(char *buf, size_t cap, const RNetLanLobby *room)
{
    char delay_line[16];
    char rollback_line[8];
    char pred_line[16];
    char session_line[16];
    char variant_line[16];
    size_t o = 0;
    int delay = 2;
    int rollback = 0;
    int pred = 4;
    if (room) {
        delay = clamp_direct_input_delay(room->input_delay >= 2 ? room->input_delay
                                                                : 2);
        rollback = room->rollback ? 1 : 0;
        pred = clamp_direct_prediction(room->input_prediction >= 2
                                           ? room->input_prediction
                                           : 4);
    }
    snprintf(delay_line, sizeof(delay_line), "%d", delay);
    snprintf(rollback_line, sizeof(rollback_line), "%d", rollback);
    snprintf(pred_line, sizeof(pred_line), "%d", pred);
    /* V4: the match's session id, which the HOST allocates per start (see
     * RNetLanLobby.session_id). Trailing, so an older guest ignores it. */
    snprintf(session_line, sizeof(session_line), "%u",
             room ? (unsigned)room->session_id : 0u);
    snprintf(variant_line, sizeof(variant_line), "%d", room ? room->session_variant : 0);
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, "START") ||
        !append_line(buf, cap, &o, delay_line) ||
        !append_line(buf, cap, &o, rollback_line) ||
        !append_line(buf, cap, &o, pred_line) ||
        !append_line(buf, cap, &o, session_line) ||
        !append_line(buf, cap, &o, variant_line) ||
        !append_line(buf, cap, &o, room ? room->mod_set : ""))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

static int build_caps(char *buf, size_t cap, const RNetLanLobby *room)
{
    char delay_line[16];
    char rollback_line[8];
    char pred_line[16];
    char variant_line[16];
    size_t o = 0;
    int delay = 2;
    int rollback = 0;
    int pred = 4;
    if (room) {
        delay = clamp_direct_input_delay(room->input_delay >= 2 ? room->input_delay
                                                                : 2);
        rollback = room->rollback ? 1 : 0;
        pred = clamp_direct_prediction(room->input_prediction >= 2
                                           ? room->input_prediction
                                           : 4);
    }
    snprintf(delay_line, sizeof(delay_line), "%d", delay);
    snprintf(rollback_line, sizeof(rollback_line), "%d", rollback);
    snprintf(pred_line, sizeof(pred_line), "%d", pred);
    snprintf(variant_line, sizeof(variant_line), "%d", room ? room->session_variant : 0);
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, "CAPS") ||
        !append_line(buf, cap, &o, delay_line) ||
        !append_line(buf, cap, &o, rollback_line) ||
        !append_line(buf, cap, &o, pred_line) ||
        !append_line(buf, cap, &o, variant_line) ||
        !append_line(buf, cap, &o, room ? room->mod_set : ""))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

static int nak_code_to_rc(const char *code)
{
    if (!code)
        return RNET_LAN_DIRECT_ERR_IO;
    if (strcmp(code, "password") == 0)
        return RNET_LAN_DIRECT_ERR_PASSWORD;
    if (strcmp(code, "full") == 0)
        return RNET_LAN_DIRECT_ERR_FULL;
    if (strcmp(code, "identity") == 0)
        return RNET_LAN_DIRECT_ERR_IDENTITY;
    if (strcmp(code, "started") == 0)
        return RNET_LAN_DIRECT_ERR_STARTED;
    return RNET_LAN_DIRECT_ERR_IO;
}

int rnet_lan_direct_host_open(RNetLanDirectHost **out, const char *bind_hostport,
                              const RNetLanLobby *room)
{
    RNetLanDirectHost *h;
    char host[128];
    rnet_u16 port = 0;
    struct sockaddr_in addr;
    (void)room;
    if (!out || !bind_hostport || !bind_hostport[0])
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    *out = NULL;
    rnet_os_startup();
    if (rnet_os_parse_hostport(bind_hostport, host, sizeof(host), &port) != 0 ||
        port == 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    if (rnet_os_resolve_sockaddr(host[0] ? host : "0.0.0.0", port, &addr) != 0)
        return RNET_LAN_DIRECT_ERR_IO;
    h = (RNetLanDirectHost *)calloc(1, sizeof(*h));
    if (!h)
        return RNET_LAN_DIRECT_ERR_IO;
    h->sock = rnet_os_socket_create_dgram();
    if (!rnet_os_socket_valid(h->sock)) {
        free(h);
        return RNET_LAN_DIRECT_ERR_IO;
    }
    (void)rnet_os_setsockopt_reuseaddr(h->sock, 1);
    if (rnet_os_bind(h->sock, &addr) != 0 ||
        rnet_os_set_nonblocking(h->sock) != 0) {
        rnet_os_socket_destroy(&h->sock);
        free(h);
        return RNET_LAN_DIRECT_ERR_IO;
    }
    snprintf(h->bind_hostport, sizeof(h->bind_hostport), "%s", bind_hostport);
    *out = h;
    return RNET_LAN_DIRECT_OK;
}

void rnet_lan_direct_host_close(RNetLanDirectHost **host)
{
    if (!host || !*host)
        return;
    rnet_os_socket_destroy(&(*host)->sock);
    free(*host);
    *host = NULL;
}

static int build_ping(char *buf, size_t cap, rnet_u64 t_ms)
{
    char ts[32];
    size_t o = 0;
    snprintf(ts, sizeof(ts), "%llu", (unsigned long long)t_ms);
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, "PING") || !append_line(buf, cap, &o, ts))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

static int build_pong(char *buf, size_t cap, const char *ts)
{
    size_t o = 0;
    if (!append_line(buf, cap, &o, RNET_DJ_MAGIC) ||
        !append_line(buf, cap, &o, "PONG") ||
        !append_line(buf, cap, &o, ts ? ts : "0"))
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return RNET_LAN_DIRECT_OK;
}

static int rtt_from_pong_ts(const char *ts, int *out_rtt_ms)
{
    unsigned long long sent = 0;
    rnet_u64 now;
    if (!ts || !out_rtt_ms || sscanf(ts, "%llu", &sent) != 1)
        return 0;
    now = rnet_os_monotonic_ms();
    if ((rnet_u64)sent > now)
        return 0;
    *out_rtt_ms = (int)(now - (rnet_u64)sent);
    if (*out_rtt_ms < 0)
        *out_rtt_ms = 0;
    if (*out_rtt_ms > 60000)
        *out_rtt_ms = 60000;
    return 1;
}

int rnet_lan_direct_host_ping(RNetLanDirectHost *host)
{
    char buf[RNET_DJ_MAX_PKT];
    if (!host || !host->guest_known)
        return RNET_LAN_DIRECT_ERR_IO;
    if (build_ping(buf, sizeof(buf), rnet_os_monotonic_ms()) != 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return send_text(host->sock, &host->guest, buf);
}

int rnet_lan_direct_host_pump(RNetLanDirectHost *host, RNetLanLobby *room,
                              int *out_rtt_ms)
{
    char buf[RNET_DJ_MAX_PKT + 1];
    struct sockaddr_in src;
    int would_block = 0;
    int n;
    int changed = 0;
    char *cursor;
    const char *magic;
    const char *op;

    if (!host || !room || !rnet_os_socket_valid(host->sock))
        return 0;

    for (;;) {
        n = rnet_os_recvfrom(host->sock, buf, sizeof(buf) - 1, &src,
                             &would_block);
        if (n < 0) {
            if (would_block)
                break;
            break;
        }
        if (n == 0)
            continue;
        buf[n] = '\0';
        cursor = buf;
        magic = next_line(&cursor);
        op = next_line(&cursor);
        if (!magic || strcmp(magic, RNET_DJ_MAGIC) != 0 || !op)
            continue;

        if (strcmp(op, "PING") == 0) {
            const char *ts = next_line(&cursor);
            char reply[RNET_DJ_MAX_PKT];
            if (build_pong(reply, sizeof(reply), ts) == 0)
                (void)send_text(host->sock, &src, reply);
            continue;
        }
        if (strcmp(op, "PONG") == 0) {
            const char *ts = next_line(&cursor);
            if (out_rtt_ms)
                (void)rtt_from_pong_ts(ts, out_rtt_ms);
            continue;
        }

        if (strcmp(op, "JOIN_REQ") == 0) {
            const char *game = next_line(&cursor);
            const char *version = next_line(&cursor);
            const char *password = next_line(&cursor);
            const char *player = next_line(&cursor);
            char reply[RNET_DJ_MAX_PKT];
            const char *nak = NULL;

            if (!game)
                game = "";
            if (!version)
                version = "";
            if (!password)
                password = "";
            if (!player || !player[0])
                player = "Player";

            /* The seated guest asking again means our JOIN_OK was lost (it
             * retransmits until one lands): answer it again rather than
             * refusing it its own seat as "full". */
            if (host->guest_known && room->joiner_name[0] &&
                src.sin_addr.s_addr == host->guest.sin_addr.s_addr &&
                src.sin_port == host->guest.sin_port &&
                strcmp(password, room->password) == 0 && !room->started) {
                if (build_join_ok(reply, sizeof(reply), room) == 0)
                    (void)send_text(host->sock, &src, reply);
                continue;
            }
            if (strcmp(game, room->game) != 0 ||
                strcmp(version, room->game_version) != 0)
                nak = "identity";
            else if (strcmp(password, room->password) != 0)
                nak = "password";
            else if (room->started)
                nak = "started";
            else if (room->joiner_name[0] != '\0')
                nak = "full";

            if (nak) {
                if (build_join_nak(reply, sizeof(reply), nak) == 0)
                    (void)send_text(host->sock, &src, reply);
                continue;
            }

            snprintf(room->joiner_name, sizeof(room->joiner_name), "%s",
                     player);
            room->started = 0;
            host->guest = src;
            host->guest_known = 1;
            changed = 1;
            if (build_join_ok(reply, sizeof(reply), room) == 0)
                (void)send_text(host->sock, &src, reply);
        } else if (strcmp(op, "SWAPREQ") == 0) {
            /* Only the seated guest has a seat to trade. */
            if (!host->guest_known ||
                src.sin_addr.s_addr != host->guest.sin_addr.s_addr ||
                src.sin_port != host->guest.sin_port)
                continue;
            host->swap_req_pending = 1;
        } else if (strcmp(op, "CHATREQ") == 0) {
            /* The guest says what it said; the HOST says who said it. A
             * sender-supplied name could be anybody's, and the seat table is
             * the only thing that actually knows. */
            const char *player_id = next_line(&cursor);
            const char *raw = next_line(&cursor);
            char text[RNET_LAN_CHAT_TEXT_LEN];
            char reply[RNET_DJ_MAX_PKT];
            const char *from;
            chat_sanitize(raw, text, sizeof(text));
            if (!text[0])
                continue;
            /* Only from the seated guest: an unseated sender is not in this
             * room and has no seat name to speak under. */
            if (!host->guest_known ||
                src.sin_addr.s_addr != host->guest.sin_addr.s_addr ||
                src.sin_port != host->guest.sin_port)
                continue;
            from = room->joiner_name[0] ? room->joiner_name : "Player";
            chat_queue_push(&host->chat, player_id, from, text);
            /* Echoed back to the sender too -- that echo is the copy the
             * guest keeps, which is what puts both logs in one order. */
            if (build_chat(reply, sizeof(reply), player_id, from, text) == 0)
                (void)send_text(host->sock, &host->guest, reply);
        } else if (strcmp(op, "LEAVE") == 0) {
            /* Only the seated guest can give its seat up -- the same rule as
             * SWAPREQ and CHATREQ. An unseated sender (a guest whose re-join
             * was refused, a stray datagram) must not empty the seat of the
             * guest who holds it. */
            if (!host->guest_known ||
                src.sin_addr.s_addr != host->guest.sin_addr.s_addr ||
                src.sin_port != host->guest.sin_port)
                continue;
            if (room->joiner_name[0]) {
                room->joiner_name[0] = '\0';
                room->started = 0;
                host->guest_known = 0;
                changed = 1;
            }
        }
    }
    return changed;
}

int rnet_lan_direct_host_notify_start(RNetLanDirectHost *host,
                                      const RNetLanLobby *room)
{
    char buf[RNET_DJ_MAX_PKT];
    if (!host || !host->guest_known)
        return RNET_LAN_DIRECT_ERR_IO;
    if (build_start(buf, sizeof(buf), room) != 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return send_text(host->sock, &host->guest, buf);
}

int rnet_lan_direct_host_notify_caps(RNetLanDirectHost *host,
                                     const RNetLanLobby *room)
{
    char buf[RNET_DJ_MAX_PKT];
    if (!host || !host->guest_known)
        return RNET_LAN_DIRECT_ERR_IO;
    if (build_caps(buf, sizeof(buf), room) != 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return send_text(host->sock, &host->guest, buf);
}

int rnet_lan_direct_host_notify_kick(RNetLanDirectHost *host)
{
    char buf[RNET_DJ_MAX_PKT];
    if (!host || !host->guest_known)
        return RNET_LAN_DIRECT_ERR_IO;
    if (build_simple(buf, sizeof(buf), "KICK") != 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    (void)send_text(host->sock, &host->guest, buf);
    host->guest_known = 0;
    return RNET_LAN_DIRECT_OK;
}

int rnet_lan_direct_host_notify_close(RNetLanDirectHost *host)
{
    char buf[RNET_DJ_MAX_PKT];
    if (!host)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    if (!host->guest_known)
        return RNET_LAN_DIRECT_OK;
    if (build_simple(buf, sizeof(buf), "CLOSE") != 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    (void)send_text(host->sock, &host->guest, buf);
    host->guest_known = 0;
    return RNET_LAN_DIRECT_OK;
}

/* JOIN_REQ is retransmitted this often while no answer has come: UDP can drop
 * the first probe, and on a rematch the host may not be listening yet. */
#define RNET_DJ_JOIN_RESEND_MS 400

static void guest_fill_room_from_join_ok(const RNetLanDirectGuest *g,
                                         char *cursor, RNetLanLobby *out_room)
{
    const char *endpoint = next_line(&cursor);
    const char *host_name = next_line(&cursor);
    const char *joiner = next_line(&cursor);
    const char *slot = next_line(&cursor);
    const char *name = next_line(&cursor);
    const char *game = next_line(&cursor);
    const char *version = next_line(&cursor);
    const char *delay_line = next_line(&cursor);
    const char *rollback_line = next_line(&cursor);
    const char *pred_line = next_line(&cursor);
    const char *variant_line = next_line(&cursor);
    const char *mod_set = next_line(&cursor);
    memset(out_room, 0, sizeof(*out_room));
    snprintf(out_room->mod_set, sizeof(out_room->mod_set), "%s", mod_set ? mod_set : "");
    snprintf(out_room->endpoint, sizeof(out_room->endpoint), "%s",
             endpoint && endpoint[0] ? endpoint : g->host_hostport);
    snprintf(out_room->host_name, sizeof(out_room->host_name), "%s",
             host_name ? host_name : "Host");
    snprintf(out_room->joiner_name, sizeof(out_room->joiner_name), "%s",
             joiner && joiner[0] ? joiner
                                 : (g->player_name[0] ? g->player_name
                                                      : "Player"));
    out_room->host_slot = (slot && strcmp(slot, "1") == 0) ? 1 : 0;
    snprintf(out_room->name, sizeof(out_room->name), "%s",
             name && name[0] ? name : "LAN Lobby");
    snprintf(out_room->game, sizeof(out_room->game), "%s",
             game && game[0] ? game : g->expected_game);
    snprintf(out_room->game_version, sizeof(out_room->game_version), "%s",
             version && version[0] ? version : g->expected_version);
    out_room->started = 0;
    out_room->password[0] = '\0';
    out_room->input_delay = parse_direct_input_delay_line(delay_line, 2);
    /* Optional V3 lines — older hosts omit them. */
    out_room->rollback = parse_direct_bool_line(rollback_line, 0);
    out_room->input_prediction = parse_direct_prediction_line(pred_line, 4);
    out_room->session_variant = variant_line ? (int)strtol(variant_line, NULL, 10) : 0;
}

int rnet_lan_direct_guest_join_begin(const char *host_hostport,
                                     const char *expected_game,
                                     const char *expected_version,
                                     const char *password,
                                     const char *player_name,
                                     const char *guest_bind_hostport,
                                     RNetLanDirectGuest **out_guest)
{
    RNetLanDirectGuest *g;
    char host[128];
    char bind_host[128];
    rnet_u16 port = 0;
    rnet_u16 bind_port = 0;
    struct sockaddr_in bind_addr;
    int rc;

    if (!out_guest || !host_hostport || !host_hostport[0] ||
        !expected_game || !expected_game[0])
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    *out_guest = NULL;

    rnet_os_startup();
    if (rnet_os_parse_hostport(host_hostport, host, sizeof(host), &port) != 0 ||
        !host[0] || port == 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;

    g = (RNetLanDirectGuest *)calloc(1, sizeof(*g));
    if (!g)
        return RNET_LAN_DIRECT_ERR_IO;
    if (rnet_os_resolve_sockaddr(host, port, &g->host) != 0) {
        free(g);
        return RNET_LAN_DIRECT_ERR_IO;
    }
    snprintf(g->host_hostport, sizeof(g->host_hostport), "%s", host_hostport);
    snprintf(g->expected_game, sizeof(g->expected_game), "%s", expected_game);
    snprintf(g->expected_version, sizeof(g->expected_version), "%s",
             expected_version ? expected_version : "");
    snprintf(g->player_name, sizeof(g->player_name), "%s",
             player_name ? player_name : "");

    g->sock = rnet_os_socket_create_dgram();
    if (!rnet_os_socket_valid(g->sock)) {
        free(g);
        return RNET_LAN_DIRECT_ERR_IO;
    }
    (void)rnet_os_setsockopt_reuseaddr(g->sock, 1);

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    if (guest_bind_hostport && guest_bind_hostport[0] &&
        rnet_os_parse_hostport(guest_bind_hostport, bind_host,
                               sizeof(bind_host), &bind_port) == 0 &&
        bind_port != 0) {
        if (rnet_os_resolve_sockaddr(bind_host[0] ? bind_host : "0.0.0.0",
                                     bind_port, &bind_addr) != 0) {
            rnet_os_socket_destroy(&g->sock);
            free(g);
            return RNET_LAN_DIRECT_ERR_IO;
        }
    } else {
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        bind_addr.sin_port = 0;
    }
    if (rnet_os_bind(g->sock, &bind_addr) != 0 ||
        rnet_os_set_nonblocking(g->sock) != 0) {
        rnet_os_socket_destroy(&g->sock);
        free(g);
        return RNET_LAN_DIRECT_ERR_IO;
    }

    rc = build_join_req(g->join_req, sizeof(g->join_req), expected_game,
                        expected_version, password, player_name);
    if (rc != RNET_LAN_DIRECT_OK) {
        rnet_lan_direct_guest_close(&g);
        return rc;
    }
    g->joining = 1;
    g->join_sent_ms = rnet_os_monotonic_ms();
    (void)send_text(g->sock, &g->host, g->join_req);
    *out_guest = g;
    return RNET_LAN_DIRECT_OK;
}

int rnet_lan_direct_guest_join_poll(RNetLanDirectGuest *guest,
                                    RNetLanLobby *out_room)
{
    char buf[RNET_DJ_MAX_PKT + 1];
    rnet_u64 now;

    if (!guest || !out_room)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    if (!guest->joining)
        return RNET_LAN_DIRECT_OK; /* seated already */
    if (guest->join_failed)
        return guest->join_failed;
    if (!rnet_os_socket_valid(guest->sock))
        return RNET_LAN_DIRECT_ERR_IO;

    for (;;) {
        struct sockaddr_in src;
        int would_block = 0;
        int n;
        char *cursor;
        const char *magic;
        const char *op;

        n = rnet_os_recvfrom(guest->sock, buf, sizeof(buf) - 1, &src,
                             &would_block);
        /* Nothing (or an ICMP port-unreachable surfacing as an error on a
         * connectionless socket, which is what a host that is not listening
         * YET looks like): not an answer, keep asking. */
        if (n <= 0)
            break;
        buf[n] = '\0';
        cursor = buf;
        magic = next_line(&cursor);
        op = next_line(&cursor);
        if (!magic || strcmp(magic, RNET_DJ_MAGIC) != 0 || !op)
            continue;
        if (strcmp(op, "JOIN_NAK") == 0) {
            guest->join_failed = nak_code_to_rc(next_line(&cursor));
            return guest->join_failed;
        }
        if (strcmp(op, "JOIN_OK") != 0)
            continue; /* nothing else is addressed to an unseated guest */
        guest_fill_room_from_join_ok(guest, cursor, out_room);
        guest->host = src; /* reply path may differ from typed dest after NAT */
        guest->joining = 0;
        return RNET_LAN_DIRECT_OK;
    }

    now = rnet_os_monotonic_ms();
    if (now - guest->join_sent_ms >= RNET_DJ_JOIN_RESEND_MS) {
        guest->join_sent_ms = now;
        (void)send_text(guest->sock, &guest->host, guest->join_req);
    }
    return RNET_LAN_DIRECT_PENDING;
}

int rnet_lan_direct_guest_join(const char *host_hostport,
                               const char *expected_game,
                               const char *expected_version,
                               const char *password, const char *player_name,
                               const char *guest_bind_hostport, int timeout_ms,
                               RNetLanLobby *out_room,
                               RNetLanDirectGuest **out_guest)
{
    RNetLanDirectGuest *g = NULL;
    rnet_u64 deadline;
    int rc;

    if (!out_guest || !out_room)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    *out_guest = NULL;
    memset(out_room, 0, sizeof(*out_room));
    if (timeout_ms <= 0)
        timeout_ms = 2000;

    rc = rnet_lan_direct_guest_join_begin(host_hostport, expected_game,
                                          expected_version, password,
                                          player_name, guest_bind_hostport, &g);
    if (rc != RNET_LAN_DIRECT_OK)
        return rc;
    deadline = rnet_os_monotonic_ms() + (rnet_u64)timeout_ms;
    for (;;) {
        rnet_u64 now;
        int wait_ms;
        rc = rnet_lan_direct_guest_join_poll(g, out_room);
        if (rc != RNET_LAN_DIRECT_PENDING)
            break;
        now = rnet_os_monotonic_ms();
        if (now >= deadline) {
            rc = RNET_LAN_DIRECT_ERR_TIMEOUT;
            break;
        }
        wait_ms = (int)(deadline - now);
        if (wait_ms > RNET_DJ_JOIN_RESEND_MS)
            wait_ms = RNET_DJ_JOIN_RESEND_MS;
        (void)rnet_os_poll_recv(g->sock, wait_ms);
    }
    if (rc != RNET_LAN_DIRECT_OK) {
        rnet_lan_direct_guest_close(&g);
        memset(out_room, 0, sizeof(*out_room));
        return rc;
    }
    *out_guest = g;
    return RNET_LAN_DIRECT_OK;
}

void rnet_lan_direct_guest_close(RNetLanDirectGuest **guest)
{
    if (!guest || !*guest)
        return;
    rnet_os_socket_destroy(&(*guest)->sock);
    free(*guest);
    *guest = NULL;
}

int rnet_lan_direct_guest_ping(RNetLanDirectGuest *guest)
{
    char buf[RNET_DJ_MAX_PKT];
    if (!guest || !rnet_os_socket_valid(guest->sock) || guest->joining)
        return RNET_LAN_DIRECT_ERR_IO;
    if (build_ping(buf, sizeof(buf), rnet_os_monotonic_ms()) != 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return send_text(guest->sock, &guest->host, buf);
}

int rnet_lan_direct_guest_pump(RNetLanDirectGuest *guest, RNetLanLobby *room,
                               int *out_rtt_ms)
{
    char buf[RNET_DJ_MAX_PKT + 1];
    struct sockaddr_in src;
    int would_block = 0;
    int n;
    char *cursor;
    const char *magic;
    const char *op;

    if (!guest || !rnet_os_socket_valid(guest->sock))
        return RNET_LAN_DIRECT_ERR_IO;
    if (guest->joining)
        return 0; /* not seated yet: rnet_lan_direct_guest_join_poll reads */

    n = rnet_os_recvfrom(guest->sock, buf, sizeof(buf) - 1, &src, &would_block);
    if (n < 0)
        return would_block ? 0 : RNET_LAN_DIRECT_ERR_IO;
    if (n == 0)
        return 0;
    buf[n] = '\0';
    cursor = buf;
    magic = next_line(&cursor);
    op = next_line(&cursor);
    if (!magic || strcmp(magic, RNET_DJ_MAGIC) != 0 || !op)
        return 0;
    if (strcmp(op, "PING") == 0) {
        const char *ts = next_line(&cursor);
        char reply[RNET_DJ_MAX_PKT];
        if (build_pong(reply, sizeof(reply), ts) == 0)
            (void)send_text(guest->sock, &src, reply);
        return 0;
    }
    if (strcmp(op, "PONG") == 0) {
        const char *ts = next_line(&cursor);
        if (out_rtt_ms && rtt_from_pong_ts(ts, out_rtt_ms))
            return 3;
        return 0;
    }
    if (strcmp(op, "ROOM") == 0) {
        const char *host_slot = next_line(&cursor);
        const char *host_name = next_line(&cursor);
        const char *joiner_name = next_line(&cursor);
        const char *started = next_line(&cursor);
        if (src.sin_addr.s_addr != guest->host.sin_addr.s_addr ||
            src.sin_port != guest->host.sin_port)
            return 0;
        if (room) {
            room->host_slot = (host_slot && host_slot[0] == '1') ? 1 : 0;
            if (host_name)
                snprintf(room->host_name, sizeof(room->host_name), "%s", host_name);
            if (joiner_name)
                snprintf(room->joiner_name, sizeof(room->joiner_name), "%s",
                         joiner_name);
            room->started = (started && started[0] == '1') ? 1 : 0;
        }
        return 0;
    }
    if (strcmp(op, "SWAPRES") == 0) {
        const char *accept = next_line(&cursor);
        if (src.sin_addr.s_addr != guest->host.sin_addr.s_addr ||
            src.sin_port != guest->host.sin_port)
            return 0;
        guest->swap_res_pending = 1;
        guest->swap_res_accept = (accept && accept[0] == '1') ? 1 : 0;
        return 0;
    }
    if (strcmp(op, "CHAT") == 0) {
        /* Only from the host: it is the authority for what was said and by
         * whom, and a line from anywhere else is not part of this room. */
        const char *player_id = next_line(&cursor);
        const char *from = next_line(&cursor);
        const char *raw = next_line(&cursor);
        char text[RNET_LAN_CHAT_TEXT_LEN];
        if (src.sin_addr.s_addr != guest->host.sin_addr.s_addr ||
            src.sin_port != guest->host.sin_port)
            return 0;
        chat_sanitize(raw, text, sizeof(text));
        chat_queue_push(&guest->chat, player_id, from, text);
        return 0;
    }
    if (strcmp(op, "START") == 0) {
        const char *delay_line = next_line(&cursor);
        const char *rollback_line = next_line(&cursor);
        const char *pred_line = next_line(&cursor);
        const char *session_line = next_line(&cursor);
        const char *variant_line = next_line(&cursor);
        const char *mod_set = next_line(&cursor);
        if (room) {
            snprintf(room->mod_set, sizeof(room->mod_set), "%s", mod_set ? mod_set : "");
            room->started = 1;
            room->session_variant = variant_line ? (int)strtol(variant_line, NULL, 10) : 0;
            /* 0 from a host that predates V4 (no line): the caller decides. */
            room->session_id = session_line && session_line[0]
                                   ? (rnet_u32)strtoul(session_line, NULL, 10)
                                   : 0u;
            room->input_delay = parse_direct_input_delay_line(delay_line, 2);
            room->rollback = parse_direct_bool_line(rollback_line, room->rollback);
            room->input_prediction =
                parse_direct_prediction_line(pred_line, room->input_prediction);
        }
        return 1;
    }
    if (strcmp(op, "CAPS") == 0) {
        const char *delay_line = next_line(&cursor);
        const char *rollback_line = next_line(&cursor);
        const char *pred_line = next_line(&cursor);
        const char *variant_line = next_line(&cursor);
        const char *mod_set = next_line(&cursor);
        if (room) {
            snprintf(room->mod_set, sizeof(room->mod_set), "%s", mod_set ? mod_set : "");
            room->session_variant = variant_line ? (int)strtol(variant_line, NULL, 10) : 0;
            room->input_delay = parse_direct_input_delay_line(delay_line, 2);
            room->rollback = parse_direct_bool_line(rollback_line, room->rollback);
            room->input_prediction =
                parse_direct_prediction_line(pred_line, room->input_prediction);
        }
        return 0;
    }
    if (strcmp(op, "KICK") == 0 || strcmp(op, "CLOSE") == 0)
        return 2;
    return 0;
}

int rnet_lan_direct_host_send_chat(RNetLanDirectHost *host,
                                   const char *player_id, const char *from,
                                   const char *text)
{
    char buf[RNET_DJ_MAX_PKT];
    char line[RNET_LAN_CHAT_TEXT_LEN];
    if (!host)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    chat_sanitize(text, line, sizeof(line));
    if (!line[0])
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    /* Kept locally first, so the host's own log is complete whether or not a
     * guest is seated to send it to. */
    chat_queue_push(&host->chat, player_id, from, line);
    if (!host->guest_known)
        return RNET_LAN_DIRECT_OK;
    if (build_chat(buf, sizeof(buf), player_id, from, line) != 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return send_text(host->sock, &host->guest, buf);
}

int rnet_lan_direct_host_take_chat(RNetLanDirectHost *host,
                                   RNetLanChatLine *out)
{
    if (!host)
        return 0;
    return chat_queue_take(&host->chat, out);
}

int rnet_lan_direct_host_notify_room(RNetLanDirectHost *host,
                                     const RNetLanLobby *room)
{
    char buf[RNET_DJ_MAX_PKT];
    if (!host || !room)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    if (!host->guest_known)
        return RNET_LAN_DIRECT_OK;
    if (build_room(buf, sizeof(buf), room) != 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return send_text(host->sock, &host->guest, buf);
}

int rnet_lan_direct_host_take_swap_request(RNetLanDirectHost *host)
{
    if (!host || !host->swap_req_pending)
        return 0;
    host->swap_req_pending = 0;
    return 1;
}

int rnet_lan_direct_host_send_swap_result(RNetLanDirectHost *host, int accept)
{
    char buf[RNET_DJ_MAX_PKT];
    if (!host)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    if (!host->guest_known)
        return RNET_LAN_DIRECT_OK;
    if (build_swap_res(buf, sizeof(buf), accept) != 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return send_text(host->sock, &host->guest, buf);
}

int rnet_lan_direct_guest_send_swap_request(RNetLanDirectGuest *guest)
{
    char buf[RNET_DJ_MAX_PKT];
    if (!guest)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    if (guest->joining)
        return RNET_LAN_DIRECT_ERR_IO; /* no seat to trade yet */
    if (build_swap_req(buf, sizeof(buf)) != 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return send_text(guest->sock, &guest->host, buf);
}

int rnet_lan_direct_guest_take_swap_result(RNetLanDirectGuest *guest, int *accept)
{
    if (!guest || !guest->swap_res_pending)
        return 0;
    guest->swap_res_pending = 0;
    if (accept)
        *accept = guest->swap_res_accept;
    return 1;
}

int rnet_lan_direct_guest_send_chat(RNetLanDirectGuest *guest,
                                    const char *player_id, const char *text)
{
    char buf[RNET_DJ_MAX_PKT];
    char line[RNET_LAN_CHAT_TEXT_LEN];
    if (!guest)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    if (guest->joining)
        return RNET_LAN_DIRECT_ERR_IO; /* not in the room yet */
    chat_sanitize(text, line, sizeof(line));
    if (!line[0])
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    if (build_chat_req(buf, sizeof(buf), player_id, line) != 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    /* Deliberately NOT queued locally: the host echoes it back, and that echo
     * is the copy this side keeps. Appending here as well would show the line
     * twice, and show it in an order the host never agreed to. */
    return send_text(guest->sock, &guest->host, buf);
}

int rnet_lan_direct_guest_take_chat(RNetLanDirectGuest *guest,
                                    RNetLanChatLine *out)
{
    if (!guest)
        return 0;
    return chat_queue_take(&guest->chat, out);
}

int rnet_lan_direct_guest_leave(RNetLanDirectGuest *guest)
{
    char buf[RNET_DJ_MAX_PKT];
    if (!guest || !rnet_os_socket_valid(guest->sock))
        return RNET_LAN_DIRECT_ERR_IO;
    if (guest->joining)
        return RNET_LAN_DIRECT_OK; /* never seated: nothing to give up */
    if (build_simple(buf, sizeof(buf), "LEAVE") != 0)
        return RNET_LAN_DIRECT_ERR_ARGUMENT;
    return send_text(guest->sock, &guest->host, buf);
}
