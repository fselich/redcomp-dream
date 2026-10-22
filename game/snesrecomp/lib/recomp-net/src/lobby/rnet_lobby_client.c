/* rnet_lobby_client.c -- see recomp_net/lobby_client.h.
 *
 * Lifted from snesrecomp runner/src/lobby/snes_lobby_client.c (snesrecomp
 * 36d6ce5). The history of every rule below -- and most of the comments --
 * came with it; "this title" in a comment means whichever title links this.
 */
#include "recomp_net/lobby_client.h"
#include "recomp_net/auth.h"   /* optional Discord session for `hello` */

#include "recomp_net/ice_xfer.h"
#include "recomp_net/chat_filter.h"
#include "recomp_net/chat_report.h"

#include <ctype.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#include "recomp_net/rnet_ws.h"
#include "recomp_net/rnet_sha1.h"
#include "recomp_net/address.h"
#include "recomp_net/host_relay.h"
#include "recomp_net/host_ice.h"
#include "recomp_net/ice_rtt.h"
#include "nat/rnet_sig_hold.h"

/* The fallback release pin, for a build that never said what it is. The
 * server normalises an empty version to the same word. */
#define RNET_LOBBY_DEFAULT_VERSION "dev"

/* ── Configuration (rnet_lobby_configure) ─────────────────────────────────
 * File scope and never touched by rnet_lobby_disconnect: it describes the
 * build, not a connection. */
static struct {
    char platform[16];
    int  max_players;
    int  automatch_slots;
    char legacy_env_prefix[24];
} g_cfg = { "unknown", 4, 2, "" };

static RNetLobbyCapsCodec g_codec;

/* An environment knob by its RNET_LOBBY_* name, else by the legacy spelling
 * the engine registered (e.g. SNES_NET_LOBBY_URL). The generic name wins; a
 * disagreement is said once, because two spellings of one setting quietly
 * pointing at different things is the bug an alias must not introduce. */
static const char *lobby_env(const char *rnet_name, const char *legacy_suffix)
{
    const char *v = getenv(rnet_name);
    const char *legacy = NULL;
    if (g_cfg.legacy_env_prefix[0] && legacy_suffix) {
        char name[64];
        snprintf(name, sizeof(name), "%s%s", g_cfg.legacy_env_prefix,
                 legacy_suffix);
        legacy = getenv(name);
        if (legacy && !legacy[0]) legacy = NULL;
        if (v && v[0] && legacy && strcmp(v, legacy) != 0) {
            static int said;
            if (!said++)
                fprintf(stderr, "rnet_lobby: %s=\"%s\" and %s=\"%s\" disagree; "
                                "using %s\n", rnet_name, v, name, legacy,
                        rnet_name);
        }
    }
    if (v && v[0]) return v;
    return legacy;
}

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "platform/rnet_platform.h"

static int socket_would_block(void)
{
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

typedef struct {
    int fd;
    int connected;
    int handshake_done;
    char player_id[RNET_LOBBY_ID_LEN];
    char display_name[RNET_LOBBY_NAME_LEN];
    char host[128];
    int port;
    char path[128];
    char url[256]; /* full WS URL passed to connect */
    char rx_http[4096];
    size_t rx_http_len;
    /* Bytes that arrived with the HTTP 101 response after the header end. */
    uint8_t ws_pending[4096];
    size_t ws_pending_len;
    RNetLobbyRow list[RNET_LOBBY_MAX_LIST];
    int list_count;
    RNetLobbyOnlinePlayer online[RNET_LOBBY_MAX_ONLINE];
    int online_count;
    int in_lobby;
    int is_host;
    char host_player_id[RNET_LOBBY_ID_LEN];
    char my_bind[RNET_LOBBY_ENDPOINT_LEN];
    char filter_game_name[RNET_LOBBY_NAME_LEN];
    char filter_game_version[RNET_LOBBY_VERSION_LEN];
    RNetLobbyJoinInfo join;
    RNetLobbyMember members[RNET_LOBBY_MAX_MEMBERS];
    int member_count;
    /* Lobby chat ring (oldest at chat_head). */
    RNetLobbyChatMsg chat[RNET_LOBBY_CHAT_RING];
    /* Server (per-game) chat: a second ring with its own sequence. */
    RNetLobbyChatMsg schat[RNET_LOBBY_CHAT_RING];
    int schat_head;
    int schat_count;
    uint32_t schat_seq;
    /* Seat swap: one pending ask aimed at us, one outgoing result. */
    int  swap_in_valid;
    char swap_in_asker_id[RNET_LOBBY_ID_LEN];
    char swap_in_asker_name[RNET_LOBBY_NAME_LEN];
    int  swap_in_from_slot;
    int  swap_out;
    int chat_head;
    int chat_count;
    uint32_t chat_seq;
    int local_ready;
    int all_ready;
    int launch_pending;
    RNetLobbyMatchCaps match_caps;
    /* The server's need_mods refusal: what it says we are missing, and the
     * identity of the peer that could supply it. Kept rather than reduced to
     * an error code because "you cannot join" is not actionable without the
     * list. */
    /* What each seated peer says it has, from the server's per-slot echo of
     * their set_ready offer. The input to the launch gate: the host compares
     * every peer against the plan and holds the match until they can run it. */
    RNetLobbyModPkg member_offer[RNET_LOBBY_MAX_MEMBERS][RNET_LOBBY_MAX_MODS];
    int member_offer_count[RNET_LOBBY_MAX_MEMBERS];
    /* Mod transfer: one at a time, in one direction. A second request while
     * one is in flight is refused rather than queued -- two agents on one
     * relay is a harder thing to get right than making the player click
     * again, and the panel shows which row is moving. */
    RNetIceXfer *xfer;
    int   xfer_busy;
    int   xfer_sending;              /* 1 host side, 0 guest side */
    int   xfer_progress;             /* -1 idle, -2 failed, 0..100 */
    char  xfer_peer[RNET_LOBBY_ID_LEN];
    char  xfer_id[RNET_LOBBY_MOD_ID_LEN];
    char  xfer_ver[RNET_LOBBY_MOD_VER_LEN];
    char  xfer_sha[65];
    uint32_t xfer_expect;
    char  xfer_err[256];
    /* HOST side: the packed archive, waiting for the path to be known.
     *
     * The size cap depends on whether the pair turns out to be relayed, and
     * that is not decided until ICE connects. So the export happens on the
     * request (it is local work and it is what tells us the size) but the
     * bytes are held here until the pump can price the path. Owned by us
     * until queued; mod_xfer_reset frees it. */
    uint8_t *xfer_hold;
    size_t   xfer_hold_len;
    char     xfer_hold_hdr[512];
    int      xfer_path_priced;   /* 1 once the cap decision has been made */
    char  ice_stun[128], ice_turn[128], ice_user[192], ice_pass[128];
    /* ICE signals that arrived before the agent existed.
     *
     * The requester opens its agent and starts gathering the moment it asks,
     * so its offer and first candidates are already crossing the relay while
     * the sender is still packing the archive. Dropped, they are simply gone
     * -- libjuice will not re-send them -- and the handshake stalls until it
     * times out. Held here and replayed in arrival order once the agent
     * exists. One bucket per sender: a second peer's signals neither evict
     * nor get replayed into the first's negotiation. */
    RNetSigHold sig_hold;   /* per sending peer; see nat/rnet_sig_hold.h */
    int      xfer_last_state;    /* last RNetIceState logged */
    uint64_t xfer_started_ms;    /* for the stall watchdog */
    uint64_t xfer_connected_ms;
    RNetLobbyModPkg need_mods[RNET_LOBBY_MAX_MODS];
    int need_mods_count;
    int need_mods_can_transfer;
    char need_mods_lobby_id[RNET_LOBBY_ID_LEN];
    char need_mods_host_player_id[RNET_LOBBY_ID_LEN];
    /* Outbound frames waiting for the handshake. Sized for the largest frame
     * any op builds (set_blocks at 256 ids, ~10.5 KB; create with a full caps
     * object, ~5 KB): a frame is queued whole or refused, never cut. */
    char pending_tx[8][RNET_LOBBY_TX_MAX];
    int pending_n;
    /* After the handshake every frame goes through this frame-atomic buffer
     * (snesrecomp#104): the socket is non-blocking, and a would-block used to
     * leave half a frame on the wire with the next frame written after it.
     * tx_failed latches a hard socket error or a backlog past the cap; the
     * next pump disconnects (never mid-op: disconnect wipes this struct). */
    RNetWsTx tx;
    int tx_failed;
    char tx_error[96];
    /* Inbound ICE signals (WS op:signal). */
    struct {
        int type;
        int flag;
        char text[2048];
    } sig_q[32];
    int sig_head;
    int sig_tail;
    int sig_count;
    /* Coturn mint from WS get_turn_credentials. */
    RNetLobbyTurnCredentials turn;
    time_t turn_received_at;
    int turn_request_pending;
    /* Waiting-room latency (ms) keyed by pad slot; -1 = unknown. */
    int member_rtt_ms[RNET_LOBBY_MAX_MEMBERS];
    uint64_t rtt_next_ping_ms;
} LobbyClient;

enum {
    RNET_LOBBY_SIG_RTT_PING = 100,
    RNET_LOBBY_SIG_RTT_PONG = 101,
    RNET_LOBBY_SIG_RTT_REPORT = 102,

    /* Mod transfer.
     *
     * These ride the ordinary seated `signal` relay, which the lobby server
     * forwards between seated members verbatim -- so a transfer needs no
     * server support at all. The server's own mod_xfer_* ops cannot serve
     * this: it grants that channel only to a peer it REFUSED to seat, and
     * this title seats everyone on purpose.
     *
     * Only SDP and ICE candidates travel here. The package itself goes over
     * the direct peer-to-peer connection those negotiate, so no file byte
     * ever reaches the lobby server. */
    RNET_LOBBY_SIG_MOD_REQ = 110,   /* guest -> host: "<id>@<ver>"           */
    RNET_LOBBY_SIG_MOD_NAK = 111,   /* host -> guest: refusal, text = reason */
    /* 120 + RNetSignalType(1..6): the ICE handshake for the transfer agent.
     * Offset so the game's own netplay ICE, which uses the bare types on the
     * same relay, can never ingest one of ours nor we one of its. */
    RNET_LOBBY_SIG_MOD_ICE_BASE = 120
};

/* The platform clock, not clock_gettime: MinGW resolves that through
 * winpthread, which recomp_net does not link, and MSVC has no CLOCK_MONOTONIC
 * at all, which fell back to time(NULL) -- whole seconds, for an RTT probe. */
static uint64_t lobby_mono_ms(void)
{
    return (uint64_t)rnet_os_monotonic_ms();
}

/* Defined later; used by waiting-room RTT signal handling. */
int rnet_lobby_send_signal(int type, int flag, const char *text);


/* Host's preference for ICE as the host relay's path (see host_ice_step). */
static int g_relay_via_ice_pref = 1;
static void ice_launch_capture(void);
static int room_relays_via_ice(void);

static LobbyClient g_lc = {
    .fd = -1,
    .filter_game_version = RNET_LOBBY_DEFAULT_VERSION,
};

/* ── Automatch state ────────────────────────────────────────────────────────
 *
 * Deliberately its own struct rather than more fields on LobbyClient: none of
 * it is lobby membership, its lifetime is the queue rather than the room, and
 * a `joined` arriving from a pairing must clear it without disturbing the room
 * state that the same message is setting up.
 */
typedef struct {
    int  have_rulesets;          /* a rulesets_ok has been seen at all */
    /* A rulesets query is out. The launcher polls availability EVERY
     * FRAME while the netplay page is up, so without this the gap
     * before the first reply becomes a request per frame -- measured
     * as seven answers to one question. */
    int  rulesets_in_flight;
    int  ruleset_count;
    RNetLobbyRuleset rulesets[RNET_LOBBY_MAX_RULESETS];

    int  state;                  /* RNET_LOBBY_AUTOMATCH_* */
    char ticket_id[RNET_LOBBY_ID_LEN];
    int  queued_secs;
    int  pool;
    char error[160];

    RNetLobbyAutomatchFound found;
    /* When the offer lapses, as a monotonic timestamp rather than the count
     * the server sent. The server states the deadline once; a client that
     * stored the number and showed it unchanged would display "15s" for the
     * whole fifteen seconds, which reads as a frozen dialog rather than a
     * deadline. Counting locally also means the display ticks smoothly
     * instead of jumping with a 1 Hz push. */
    uint64_t found_deadline_ms;

    /* Where to send the latency probe, published on rulesets_ok and again on
     * automatch_queued. Kept from whichever arrived last. */
    char probe_host[128];
    int  probe_port;
    unsigned probe_magic;
    int  probe_type;
    /* The measurement, and the nonce that identifies our outstanding probe. */
    int      rtt_ms;             /* <0 = not measured yet */
    uint32_t probe_nonce;
    uint64_t probe_sent_ms;      /* 0 = none outstanding */
    int      probe_socket;       /* -1 = not open */
    int      rtt_reported;       /* the server has our number */
    /* A queue op is out and its answer -- automatch_queued, or an error --
     * has not arrived. Without it a refusal of the FIRST queue attempt
     * (need_account is the common one) would arrive while state is still
     * IDLE and be filed as somebody else's error. */
    int      queue_in_flight;
    /* This seat came from a pairing, not from a room somebody hosts. */
    int      in_automatch_room;
} LobbyAutomatch;

static LobbyAutomatch g_am = { .rtt_ms = -1, .probe_socket = -1 };

/* Round trip to the lobby server, timed with the server's own ping/pong over
 * this WebSocket, and reported (op automatch_rtt) so the server can publish
 * each room host's figure in lobby_list. A room's latency before joining is
 * then estimated as ours + the host's -- the fallback for a room the list
 * probe (below) could not reach directly. */
/* Once per connection and once per list request -- never on a timer. */
#define SERVER_PING_TIMEOUT_MS 10000u
static int g_srv_rtt_ms = -1;
static int g_srv_rtt_reported = -1;
static uint64_t g_srv_ping_sent_ms;
static int g_srv_ping_wanted = 1;
/* lobby_list host_rtt_ms per row (parallel to g_lc.list), -1 unknown. */
static int g_list_host_rtt[RNET_LOBBY_MAX_LIST];

static void automatch_reset_queue_state(void)
{
    g_am.state = RNET_LOBBY_AUTOMATCH_IDLE;
    g_am.queue_in_flight = 0;
    g_am.ticket_id[0] = '\0';
    g_am.queued_secs = 0;
    g_am.pool = 0;
    g_am.rtt_reported = 0;
    memset(&g_am.found, 0, sizeof(g_am.found));
}

static void automatch_fail(const char *why)
{
    g_am.state = RNET_LOBBY_AUTOMATCH_FAILED;
    snprintf(g_am.error, sizeof(g_am.error), "%s", why ? why : "automatch failed");
    fprintf(stderr, "rnet_lobby: automatch failed: %s\n", g_am.error);
}

/* ── Latency probe ──────────────────────────────────────────────────────────
 *
 * Every online match goes through one relay, so the only latency that matters
 * is each peer -> relay, and a ticket can be qualified on its own before any
 * pairing. The relay answers packet type 200 with 201 -- the same 14 bytes
 * back, nonce included -- BEFORE any session lookup, precisely so a client
 * sitting in a queue with no session can measure the path.
 *
 * Fire-and-forget over an unconnected UDP socket, polled from the same pump
 * as the WebSocket. A reply that never comes costs one socket and a number
 * that stays -1: a ticket that has not measured is held out of pairing for a
 * few seconds and then matches anyway, so a silent relay delays a match
 * rather than preventing one.
 *
 * The value is CLIENT-REPORTED, and the server clamps it. Reporting high to
 * force delay on an opponent is the grief case; reporting low only stalls the
 * liar's own sim, which is its own answer.
 */
#define AUTOMATCH_PROBE_LEN 14

static int set_nonblock(int fd);   /* defined with the WS socket helpers */
static const char *effective_game_version(const char *override_ver);
/* Every free-text value in an outbound frame goes through json_escape, and
 * JSON_ESC_CAP is the buffer its result needs. Escaping at most doubles a
 * value (only " \\ \n \r \t expand, each to two bytes; anything else below
 * 0x20 is dropped), so 2x + 8 never truncates -- which matters most for a
 * password, where a dropped character would put a secret on the wire that is
 * not the one the host typed. Declared up here because the first frame that
 * needs it is built well above the definition. */
#define JSON_ESC_CAP(n) ((n) * 2 + 8)
static size_t json_escape(const char *in, char *out, size_t cap);
static void queue_send(const char *json);
static void flush_pending(void);
static int ws_send(const char *json);
static const char *json_get_str(const char *json, const char *key, char *out, size_t cap);
static int json_get_int(const char *json, const char *key, int def);
static int json_extract_object(const char *json, const char *key, char *out, size_t out_cap);
static size_t json_escape(const char *in, char *out, size_t cap);
static void parse_match_caps_object(const char *obj, RNetLobbyMatchCaps *out);

/* ---- Room list: direct latency to each room's host ----------------------
 *
 * When the list is requested (opened, or Refresh), each listed room gets one
 * short ICE probe to its host: the server relays the setup as probe_signal
 * (only between a browser and the host of a room it can see -- never a
 * password room), the two ends link directly, the browser takes a few pings
 * and closes it. That is the same path a join would use, so the figure is the
 * one the match will see; the server-relative estimate above stands in only
 * for a room whose probe could not link. Nothing runs on a timer: the
 * server's own list pushes reuse the last measurement.
 *
 * Probe signals carry the probe's generation in `flag`, so a reply to a probe
 * that a Refresh already replaced cannot land in its successor. */
#define LIST_PROBE_CONCURRENT 4    /* the server's signal burst is ~60 */
#define LIST_PROBE_SAMPLES 4
#define LIST_PROBE_TIMEOUT_MS 6000u
#define LIST_PROBE_MIN_GAP_MS 3000u  /* a Refresh hammered twice probes once */
#define LIST_ANSWER_MAX 8
#define LIST_ANSWER_TTL_MS 15000u

enum { LP_IDLE = 0, LP_QUEUED, LP_RUNNING, LP_DONE };

typedef struct ListProbe {
    char lobby_id[RNET_LOBBY_ID_LEN];
    int used;
    int state;
    int gen;
    int rtt_ms;            /* last finished measurement, -1 none */
    uint64_t started_ms;
    RNetIceRttProbe *probe;
} ListProbe;

typedef struct ListAnswer {
    char browser[RNET_LOBBY_ID_LEN];
    int used;
    int gen;
    uint64_t born_ms;
    RNetIceRttProbe *probe;
} ListAnswer;

static ListProbe g_lp[RNET_LOBBY_MAX_LIST];
static ListAnswer g_la[LIST_ANSWER_MAX];
static int g_list_probe_armed;
static int g_list_probe_gen;
static int g_list_probe_unsupported; /* this connection's server refused the op */

static int list_probe_send(const char *lobby_id, const char *to, int gen,
                           const RNetSignal *msg)
{
    char esc[4096];
    char lid_esc[JSON_ESC_CAP(RNET_LOBBY_ID_LEN)];
    char to_esc[JSON_ESC_CAP(RNET_LOBBY_ID_LEN)];
    char out[4608];
    if (msg->type != RNET_SIGNAL_LOCAL_SDP && msg->type != RNET_SIGNAL_LOCAL_CANDIDATE &&
        msg->type != RNET_SIGNAL_GATHERING_DONE)
        return -1;
    if (!rnet_lobby_connected())
        return -1;
    json_escape(msg->text, esc, sizeof(esc));
    json_escape(lobby_id, lid_esc, sizeof(lid_esc));
    json_escape(to ? to : "", to_esc, sizeof(to_esc));
    snprintf(out, sizeof(out),
             "{\"op\":\"probe_signal\",\"lobby_id\":\"%s\",\"to_player_id\":\"%s\","
             "\"type\":%d,\"flag\":%d,\"text\":\"%s\"}",
             lid_esc, to_esc, (int)msg->type, gen, esc);
    /* Immediately, as rnet_lobby_send_signal_to does: candidates burst. */
    if (g_lc.handshake_done && g_lc.fd >= 0)
        return ws_send(out) < 0 ? -1 : 0;
    queue_send(out);
    return 0;
}

static void list_probe_emit(const RNetSignal *msg, void *user)
{
    ListProbe *lp = (ListProbe *)user;
    if (lp && lp->used)
        (void)list_probe_send(lp->lobby_id, "", lp->gen, msg);
}

static void list_answer_emit(const RNetSignal *msg, void *user)
{
    ListAnswer *la = (ListAnswer *)user;
    if (la && la->used)
        (void)list_probe_send(g_lc.join.lobby_id, la->browser, la->gen, msg);
}

/* STUN only, like the waiting room's agents: a TURN relay would time the
 * relay, and the point is the path a join takes. */
static void list_probe_ice_config(RNetIceConfig *cfg, int controlling)
{
    const RNetLobbyTurnCredentials *tc = rnet_lobby_turn_credentials();
    rnet_ice_config_init_defaults(cfg);
    if (tc && tc->valid && tc->stun_host[0]) {
        cfg->stun_host = tc->stun_host;
        cfg->stun_port = (rnet_u16)(tc->stun_port > 0 ? tc->stun_port : 3478);
    }
    cfg->controlling = (rnet_u8)(controlling ? 1 : 0);
}

static void list_probe_finish(ListProbe *lp)
{
    const int ms = rnet_ice_rtt_ms(lp->probe);
    if (ms >= 0)
        lp->rtt_ms = ms;
    rnet_ice_rtt_close(&lp->probe);
    lp->state = LP_DONE;
}

static void list_answer_free(ListAnswer *la)
{
    rnet_ice_rtt_close(&la->probe);
    memset(la, 0, sizeof(*la));
}

static void list_probe_reset(void)
{
    int i;
    for (i = 0; i < RNET_LOBBY_MAX_LIST; ++i) {
        if (g_lp[i].probe)
            rnet_ice_rtt_close(&g_lp[i].probe);
        memset(&g_lp[i], 0, sizeof(g_lp[i]));
    }
    for (i = 0; i < LIST_ANSWER_MAX; ++i)
        if (g_la[i].used)
            list_answer_free(&g_la[i]);
    g_list_probe_armed = 0;
    g_list_probe_unsupported = 0;
}

/* Stop every probe in flight; keep any figures already measured. */
static void list_probe_unsupported(void)
{
    int i;
    g_list_probe_unsupported = 1;
    for (i = 0; i < RNET_LOBBY_MAX_LIST; ++i)
        if (g_lp[i].used && (g_lp[i].state == LP_QUEUED || g_lp[i].state == LP_RUNNING))
            list_probe_finish(&g_lp[i]);
}

static ListProbe *list_probe_find(const char *lobby_id)
{
    int i;
    for (i = 0; i < RNET_LOBBY_MAX_LIST; ++i)
        if (g_lp[i].used && strcmp(g_lp[i].lobby_id, lobby_id) == 0)
            return &g_lp[i];
    return NULL;
}

/* A lobby_list arrived. Forget rooms that left it; if a request armed it,
 * queue a probe to every room that can take one. */
static void list_probe_on_list(void)
{
    const uint64_t now = lobby_mono_ms();
    const int armed = g_list_probe_armed;
    int i, j;
    g_list_probe_armed = 0;
    for (i = 0; i < RNET_LOBBY_MAX_LIST; ++i) {
        int listed = 0;
        if (!g_lp[i].used)
            continue;
        for (j = 0; j < g_lc.list_count && !listed; ++j)
            listed = strcmp(g_lc.list[j].lobby_id, g_lp[i].lobby_id) == 0;
        if (!listed) {
            if (g_lp[i].probe)
                rnet_ice_rtt_close(&g_lp[i].probe);
            memset(&g_lp[i], 0, sizeof(g_lp[i]));
        }
    }
    if (!armed || g_list_probe_unsupported || !rnet_host_ice_available())
        return;
    for (j = 0; j < g_lc.list_count; ++j) {
        const RNetLobbyRow *row = &g_lc.list[j];
        ListProbe *lp;
        if (!row->lobby_id[0] || row->has_password)
            continue; /* the server refuses these; don't ask */
        if (g_lc.in_lobby && strcmp(row->lobby_id, g_lc.join.lobby_id) == 0)
            continue;
        lp = list_probe_find(row->lobby_id);
        if (!lp) {
            for (i = 0; i < RNET_LOBBY_MAX_LIST && g_lp[i].used; ++i) {}
            if (i == RNET_LOBBY_MAX_LIST)
                break;
            lp = &g_lp[i];
            memset(lp, 0, sizeof(*lp));
            lp->used = 1;
            lp->rtt_ms = -1;
            snprintf(lp->lobby_id, sizeof(lp->lobby_id), "%s", row->lobby_id);
        }
        if (lp->state == LP_QUEUED || lp->state == LP_RUNNING)
            continue;
        if (lp->state == LP_DONE && now - lp->started_ms < LIST_PROBE_MIN_GAP_MS)
            continue;
        lp->state = LP_QUEUED;
    }
}

static void list_probe_pump(void)
{
    const uint64_t now = lobby_mono_ms();
    int i, running = 0;
    if (!rnet_lobby_connected())
        return; /* the disconnected branch of the pump resets */
    for (i = 0; i < RNET_LOBBY_MAX_LIST; ++i) {
        ListProbe *lp = &g_lp[i];
        if (!lp->used || lp->state != LP_RUNNING)
            continue;
        rnet_ice_rtt_pump(lp->probe);
        if (rnet_ice_rtt_samples(lp->probe) >= LIST_PROBE_SAMPLES ||
            now - lp->started_ms > LIST_PROBE_TIMEOUT_MS ||
            rnet_ice_rtt_state(lp->probe) == RNET_ICE_STATE_FAILED)
            list_probe_finish(lp);
        else
            running++;
    }
    for (i = 0; i < RNET_LOBBY_MAX_LIST && running < LIST_PROBE_CONCURRENT; ++i) {
        ListProbe *lp = &g_lp[i];
        RNetIceConfig cfg;
        if (!lp->used || lp->state != LP_QUEUED)
            continue;
        g_list_probe_gen = g_list_probe_gen % 255 + 1;
        lp->gen = g_list_probe_gen;
        lp->started_ms = now;
        list_probe_ice_config(&cfg, 1);
        if (rnet_ice_rtt_open(&lp->probe, &cfg, list_probe_emit, lp) != 0) {
            lp->state = LP_DONE;
            continue;
        }
        lp->state = LP_RUNNING;
        running++;
    }
    for (i = 0; i < LIST_ANSWER_MAX; ++i) {
        ListAnswer *la = &g_la[i];
        if (!la->used)
            continue;
        /* Answer only while this is still an open room we host. */
        if (now - la->born_ms > LIST_ANSWER_TTL_MS || !g_lc.is_host || !g_lc.in_lobby ||
            g_lc.launch_pending) {
            list_answer_free(la);
            continue;
        }
        rnet_ice_rtt_pump(la->probe);
    }
}

static void list_probe_on_signal(const char *lobby_id, const char *from, int type,
                                 int flag, const char *text)
{
    RNetSignal sig;
    if (!lobby_id[0] || !from[0] || flag < 1 || flag > 255)
        return;
    if (type != RNET_SIGNAL_LOCAL_SDP && type != RNET_SIGNAL_LOCAL_CANDIDATE &&
        type != RNET_SIGNAL_GATHERING_DONE)
        return;
    memset(&sig, 0, sizeof(sig));
    sig.type = type == RNET_SIGNAL_LOCAL_SDP ? RNET_SIGNAL_REMOTE_SDP
             : type == RNET_SIGNAL_LOCAL_CANDIDATE ? RNET_SIGNAL_REMOTE_CANDIDATE
                                                   : RNET_SIGNAL_GATHERING_DONE;
    sig.peer_slot = 0xFF;
    snprintf(sig.text, sizeof(sig.text), "%s", text);
    if (g_lc.is_host && g_lc.in_lobby && strcmp(lobby_id, g_lc.join.lobby_id) == 0) {
        /* A browser probing our room. Its offer starts a fresh responder
         * (a Refresh re-probes); anything else joins the one it belongs to. */
        ListAnswer *la = NULL;
        int i;
        if (g_lc.launch_pending)
            return;
        for (i = 0; i < LIST_ANSWER_MAX; ++i)
            if (g_la[i].used && strcmp(g_la[i].browser, from) == 0)
                la = &g_la[i];
        if (type == RNET_SIGNAL_LOCAL_SDP && (!la || la->gen != flag)) {
            RNetIceConfig cfg;
            if (la) {
                list_answer_free(la);
            } else {
                ListAnswer *oldest = &g_la[0];
                for (i = 0; i < LIST_ANSWER_MAX && g_la[i].used; ++i)
                    if (g_la[i].born_ms < oldest->born_ms)
                        oldest = &g_la[i];
                la = i < LIST_ANSWER_MAX ? &g_la[i] : oldest;
                if (la->used)
                    list_answer_free(la);
            }
            la->used = 1;
            la->gen = flag;
            la->born_ms = lobby_mono_ms();
            snprintf(la->browser, sizeof(la->browser), "%s", from);
            list_probe_ice_config(&cfg, 0);
            if (rnet_ice_rtt_open(&la->probe, &cfg, list_answer_emit, la) != 0) {
                list_answer_free(la);
                return;
            }
        }
        if (la && la->probe && la->gen == flag)
            rnet_ice_rtt_push_signal(la->probe, &sig);
        return;
    }
    {
        /* The host's answer to one of ours. The server only lets a room's
         * host send here, and only to a browser that probed that room. */
        ListProbe *lp = list_probe_find(lobby_id);
        if (lp && lp->state == LP_RUNNING && lp->probe && lp->gen == flag)
            rnet_ice_rtt_push_signal(lp->probe, &sig);
    }
}

static void automatch_probe_close(void)
{
    if (g_am.probe_socket >= 0) {
        close(g_am.probe_socket);
        g_am.probe_socket = -1;
    }
    g_am.probe_sent_ms = 0;
}

/*
 * The 14-byte probe: magic, type, then a nonce.
 *
 * LITTLE-ENDIAN, because that is what the relay's header is (`read_u32_le` /
 * `read_u16_le` in input_relay.rs) -- and a big-endian magic is simply not
 * this protocol's magic, so the packet is dropped on the `magic` counter
 * with no reply and nothing to see from here but a timeout. The nonce sits at
 * offset 6, where an ordinary packet carries its session id; the relay echoes
 * those bytes untouched and only rewrites the type, so it comes back as sent.
 */
static void automatch_probe_pack(unsigned char *out, uint32_t nonce)
{
    const unsigned magic = g_am.probe_magic;
    const int type = g_am.probe_type ? g_am.probe_type : 200;
    memset(out, 0, AUTOMATCH_PROBE_LEN);
    out[0] = (unsigned char)(magic & 0xFF);
    out[1] = (unsigned char)((magic >> 8) & 0xFF);
    out[2] = (unsigned char)((magic >> 16) & 0xFF);
    out[3] = (unsigned char)((magic >> 24) & 0xFF);
    out[4] = (unsigned char)(type & 0xFF);
    out[5] = (unsigned char)((type >> 8) & 0xFF);
    out[6] = (unsigned char)(nonce & 0xFF);
    out[7] = (unsigned char)((nonce >> 8) & 0xFF);
    out[8] = (unsigned char)((nonce >> 16) & 0xFF);
    out[9] = (unsigned char)((nonce >> 24) & 0xFF);
}

static int automatch_probe_send(void)
{
    struct addrinfo hints, *res = NULL;
    unsigned char pkt[AUTOMATCH_PROBE_LEN];
    char portstr[16];
    int fd;

    if (!g_am.probe_host[0] || g_am.probe_port <= 0) return -1;
    if (g_am.probe_sent_ms) return 0;   /* one outstanding at a time */

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(portstr, sizeof(portstr), "%d", g_am.probe_port);
    if (getaddrinfo(g_am.probe_host, portstr, &hints, &res) != 0 || !res)
        return -1;

    fd = (int)socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }
    set_nonblock(fd);

    /* A fresh nonce per attempt, so a late reply to a previous probe cannot
     * be timed against this one's clock and report an absurdly low number. */
    g_am.probe_nonce = (uint32_t)(lobby_mono_ms() * 2654435761u) ^ 0x9E3779B9u;
    automatch_probe_pack(pkt, g_am.probe_nonce);

    if (sendto(fd, (const char *)pkt, (int)sizeof(pkt), 0,
               res->ai_addr, (int)res->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    automatch_probe_close();
    g_am.probe_socket = fd;
    g_am.probe_sent_ms = lobby_mono_ms();
    return 0;
}

static void automatch_send_rtt(void);

/* Polled every pump. Times the 201 reply, or gives up after 2 s. */
static void automatch_probe_poll(void)
{
    unsigned char buf[64];
    uint64_t now;

    if (g_am.probe_socket < 0 || !g_am.probe_sent_ms) return;
    now = lobby_mono_ms();

    for (;;) {
        int n = (int)recv(g_am.probe_socket, (char *)buf, (int)sizeof(buf), 0);
        if (n < 0) break;
        if (n < 10) continue;
        /* Match the nonce: the socket is unconnected and anything can arrive
         * on it, and an unrelated packet timed as our reply is a wrong number
         * reported as fact. */
        if (((uint32_t)buf[6] | (uint32_t)buf[7] << 8 |
             (uint32_t)buf[8] << 16 | (uint32_t)buf[9] << 24) != g_am.probe_nonce)
            continue;
        g_am.rtt_ms = (int)(now - g_am.probe_sent_ms);
        if (g_am.rtt_ms < 0) g_am.rtt_ms = 0;
        if (g_am.rtt_ms > 2000) g_am.rtt_ms = 2000;   /* the server clamps here too */
        fprintf(stderr, "rnet_lobby: automatch probe %s:%d rtt=%d ms\n",
                g_am.probe_host, g_am.probe_port, g_am.rtt_ms);
        automatch_probe_close();
        /* Queued already? Then the ticket was enqueued on an unknown latency
         * and the server is holding it out of pairing for the probe grace --
         * tell it now rather than letting the grace lapse. */
        if (g_am.state == RNET_LOBBY_AUTOMATCH_QUEUED) automatch_send_rtt();
        return;
    }
    if (now - g_am.probe_sent_ms > 2000) {
        fprintf(stderr, "rnet_lobby: automatch probe timed out (%s:%d) -- "
                        "queueing without a latency estimate\n",
                g_am.probe_host, g_am.probe_port);
        automatch_probe_close();
    }
}

static char g_disc_fp[65];

void rnet_lobby_set_disc_fp(const char *hex)
{
    size_t i;
    g_disc_fp[0] = '\0';
    if (!hex) return;
    /* Validated, not trusted: the wire contract is 64 lower-case hex, and a
     * malformed value would be refused by the server as need_disc_fp far from
     * where it was set. Accept upper-case by folding it, refuse anything else
     * outright rather than sending a fingerprint that names nothing. */
    if (strlen(hex) != 64) return;
    for (i = 0; i < 64; ++i) {
        char c = hex[i];
        if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            g_disc_fp[0] = '\0';
            return;
        }
        g_disc_fp[i] = c;
    }
    g_disc_fp[64] = '\0';
    fprintf(stderr, "rnet_lobby: rom fingerprint %.16s... (disc_fp)\n", g_disc_fp);
}

const char *rnet_lobby_disc_fp(void) { return g_disc_fp; }

/* Walk to the next {...} in an array, copying it out. Returns 0 at ']'. */
static int automatch_next_object(const char **pp, char *out, size_t cap)
{
    const char *p = *pp;
    const char *start;
    int depth = 0;
    size_t n;

    while (*p && *p != '{') {
        if (*p == ']') { *pp = p; return 0; }
        ++p;
    }
    if (*p != '{') { *pp = p; return 0; }
    start = p;
    do {
        if (*p == '{') ++depth;
        else if (*p == '}') --depth;
        ++p;
    } while (*p && depth > 0);
    n = (size_t)(p - start);
    if (n >= cap) n = cap - 1;
    memcpy(out, start, n);
    out[n] = '\0';
    *pp = p;
    return 1;
}

/* `probe: { endpoint, magic, type }` -- where to measure the path. Published
 * on both rulesets_ok and queued, and taken from whichever arrived last. */
static void automatch_ingest_probe(const char *obj)
{
    char endpoint[160];
    char *colon;
    endpoint[0] = '\0';
    json_get_str(obj, "endpoint", endpoint, sizeof(endpoint));
    /* Rightmost colon: an IPv6 literal has several, and the port is last. */
    colon = strrchr(endpoint, ':');
    if (!colon || !colon[1]) return;
    *colon = '\0';
    snprintf(g_am.probe_host, sizeof(g_am.probe_host), "%s", endpoint);
    g_am.probe_port = atoi(colon + 1);
    g_am.probe_magic = (unsigned)json_get_int(obj, "magic", 0);
    g_am.probe_type = json_get_int(obj, "type", 200);
}

/* `titles: [ { ..., pool: N } ]` -- this host queues one title, so the first
 * row is the one the player is waiting in. */
static int automatch_first_pool(const char *json)
{
    const char *p = strstr(json, "\"titles\"");
    char obj[512];
    if (!p) return g_am.pool;
    p = strchr(p, '[');
    if (!p) return g_am.pool;
    ++p;
    if (!automatch_next_object(&p, obj, sizeof(obj))) return 0;
    return json_get_int(obj, "pool", 0);
}

static void automatch_send_rtt(void)
{
    char msg[128];
    if (g_am.rtt_ms < 0 || g_am.rtt_reported) return;
    snprintf(msg, sizeof(msg),
             "{\"op\":\"automatch_rtt\",\"rtt_ms\":%d}", g_am.rtt_ms);
    queue_send(msg);
    g_am.rtt_reported = 1;
}

int rnet_lobby_set_blocks(const char *accounts)
{
    /* Room for the server's cap (256 ids) at 40 characters each, plus the
     * separators and the envelope. A list that would not fit is truncated at
     * a separator rather than sent malformed -- a half-written id at the end
     * would name nobody, and a malformed op would leave the server enforcing
     * nothing at all. */
    static char msg[256 * 41 + 64];
    char list[256 * 41];
    size_t n = 0;
    int first = 1;
    const char *p = accounts ? accounts : "";

    if (!g_lc.connected) return -1;
    list[0] = '\0';
    while (*p) {
        const char *sep = strchr(p, ';');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        if (len && len < 40 && n + len + 8 < sizeof(list)) {
            if (!first) { list[n++] = ','; }
            list[n++] = '"';
            memcpy(list + n, p, len);
            n += len;
            list[n++] = '"';
            list[n] = '\0';
            first = 0;
        }
        if (!sep) break;
        p = sep + 1;
    }
    snprintf(msg, sizeof(msg), "{\"op\":\"set_blocks\",\"accounts\":[%s]}", list);
    queue_send(msg);
    flush_pending();
    return 0;
}

int rnet_lobby_automatch_request_rulesets(void)
{
    char msg[256];
    char gn_esc[RNET_LOBBY_NAME_LEN * 2 + 4];
    const char *gn = g_lc.filter_game_name;
    if (!gn || !gn[0]) return -1;
    if (g_am.rulesets_in_flight) return 0;
    json_escape(gn, gn_esc, sizeof(gn_esc));
    snprintf(msg, sizeof(msg),
             "{\"op\":\"automatch_rulesets\",\"game_name\":\"%s\"}", gn_esc);
    queue_send(msg);
    g_am.rulesets_in_flight = 1;
    return 0;
}

int rnet_lobby_automatch_available(void)
{
    /* Zero rulesets is a real answer and means the same as "no": this
     * deployment has none loaded for this title. Not having ASKED yet is also
     * no -- the button must not be offered on an assumption. */
    return g_am.have_rulesets && g_am.ruleset_count > 0;
}

int rnet_lobby_automatch_ruleset_count(void) { return g_am.ruleset_count; }

int rnet_lobby_automatch_ruleset_get(int index, RNetLobbyRuleset *out)
{
    if (!out || index < 0 || index >= g_am.ruleset_count) return 0;
    *out = g_am.rulesets[index];
    return 1;
}

int rnet_lobby_automatch_queue(const char *ruleset_id, int mods_enabled,
                               const char *mod_exempt)
{
    char msg[2048];
    char exempt_json[768];
    char gn_esc[RNET_LOBBY_NAME_LEN * 2 + 4];
    char gv_esc[RNET_LOBBY_VERSION_LEN * 2 + 4];
    char rid_esc[RNET_LOBBY_RULESET_ID_LEN * 2 + 4];
    const char *rid = (ruleset_id && ruleset_id[0]) ? ruleset_id
                      : (g_am.ruleset_count > 0 ? g_am.rulesets[0].id : "");
    const char *disc_fp = rnet_lobby_disc_fp();
    char rtt[48];

    if (!g_lc.connected) return -1;
    if (!rid[0]) return -1;
    if (g_am.state == RNET_LOBBY_AUTOMATCH_QUEUED ||
        g_am.state == RNET_LOBBY_AUTOMATCH_FOUND)
        return -1;
    /* The queue key REQUIRES a fingerprint: in `join` an empty one means
     * "legacy host, no check", and a wildcard in a queue silently pairs a
     * different dump against this one. Refuse here rather than let the server
     * answer need_disc_fp, so the reason is available before the round trip. */
    if (!disc_fp || strlen(disc_fp) != 64) {
        automatch_fail("this build cannot fingerprint its ROM, so it cannot queue");
        return -1;
    }

    json_escape(g_lc.filter_game_name, gn_esc, sizeof(gn_esc));
    json_escape(effective_game_version(NULL), gv_esc, sizeof(gv_esc));
    json_escape(rid, rid_esc, sizeof(rid_esc));

    /* The exemption evidence, as a JSON ARRAY of strings.
     *
     * An array rather than one ';'-joined string, and for a reason this
     * codebase has already paid for once: match_caps.mod_plan shipped as a
     * ';'-separated STRING, which is valid JSON that every reader using
     * as_array() saw as empty -- so a host with a full plan read as a host
     * requiring nothing, failing open and silently. A list of things the
     * server must check is exactly the shape where that direction of failure
     * is worst, so it goes on the wire as a list. */
    {
        size_t o = 0;
        const char *p = mod_exempt;
        int first = 1;
        exempt_json[o++] = '[';
        while (p && *p) {
            const char *end = p;
            char entry[160];
            char esc[sizeof(entry) * 2 + 4];
            size_t len;
            while (*end && *end != ';' && *end != '\n') ++end;
            len = (size_t)(end - p);
            if (len && len < sizeof(entry)) {
                memcpy(entry, p, len);
                entry[len] = '\0';
                json_escape(entry, esc, sizeof(esc));
                if (o + strlen(esc) + 4 < sizeof(exempt_json)) {
                    if (!first) exempt_json[o++] = ',';
                    exempt_json[o++] = '"';
                    memcpy(exempt_json + o, esc, strlen(esc));
                    o += strlen(esc);
                    exempt_json[o++] = '"';
                    first = 0;
                } else {
                    /* Refuse rather than send a SHORT list: a truncated list
                     * of exemptions is a list the server approves in full
                     * while the client relies on more than it declared. */
                    automatch_fail("too many mod exemptions to declare");
                    return -1;
                }
            }
            p = *end ? end + 1 : end;
        }
        exempt_json[o++] = ']';
        exempt_json[o] = '\0';
    }

    /* Measure before queueing when we can: a client that probes first never
     * waits out the server's probe grace at all. */
    if (g_am.rtt_ms < 0) automatch_probe_send();
    rtt[0] = '\0';
    if (g_am.rtt_ms >= 0)
        snprintf(rtt, sizeof(rtt), ",\"rtt_ms\":%d", g_am.rtt_ms);

    /* One title: this host runs one game. A multi-title launcher sends
     * several here, in preference order; the wire has always allowed it. */
    snprintf(msg, sizeof(msg),
             "{\"op\":\"automatch_queue\",\"titles\":[{"
             "\"game_name\":\"%s\",\"game_version\":\"%s\","
             "\"disc_fp\":\"%s\",\"ruleset_id\":\"%s\",\"max_slots\":%d}],"
             "\"mods_enabled\":%s,\"mod_exempt\":%s%s%s}",
             gn_esc, gv_esc, disc_fp, rid_esc, g_cfg.automatch_slots,
             mods_enabled ? "true" : "false", exempt_json,
             /* this client can host a match over ICE (recomp_net/host_ice.h) */
             (g_relay_via_ice_pref && rnet_host_ice_available()) ? ",\"ice_relay\":true" : "",
             rtt);
    queue_send(msg);
    g_am.error[0] = '\0';
    g_am.queue_in_flight = 1;
    g_am.rtt_reported = (g_am.rtt_ms >= 0);
    return 0;
}

int rnet_lobby_automatch_cancel(void)
{
    if (!g_lc.connected) return -1;
    queue_send("{\"op\":\"automatch_cancel\"}");
    return 0;
}

int rnet_lobby_automatch_state(void) { return g_am.state; }
int rnet_lobby_automatch_queued_secs(void) { return g_am.queued_secs; }
int rnet_lobby_automatch_pool(void) { return g_am.pool; }

int rnet_lobby_automatch_found_get(RNetLobbyAutomatchFound *out)
{
    if (!out || g_am.state != RNET_LOBBY_AUTOMATCH_FOUND) return 0;
    *out = g_am.found;
    /* Recomputed on every read, so the caller can poll it each frame and get
     * a live count. Never below 0: the offer is about to lapse, and a
     * negative would draw as one. */
    if (g_am.found_deadline_ms) {
        uint64_t now = lobby_mono_ms();
        out->accept_secs = now >= g_am.found_deadline_ms
                               ? 0
                               : (int)((g_am.found_deadline_ms - now + 999ull) / 1000ull);
    }
    return 1;
}

int rnet_lobby_automatch_accept(int accept)
{
    char msg[160];
    char tid_esc[RNET_LOBBY_ID_LEN * 2 + 4];
    if (!g_lc.connected) return -1;
    if (g_am.state != RNET_LOBBY_AUTOMATCH_FOUND) return -1;
    /* The ticket id is echoed so a late answer to a LAPSED offer is discarded
     * rather than applied to whatever offer is current by then. */
    json_escape(g_am.ticket_id, tid_esc, sizeof(tid_esc));
    snprintf(msg, sizeof(msg),
             "{\"op\":\"automatch_accept\",\"ticket_id\":\"%s\","
             "\"accept\":%s}", tid_esc, accept ? "true" : "false");
    queue_send(msg);
    if (accept) {
        g_am.state = RNET_LOBBY_AUTOMATCH_ACCEPTED;
    } else {
        /* Declining ends the ticket. The strike is the server's to record. */
        automatch_reset_queue_state();
    }
    return 0;
}

int rnet_lobby_automatch_room(void) { return g_am.in_automatch_room; }

void rnet_lobby_automatch_refuse_local(const char *why)
{
    automatch_fail(why);
}

const char *rnet_lobby_automatch_error(void) { return g_am.error; }

static void member_rtt_clear(void)
{
    int i;
    for (i = 0; i < RNET_LOBBY_MAX_MEMBERS; ++i)
        g_lc.member_rtt_ms[i] = -1;
    g_lc.rtt_next_ping_ms = 0;
}

int rnet_lobby_spectator_slot_base(void);

/* member_rtt_ms has a cell per seat in BOTH tables, but a gallery seat is
 * numbered from the server's spectator base (64+), not from the array. Map
 * a seat to its cell; -1 for a seat neither table has. Without this every
 * spectator's report was dropped as out of range and the gallery showed no
 * latency at all. */
static int rtt_index_for_slot(int slot)
{
    int base;
    if (slot < 0) return -1;
    if (slot < RNET_LOBBY_MAX_PLAYERS) return slot;
    base = rnet_lobby_spectator_slot_base();
    if (base > 0 && slot >= base && slot < base + RNET_LOBBY_MAX_SPECTATORS)
        return RNET_LOBBY_MAX_PLAYERS + (slot - base);
    return -1;
}

static int member_slot_for_player(const char *player_id)
{
    int i;
    if (!player_id || !player_id[0])
        return -1;
    for (i = 0; i < g_lc.member_count; ++i) {
        if (strcmp(g_lc.members[i].player_id, player_id) == 0)
            return g_lc.members[i].slot;
    }
    return -1;
}

static int local_member_slot(void)
{
    return member_slot_for_player(g_lc.player_id);
}

static int member_is_spectator(const char *player_id)
{
    int i;
    if (!player_id || !player_id[0])
        return 0;
    for (i = 0; i < g_lc.member_count; ++i) {
        if (strcmp(g_lc.members[i].player_id, player_id) == 0)
            return g_lc.members[i].is_spectator ? 1 : 0;
    }
    return 0;
}

/* Is this gameplay ICE signal ours to ingest?
 *
 * The netplay session owns exactly ONE ICE agent (rnet_session.c), so it can
 * negotiate with exactly one peer. The lobby relay, though, forwards a
 * broadcast `signal` to everyone in the room, and a spectator's agent gathers
 * and offers just like a player's. Feeding a third party's SDP to an agent
 * that has already set a remote description reads to it as a peer ICE restart
 * (rnet_ice_agent.c): it destroys the LIVE connection, adopts force_relay, and
 * rebinds to the wrong party -- whose host candidates the relay filter then
 * drops, so nothing reconnects. Both players lose the match the moment anyone
 * walks into the gallery.
 *
 * The mod-transfer handshake above already learned this and filters by sender.
 * This is the same rule for the game's own ICE: the gallery never speaks to a
 * player's agent, and a spectator -- which has no ICE peer at all, since it
 * rides the server input relay -- ingests nothing.
 *
 * A sender we cannot attribute (`from` empty, a server that predates
 * from_player_id) is accepted as before: dropping it would break a working
 * two-player match to close a hole that server cannot open. */
static int ice_signal_is_for_us(int type, const char *from)
{
    if (type < (int)RNET_SIGNAL_LOCAL_SDP || type > (int)RNET_SIGNAL_SET_CONTROLLING)
        return 1; /* Not a gameplay ICE signal -- not ours to judge. */
    if (member_is_spectator(g_lc.player_id)) {
        fprintf(stderr, "rnet_lobby: dropping ICE signal type=%d — this build "
                        "is spectating and negotiates with nobody\n", type);
        return 0;
    }
    if (from && from[0] && member_is_spectator(from)) {
        fprintf(stderr, "rnet_lobby: dropping ICE signal type=%d from a "
                        "spectator — the gallery does not negotiate with a "
                        "player's agent\n", type);
        return 0;
    }
    return 1;
}

static const char *effective_game_version(const char *override_ver)
{
    if (override_ver && override_ver[0]) return override_ver;
    if (g_lc.filter_game_version[0]) return g_lc.filter_game_version;
    return RNET_LOBBY_DEFAULT_VERSION;
}

static const char *identity_version_override(void);   /* fwd */

static int list_filter_version_strict(void)
{
    /* A run under RNET_LOBBY_GAME_VERSION lists UNFILTERED whatever the pin
     * looks like. The override exists to put two development machines in one
     * pool, and the mistake that session invites is setting it on ONE of them
     * -- at which point strict filtering hides the other's lobby and hands
     * back exactly the "my friend's lobby isn't showing up" symptom the rest
     * of this function exists to prevent, in the situation least able to
     * afford it. Show every lobby and let the join explain the mismatch. */
    if (identity_version_override()) return 0;

    /* Otherwise: any build that is not a plain released version lists
     * UNFILTERED.
     *
     * The version pin is still enforced -- the server refuses the join with
     * version_mismatch -- but a filtered list would hide the mismatched lobby
     * instead of explaining it, and "my friend's lobby isn't showing up" is a
     * much worse thing to debug than "this lobby is a different build".
     *
     * The test is the QUALIFIER, not a "dev" prefix. A pin is unqualified only
     * for a clean release build ("0.1.5"); every other shape carries a '+' and
     * what follows it: "dev+abc12345", "dev+abc12345-dirty.1234abcd", and --
     * the case the old prefix test missed -- "0.1.5+abc12345-dirty.1234abcd",
     * which is what CMake stamps for a Release-TYPE build from a modified
     * tree. That is an ordinary local build, and it was being classified as a
     * release and silently filtering every other build out of its own lobby
     * list, which is precisely the debugging pain this function exists to
     * prevent. */
    const char *gv = effective_game_version(NULL);
    return gv && gv[0] && strchr(gv, '+') == NULL;
}

static void queue_send(const char *json);
static void clear_turn_credentials(void);
static int queue_turn_credentials_request(void);


static void clear_turn_credentials(void)
{
    memset(&g_lc.turn, 0, sizeof(g_lc.turn));
    g_lc.turn_received_at = 0;
    g_lc.turn_request_pending = 0;
}

static int queue_turn_credentials_request(void)
{
    if (!rnet_lobby_connected())
        return -1;
    queue_send("{\"op\":\"get_turn_credentials\"}");
    g_lc.turn_request_pending = 1;
    return 0;
}

static void queue_list_request(void)
{
    char msg[384];
    char gn_esc[JSON_ESC_CAP(RNET_LOBBY_NAME_LEN)];
    char gv_esc[JSON_ESC_CAP(RNET_LOBBY_VERSION_LEN)];
    const char *gn = g_lc.filter_game_name;
    const char *gv = effective_game_version(NULL);
    json_escape(gn, gn_esc, sizeof(gn_esc));
    json_escape(gv ? gv : "dev", gv_esc, sizeof(gv_esc));
    if (list_filter_version_strict() && (gn[0] || (gv && gv[0]))) {
        snprintf(msg, sizeof(msg),
                 "{\"op\":\"list\",\"game_name\":\"%s\",\"game_version\":\"%s\"}",
                 gn_esc, gv_esc);
        queue_send(msg);
    } else if (gn[0]) {
        snprintf(msg, sizeof(msg), "{\"op\":\"list\",\"game_name\":\"%s\"}", gn_esc);
        queue_send(msg);
    } else {
        queue_send("{\"op\":\"list\"}");
    }
}

static void match_caps_clear(RNetLobbyMatchCaps *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->input_delay = 6;
}

/* ---- host relay (recomp_net/host_relay.h) -------------------------------
 * The orchestration object lives here so every consumer of this client gets
 * the flow by pumping the lobby. `relay_host_pref` is the HOST's setting,
 * default on, published into the caps of rooms it creates / republishes. */
static RNetHostRelay *g_host_relay;
static int g_relay_host_pref = 1;

void rnet_lobby_set_relay_host_pref(int on) { g_relay_host_pref = on ? 1 : 0; }
int  rnet_lobby_relay_host_pref(void) { return g_relay_host_pref; }

static int host_relay_send(const char *json, void *ctx)
{
    (void)ctx;
    if (!rnet_lobby_connected() || !g_lc.in_lobby) return -1;
    queue_send(json);
    flush_pending();
    return 0;
}

static int lobby_bind_port(void)
{
    const char *colon = strrchr(g_lc.my_bind, ':');
    const int port = colon ? atoi(colon + 1) : 0;
    return port > 0 && port < 65536 ? port : 0;
}

/* Every pump: the view of the room decides what the orchestration holds.
 * Active only while seated in an online room whose published caps ask for
 * the host relay (the host's caps are echoed back to it, so one rule serves
 * both roles) and before the launch, which releases the port for the game. A spectator is
 * not on the relay's path and reports nothing. */
static void host_relay_step(void)
{
    RNetHostRelayView v;
    if (!g_host_relay) {
        g_host_relay = rnet_host_relay_create();
        if (!g_host_relay) return;
    }
    memset(&v, 0, sizeof(v));
    v.is_host = g_lc.is_host ? 1 : 0;
    v.active = rnet_lobby_connected() && g_lc.in_lobby && !g_lc.launch_pending &&
               !g_lc.join.local_is_spectator &&
               g_lc.match_caps.valid && g_lc.match_caps.relay_host &&
               !room_relays_via_ice();   /* ICE mode: no port, no set_host_endpoint */
    v.bind_port = (unsigned short)lobby_bind_port();
    if (v.is_host && !v.bind_port) v.active = 0;
    v.host_endpoint = g_lc.join.host_endpoint;
    v.send_json = host_relay_send;
    rnet_host_relay_update(g_host_relay, &v);
}

int rnet_lobby_host_relay_status(struct RNetHostRelayStatus *out)
{
    RNetHostRelayStatus st;
    if (!out) return 0;
    rnet_host_relay_status(g_host_relay, &st);
    *out = st;
    return st.role != 0;
}

void rnet_lobby_host_relay_release_port(void)
{
    rnet_host_relay_release_port(g_host_relay);
}

static void host_relay_leave(void)
{
    rnet_host_relay_leave(g_host_relay);
}

/* ---- host relay over ICE (recomp_net/host_ice.h) -------------------------
 * `relay_via_ice_pref` is the HOST's setting, default ON: it publishes
 * match_caps.relay_via = "ice" alongside relay = "host" when this build can
 * run ICE. In that mode the waiting-room proof is an ICE agent pair per
 * guest, NOT a probe of an advertised UDP port: no set_host_endpoint, no
 * UPnP / NAT-PMP / STUN port (host_relay_step stands down). */
static RNetHostIce *g_host_ice;

void rnet_lobby_set_relay_via_ice(int on) { g_relay_via_ice_pref = on ? 1 : 0; }
int  rnet_lobby_relay_via_ice(void) { return g_relay_via_ice_pref; }

/* The room's published caps ask for ICE as the host relay's path. */
static int room_relays_via_ice(void)
{
    return g_lc.match_caps.valid && g_lc.match_caps.relay_via_ice;
}

/* The waiting-room agents handed over at launch, until the engine takes them
 * (or the room / launch is abandoned). Kept apart from g_lc: a launch that
 * fails must still be able to say why after the lobby state is reset. */
typedef struct {
    int      valid;                       /* a bundle awaits take */
    int      is_host;
    int      n;
    RNetLobbyIceSeat seat[RNET_HOST_ICE_MAX_PEERS];   /* host */
    RNetIceAgent *guest;                  /* guest: connected to the host */
    uint64_t born_ms;
    char     error[200];
} IceLaunch;
static IceLaunch g_il;

static void ice_launch_discard(void)
{
    int i;
    for (i = 0; i < g_il.n; ++i)
        if (g_il.seat[i].agent) rnet_host_ice_destroy_agent(g_il.seat[i].agent);
    if (g_il.guest) rnet_host_ice_destroy_agent(g_il.guest);
    memset(&g_il, 0, offsetof(IceLaunch, error));
}

static void host_ice_leave(void)
{
    ice_launch_discard();
    g_il.error[0] = '\0';
    rnet_host_ice_destroy(&g_host_ice);
}

static int host_ice_send_signal(const char *to, int type, int flag, const char *text,
                                void *ctx)
{
    (void)ctx;
    return rnet_lobby_send_signal_to(to, type, flag, text);
}

/* Every pump, beside host_relay_step: the room decides which agents exist. */
static void host_ice_step(void)
{
    RNetHostIceView v;
    RNetHostIcePeer peers[RNET_HOST_ICE_MAX_PEERS];
    const RNetLobbyTurnCredentials *tc;
    int n = 0, i;
    if (!g_host_ice) {
        g_host_ice = rnet_host_ice_create();
        if (!g_host_ice) return;
    }
    /* A bundle nobody took (the engine declined the launch) must not hold
     * sockets for the rest of the session. */
    if (g_il.valid && lobby_mono_ms() - g_il.born_ms > 60000u)
        ice_launch_discard();
    memset(&v, 0, sizeof(v));
    v.is_host = g_lc.is_host ? 1 : 0;
    v.local_slot = local_member_slot();
    v.active = rnet_lobby_connected() && g_lc.in_lobby && !g_lc.launch_pending &&
               !g_lc.join.local_is_spectator && room_relays_via_ice() &&
               v.local_slot >= 0;
    if (v.active) {
        if (v.is_host) {
            for (i = 0; i < g_lc.member_count && n < RNET_HOST_ICE_MAX_PEERS; ++i) {
                const RNetLobbyMember *m = &g_lc.members[i];
                if (m->is_spectator || !m->player_id[0] ||
                    strcmp(m->player_id, g_lc.player_id) == 0)
                    continue;
                peers[n].player_id = m->player_id;
                peers[n].slot = m->slot;
                n++;
            }
        } else {
            const int hs = member_slot_for_player(g_lc.host_player_id);
            if (g_lc.host_player_id[0] && hs >= 0) {
                peers[0].player_id = g_lc.host_player_id;
                peers[0].slot = hs;
                n = 1;
            } else {
                v.active = 0;
            }
        }
    }
    v.peers = peers;
    v.peer_count = n;
    /* STUN only. The lobby's Coturn mint is deliberately not used: hub agents
     * never use TURN. */
    tc = rnet_lobby_turn_credentials();
    if (tc && tc->valid && tc->stun_host[0]) {
        v.stun_host = tc->stun_host;
        v.stun_port = (unsigned short)(tc->stun_port > 0 ? tc->stun_port : 3478);
    }
    v.send_signal = host_ice_send_signal;
    v.send_json = host_relay_send;
    rnet_host_ice_update(g_host_ice, &v);
}

int rnet_lobby_host_ice_status(RNetHostIceStatus *out)
{
    if (!out) return 0;
    rnet_host_ice_status(g_host_ice, out);
    return out->role != 0;
}

static int json_extract_object(const char *json, const char *key, char *out, size_t out_cap);
static void send_set_ready(int ready);
static void mod_xfer_on_request(const char *from, const char *text);
static void mod_xfer_fail(const char *why);
static int mod_ice_type_for_push(int emitted_type);
static void parse_match_caps_object(const char *obj, RNetLobbyMatchCaps *out);
static void ingest_match_caps_from_json(const char *json);
static int append_match_caps_json(char *dst, size_t dst_cap, const RNetLobbyMatchCaps *caps);

const char *rnet_lobby_default_url(void)
{
    const char *e = lobby_env("RNET_LOBBY_URL", "LOBBY_URL");
    return (e && e[0]) ? e : "ws://netplay.retcomm.net:8765";
}

static int parse_ws_url(const char *url, char *host, size_t hcap, int *port, char *path, size_t pcap)
{
    const char *p = url;
    const char *slash;
    char hostport[192];
    char *colon;
    if (!url) {
        return -1;
    }
    if (strncmp(p, "ws://", 5) == 0) {
        p += 5;
    } else if (strncmp(p, "wss://", 6) == 0) {
        return -1; /* TLS not in this phase */
    }
    slash = strchr(p, '/');
    if (slash) {
        size_t n = (size_t)(slash - p);
        if (n >= sizeof(hostport)) {
            n = sizeof(hostport) - 1;
        }
        memcpy(hostport, p, n);
        hostport[n] = '\0';
        strncpy(path, slash, pcap - 1);
        path[pcap - 1] = '\0';
    } else {
        strncpy(hostport, p, sizeof(hostport) - 1);
        hostport[sizeof(hostport) - 1] = '\0';
        strncpy(path, "/", pcap - 1);
    }
    colon = strrchr(hostport, ':');
    if (colon && strchr(hostport, ']') == NULL) {
        *colon = '\0';
        *port = atoi(colon + 1);
        strncpy(host, hostport, hcap - 1);
    } else {
        strncpy(host, hostport, hcap - 1);
        *port = 8765;
    }
    host[hcap - 1] = '\0';
    return 0;
}

static const char *json_get_str(const char *json, const char *key, char *out, size_t cap)
{
    char pat[80];
    const char *p;
    size_t o = 0;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) {
        if (out && cap) {
            out[0] = '\0';
        }
        return NULL;
    }
    p = strchr(p + strlen(pat), ':');
    if (!p) {
        return NULL;
    }
    ++p;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p != '"') {
        return NULL;
    }
    ++p;
    while (*p && *p != '"' && o + 1 < cap) {
        if (*p == '\\' && p[1]) {
            ++p;
            switch (*p) {
            case 'n': out[o++] = '\n'; break;
            case 'r': out[o++] = '\r'; break;
            case 't': out[o++] = '\t'; break;
            case '"': out[o++] = '"'; break;
            case '\\': out[o++] = '\\'; break;
            case '/': out[o++] = '/'; break;
            default: out[o++] = *p; break;
            }
            ++p;
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
    return out;
}

static size_t json_escape(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    if (!in || !out || cap == 0) return 0;
    while (*in && o + 2 < cap) {
        unsigned char c = (unsigned char)*in++;
        if (c == '"' || c == '\\') {
            if (o + 3 >= cap) break;
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c == '\n') {
            if (o + 3 >= cap) break;
            out[o++] = '\\';
            out[o++] = 'n';
        } else if (c == '\r') {
            if (o + 3 >= cap) break;
            out[o++] = '\\';
            out[o++] = 'r';
        } else if (c == '\t') {
            if (o + 3 >= cap) break;
            out[o++] = '\\';
            out[o++] = 't';
        } else if (c < 0x20) {
            continue;
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
    return o;
}

static void enqueue_signal(int type, int flag, const char *text)
{
    int i;
    if (g_lc.sig_count >= (int)(sizeof(g_lc.sig_q) / sizeof(g_lc.sig_q[0]))) {
        /* Drop oldest. */
        g_lc.sig_head = (g_lc.sig_head + 1) % (int)(sizeof(g_lc.sig_q) / sizeof(g_lc.sig_q[0]));
        g_lc.sig_count--;
    }
    i = g_lc.sig_tail;
    g_lc.sig_q[i].type = type;
    g_lc.sig_q[i].flag = flag;
    g_lc.sig_q[i].text[0] = '\0';
    if (text)
        strncpy(g_lc.sig_q[i].text, text, sizeof(g_lc.sig_q[i].text) - 1);
    g_lc.sig_tail = (g_lc.sig_tail + 1) % (int)(sizeof(g_lc.sig_q) / sizeof(g_lc.sig_q[0]));
    g_lc.sig_count++;
}

static int json_get_int(const char *json, const char *key, int def)
{
    char pat[80];
    const char *p;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) {
        return def;
    }
    p = strchr(p + strlen(pat), ':');
    if (!p) {
        return def;
    }
    return (int)strtol(p + 1, NULL, 10);
}

static int json_get_bool(const char *json, const char *key, int def);

/* The `players` array of a lobby_list: everyone on the hub. Flat objects,
 * so each one is cut out by brace depth and read with the key getters. An
 * older server has no such array and the count simply goes to zero. */
static void lobby_list_parse_players(const char *json)
{
    const char *p = strstr(json, "\"players\"");
    int n = 0;
    g_lc.online_count = 0;
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    ++p;
    while (*p && n < RNET_LOBBY_MAX_ONLINE) {
        const char *obj, *end;
        int depth = 0;
        char chunk[512];
        size_t len;
        while (*p && *p != '{' && *p != ']') ++p;
        if (*p != '{') break;
        obj = end = p;
        do {
            if (*end == '{') ++depth;
            else if (*end == '}') --depth;
            ++end;
        } while (*end && depth > 0);
        len = (size_t)(end - obj);
        if (len >= sizeof(chunk)) len = sizeof(chunk) - 1;
        memcpy(chunk, obj, len);
        chunk[len] = '\0';
        memset(&g_lc.online[n], 0, sizeof(g_lc.online[n]));
        json_get_str(chunk, "display_name", g_lc.online[n].display_name,
                     sizeof(g_lc.online[n].display_name));
        json_get_str(chunk, "country", g_lc.online[n].country,
                     sizeof(g_lc.online[n].country));
        json_get_str(chunk, "lobby_id", g_lc.online[n].lobby_id,
                     sizeof(g_lc.online[n].lobby_id));
        json_get_str(chunk, "lobby_name", g_lc.online[n].lobby_name,
                     sizeof(g_lc.online[n].lobby_name));
        g_lc.online[n].hosting = json_get_bool(chunk, "hosting", 0);
        json_get_str(chunk, "tag", g_lc.online[n].tag, sizeof(g_lc.online[n].tag));
        json_get_str(chunk, "account", g_lc.online[n].account,
                     sizeof(g_lc.online[n].account));
        json_get_str(chunk, "game_name", g_lc.online[n].game_name,
                     sizeof(g_lc.online[n].game_name));
        /* Players of another title are not "online" for this one. A row
         * with no title yet (a client that has not listed) is kept. */
        if (g_lc.filter_game_name[0] && g_lc.online[n].game_name[0] &&
            strcmp(g_lc.online[n].game_name, g_lc.filter_game_name) != 0)
            { p = end; continue; }
        if (g_lc.online[n].display_name[0]) ++n;
        p = end;
    }
    g_lc.online_count = n;
}

int rnet_lobby_online_count(void)
{
    return g_lc.online_count;
}

int rnet_lobby_online_get(int index, RNetLobbyOnlinePlayer *out)
{
    if (!out || index < 0 || index >= g_lc.online_count) return 0;
    *out = g_lc.online[index];
    return 1;
}

static int json_get_bool(const char *json, const char *key, int def)
{
    char pat[80];
    const char *p;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) {
        return def;
    }
    p = strchr(p + strlen(pat), ':');
    if (!p) {
        return def;
    }
    ++p;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (strncmp(p, "true", 4) == 0) {
        return 1;
    }
    if (strncmp(p, "false", 5) == 0) {
        return 0;
    }
    return def;
}

static int json_extract_object(const char *json, const char *key, char *out, size_t out_cap)
{
    char pat[80];
    const char *p;
    int depth;
    size_t n;
    if (!json || !key || !out || out_cap < 3) return 0;
    out[0] = '\0';
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) return 0;
    p = strchr(p + strlen(pat), ':');
    if (!p) return 0;
    ++p;
    while (*p && isspace((unsigned char)*p)) ++p;
    if (*p != '{') return 0;
    depth = 0;
    n = 0;
    do {
        if (*p == '{') ++depth;
        else if (*p == '}') --depth;
        if (n + 1 >= out_cap) return 0;
        out[n++] = *p++;
    } while (*p && depth > 0);
    out[n] = '\0';
    return depth == 0 && n > 1;
}

/* Parse `"<key>":[ {..}, {..} ]` into package rows.
 *
 * Returns the number of rows filled. A row missing an id or a version is
 * skipped: the pair is the whole identity the seat gate matches on, so half of
 * it is not a lesser row, it is a different package. */
static int parse_mod_pkg_array(const char *json, const char *key,
                               RNetLobbyModPkg *out, int max)
{
    char pattern[64];
    const char *p;
    int n = 0;

    if (!json || !key || !out || max <= 0) return 0;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p) return 0;
    p += strlen(pattern);
    while (*p && *p != ':') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '[') return 0;          /* a string here is the old encoding */
    p++;

    while (*p && n < max) {
        char obj[1024];
        const char *start;
        int depth = 0;
        int in_str = 0;
        size_t len;
        RNetLobbyModPkg row;

        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        if (*p == ']' || !*p) break;
        if (*p != '{') break;
        start = p;
        for (; *p; ++p) {
            if (in_str) {
                if (*p == '\\' && p[1]) { ++p; continue; }
                if (*p == '"') in_str = 0;
                continue;
            }
            if (*p == '"') { in_str = 1; continue; }
            if (*p == '{') depth++;
            else if (*p == '}') { depth--; if (depth == 0) { ++p; break; } }
        }
        if (depth != 0) break;                    /* truncated array */
        len = (size_t)(p - start);
        if (len >= sizeof(obj)) continue;         /* absurd row; skip it */
        memcpy(obj, start, len);
        obj[len] = '\0';

        memset(&row, 0, sizeof(row));
        json_get_str(obj, "id", row.id, sizeof(row.id));
        json_get_str(obj, "ver", row.ver, sizeof(row.ver));
        json_get_str(obj, "n", row.name, sizeof(row.name));
        json_get_str(obj, "f", row.feats, sizeof(row.feats));
        json_get_str(obj, "nf", row.nf, sizeof(row.nf));
        if (!row.id[0] || !row.ver[0])
            continue;
        out[n++] = row;
    }
    return n;
}

/* Emit `"<key>":[ … ]`. Returns bytes written, or 0 if the whole array did not
 * fit -- a half-written array is invalid JSON, and a SHORT one is worse: it
 * would name fewer requirements than the host actually has. */
static int append_mod_pkg_array(char *dst, size_t cap, const char *key,
                                const RNetLobbyModPkg *pkgs, int count)
{
    size_t used = 0;
    int i;
    int n;
    int wrote = 0;

    if (!dst || cap < 8 || !key || (!pkgs && count > 0)) return 0;
    n = snprintf(dst, cap, "\"%s\":[", key);
    if (n < 0 || (size_t)n >= cap) return 0;
    used = (size_t)n;
    for (i = 0; i < count; ++i) {
        char id_esc[JSON_ESC_CAP(RNET_LOBBY_MOD_ID_LEN)];
        char ver_esc[JSON_ESC_CAP(RNET_LOBBY_MOD_VER_LEN)];
        char name_esc[RNET_LOBBY_MOD_NAME_LEN * 2 + 4];
        char feats_esc[RNET_LOBBY_MOD_FEATS_LEN * 2 + 4];
        char nf_esc[RNET_LOBBY_MOD_FEATS_LEN * 2 + 4];
        if (!pkgs[i].id[0] || !pkgs[i].ver[0]) continue;
        /* id and ver are escaped for the same reason n and f already were:
         * they come from package metadata, which a crafted package -- or an
         * edited client -- chooses. */
        json_escape(pkgs[i].id, id_esc, sizeof(id_esc));
        json_escape(pkgs[i].ver, ver_esc, sizeof(ver_esc));
        json_escape(pkgs[i].name, name_esc, sizeof(name_esc));
        json_escape(pkgs[i].feats, feats_esc, sizeof(feats_esc));
        json_escape(pkgs[i].nf, nf_esc, sizeof(nf_esc));
        /* "nf" only when there is something to say: it is an offer-row field,
         * and an absent key already reads as "nothing missing". */
        n = snprintf(dst + used, cap - used,
                     "%s{\"id\":\"%s\",\"ver\":\"%s\",\"n\":\"%s\",\"f\":\"%s\"%s%s%s}",
                     wrote ? "," : "",
                     id_esc, ver_esc, name_esc, feats_esc,
                     nf_esc[0] ? ",\"nf\":\"" : "", nf_esc,
                     nf_esc[0] ? "\"" : "");
        if (n < 0 || (size_t)n >= cap - used) return 0;
        used += (size_t)n;
        wrote++;
    }
    n = snprintf(dst + used, cap - used, "]");
    if (n < 0 || (size_t)n >= cap - used) return 0;
    return (int)(used + (size_t)n);
}

/* Installed once at start-up and kept OUTSIDE LobbyClient on purpose.
 *
 * rnet_lobby_disconnect() memsets the whole LobbyClient -- and connect()
 * calls disconnect() first -- so anything living in there is per-connection
 * state by definition. These are configuration: the build either can pack and
 * install a package or it cannot, and connecting to a lobby does not change
 * the answer. Held in g_lc, the export hook was installed at init and NULL by
 * the time any peer could ask, and every request came back "could not pack
 * the mod" from a host that was perfectly able to. */
static RNetLobbyModOfferFn g_mod_offer_fn;
static void *g_mod_offer_ctx;
/* Host preference for the next create. File scope, like the transfer hooks:
 * rnet_lobby_disconnect memsets g_lc, and a setting made before a reconnect
 * must still be there when the create goes out. */
static int g_allow_spectators_pref;
/* rnet_lobby_set_launch_blocked: file scope for the same reason -- whether
 * this peer has a game image to boot does not change on a reconnect. */
static int g_launch_blocked;

static RNetLobbyModExportFn  g_mod_export_fn;
static RNetLobbyModFreeFn    g_mod_free_fn;
static RNetLobbyModInstallFn g_mod_install_fn;
static void *g_mod_hook_ctx;

void rnet_lobby_set_mod_offer_supplier(RNetLobbyModOfferFn fn, void *ctx)
{
    g_mod_offer_fn = fn;
    g_mod_offer_ctx = ctx;
}

/* `"mod_offer":{"pkgs":[…]}` -- what this peer already has. The server matches
 * each row of the host's plan against it and refuses to seat on any miss, so
 * an offer that is short in EITHER direction is wrong: too few rows and the
 * peer is turned away holding the mod, too many and it is seated without one.
 * Returns bytes written, or 0 if the whole offer did not fit. */
static int append_mod_offer(char *dst, size_t cap)
{
    RNetLobbyModPkg rows[RNET_LOBBY_MAX_MODS];
    int n;
    int used;

    if (!dst || cap < 8) return 0;
    if (!g_mod_offer_fn) { dst[0] = '\0'; return 0; }
    memset(rows, 0, sizeof(rows));
    n = g_mod_offer_fn(rows, RNET_LOBBY_MAX_MODS, g_mod_offer_ctx);
    if (n < 0) n = 0;
    if (n > RNET_LOBBY_MAX_MODS) n = RNET_LOBBY_MAX_MODS;
    used = snprintf(dst, cap, ",\"mod_offer\":{");
    if (used < 0 || (size_t)used >= cap) return 0;
    {
        int m = append_mod_pkg_array(dst + used, cap - (size_t)used, "pkgs",
                                     rows, n);
        if (m <= 0) return 0;
        used += m;
    }
    if ((size_t)used + 2 >= cap) return 0;
    dst[used++] = '}';
    dst[used] = '\0';
    /* The lobby server discards a mod_offer object larger than 2048 bytes
     * (sanitize_mod_offer), and a discarded offer is indistinguishable from
     * "this peer has nothing" -- which reads as missing every mod and holds
     * the match. Refuse here, where the reason can be said out loud. The
     * measured field is the object itself, not the `,"mod_offer":` prefix the
     * server never sees. */
    {
        const char *obj = strchr(dst, '{');
        const size_t obj_len = obj ? strlen(obj) : (size_t)used;
        if (obj_len > 2000) {
            fprintf(stderr,
                    "rnet_lobby: the installed mod set is %zu bytes, over the "
                    "lobby server's 2048-byte limit; it was not announced\n",
                    obj_len);
            dst[0] = '\0';
            return 0;
        }
    }
    return used;
}

static void parse_match_caps_object(const char *obj, RNetLobbyMatchCaps *out)
{
    if (!obj || !out || obj[0] != '{') return;
    match_caps_clear(out);
    /* Absent: an empty plan is "no mods required". A plan encoded as a STRING
     * rather than an array parses to zero rows here on purpose -- that is the
     * superseded encoding, and reading it would revive a plan the lobby server
     * has already ignored, leaving this peer and the server disagreeing about
     * what the match requires. */
    out->mod_count = parse_mod_pkg_array(obj, "mod_plan", out->mods,
                                         RNET_LOBBY_MAX_MODS);
    json_get_str(obj, "mod_set", out->mod_set, sizeof(out->mod_set));
    /* Absent => empty => nothing is exempt. The default has to be the strict
     * one: a server or host that has never heard of cosmetic mods has granted
     * nothing, and reading its silence as permission is the whole hole. */
    json_get_str(obj, "mod_cosmetic_allow", out->mod_cosmetic_allow,
                 sizeof(out->mod_cosmetic_allow));
    out->input_delay = json_get_int(obj, "input_delay", 6);
    if (out->input_delay < 2) out->input_delay = 2;
    if (out->input_delay > 20) out->input_delay = 20;
    /* Absent (a pre-lift host, or one that never set it) stays 0: "the host
     * published no runway", which is not the same as publishing the minimum. */
    out->input_prediction = json_get_int(obj, "input_prediction", 0);
    out->session_variant = json_get_int(obj, "session_variant", 0);
    if (out->input_prediction < 0) out->input_prediction = 0;
    if (out->input_prediction > 0 && out->input_prediction < 2)
        out->input_prediction = 2;
    if (out->input_prediction > 16) out->input_prediction = 16;
    out->force_turn = json_get_bool(obj, "force_turn", 0) ? 1 : 0;
    out->force_input_relay = json_get_bool(obj, "force_input_relay", 0) ? 1 : 0;
    {
        char relay[12];
        relay[0] = '\0';
        json_get_str(obj, "relay", relay, sizeof(relay));
        out->relay_host = strcmp(relay, "host") == 0 ? 1 : 0;
    }
    {
        char via[12];
        via[0] = '\0';
        json_get_str(obj, "relay_via", via, sizeof(via));
        out->relay_via_ice = strcmp(via, "ice") == 0 ? 1 : 0;
    }
    out->rollback = json_get_bool(obj, "rollback", 1) ? 1 : 0;
    /* The engine's own keys last, into an ext that match_caps_clear zeroed. */
    if (g_codec.parse_extra) g_codec.parse_extra(obj, out, g_codec.ctx);
    out->valid = 1;
}

static void ingest_match_caps_from_json(const char *json)
{
    /* Sized for the whole caps object INCLUDING a full mod array. It used to
     * be 512 bytes, which a plan of any size overflows -- and a truncated
     * extract parses as a caps blob with no plan, i.e. it fails open. */
    char obj[RNET_LOBBY_MAX_MODS * 256 + 512];
    if (json_extract_object(json, "match_caps", obj, sizeof(obj)))
        parse_match_caps_object(obj, &g_lc.match_caps);
}

static int append_match_caps_json(char *dst, size_t dst_cap, const RNetLobbyMatchCaps *caps)
{
    char mods[RNET_LOBBY_MAX_MODS * 256 + 16];
    char set_esc[sizeof(caps->mod_set) * 2 + 4];
    char allow_esc[sizeof(caps->mod_cosmetic_allow) * 2 + 4];
    char pred[40];
    char variant[40];
    char extra[1024];
    int n;

    if (!dst || dst_cap < 8 || !caps || !caps->valid) return 0;
    /* Build the plan first. If it does not fit, publish NOTHING rather than a
     * caps blob carrying a short plan: the server seats on what it reads, so
     * an under-reported requirement seats a peer that cannot play. */
    if (!append_mod_pkg_array(mods, sizeof(mods), "mod_plan", caps->mods,
                              caps->mod_count))
        return 0;
    /* The engine's keys. A codec that cannot fit them withholds the whole
     * object, for the same reason a plan that does not fit does. */
    extra[0] = '\0';
    if (g_codec.write_extra) {
        int e = g_codec.write_extra(caps, extra, sizeof(extra), g_codec.ctx);
        if (e < 0 || (size_t)e >= sizeof(extra)) {
            fprintf(stderr, "rnet_lobby: the engine's match caps keys did not "
                            "fit; nothing was published\n");
            return 0;
        }
        extra[e] = '\0';
    }
    /* Only when published, so a caps blob without it stays byte-identical to
     * what a client from before the field sent. */
    pred[0] = '\0';
    variant[0] = '\0';
    if (caps->session_variant)
        snprintf(variant, sizeof(variant), ",\"session_variant\":%d", caps->session_variant);
    if (caps->input_prediction > 0)
        snprintf(pred, sizeof(pred), ",\"input_prediction\":%d",
                 caps->input_prediction);
    json_escape(caps->mod_set, set_esc, sizeof(set_esc));
    json_escape(caps->mod_cosmetic_allow, allow_esc, sizeof(allow_esc));
    /* "relay":"host" only when asked: a blob without it is byte-identical to
     * what a client from before the host relay sent, and the server reads
     * its absence as "the SFU, as always". */
    n = snprintf(dst, dst_cap,
                 ",\"match_caps\":{\"v\":1,\"input_delay\":%d%s%s,"
                 "\"force_turn\":%s,\"force_input_relay\":%s,%s"
                 "\"rollback\":%s%s,%s,\"mod_set\":\"%s\","
                 "\"mod_cosmetic_allow\":\"%s\"}",
                 caps->input_delay, pred, variant,
                 caps->force_turn ? "true" : "false",
                 caps->force_input_relay ? "true" : "false",
                 caps->relay_host
                     ? ((g_relay_via_ice_pref && rnet_host_ice_available())
                            ? "\"relay\":\"host\",\"relay_via\":\"ice\","
                            : "\"relay\":\"host\",")
                     : "",
                 caps->rollback ? "true" : "false",
                 extra, mods, set_esc, allow_esc);
    if (n < 0 || (size_t)n >= dst_cap) return 0;
    /* The lobby server drops a match_caps object over 4096 bytes ENTIRELY
     * (sanitize_match_caps returns None), which would take rollback,
     * input_delay and the engine's keys down with the plan and hand guests a
     * blank caps blob. Refuse here instead, where we can say why. */
    if ((size_t)n > 4000) {
        fprintf(stderr,
                "rnet_lobby: match caps are %d bytes with %d mod(s); the lobby "
                "server discards anything over 4096, so nothing was published"
                " -- reduce the enabled mod set\n",
                n, caps->mod_count);
        return 0;
    }
    return n;
}

/* The exported spellings of the helpers above, for caps codec hooks. */
int rnet_lobby_json_get_int(const char *json, const char *key, int def)
{
    return json && key ? json_get_int(json, key, def) : def;
}

int rnet_lobby_json_get_bool(const char *json, const char *key, int def)
{
    return json && key ? json_get_bool(json, key, def) : def;
}

const char *rnet_lobby_json_get_str(const char *json, const char *key,
                                    char *out, size_t cap)
{
    if (!out || !cap) return NULL;
    out[0] = '\0';
    if (!json || !key) return NULL;
    return json_get_str(json, key, out, cap);
}

size_t rnet_lobby_json_escape(const char *in, char *out, size_t cap)
{
    return json_escape(in, out, cap);
}

/* Queue one frame, whole or not at all.
 *
 * This used to strncpy into a 2048-byte slot. Every op that can exceed that
 * -- create / start / set_match_caps with the 4000-byte caps object the rest
 * of this file carefully allows, join with a full mod offer, set_blocks --
 * was cut mid-token, the server dropped the malformed frame whole, and the
 * caller had already returned success: the silent "Create Lobby created
 * nothing" failure caps_json_build describes, one layer further down. The
 * slot is now sized for the largest frame, and anything larger (or a full
 * queue) is refused out loud. */
static void queue_send(const char *json)
{
    size_t len;
    if (!json) return;
    len = strlen(json);
    if (len >= sizeof(g_lc.pending_tx[0])) {
        fprintf(stderr, "rnet_lobby: refusing to send a %zu-byte frame (limit "
                        "%zu): %.40s...\n", len,
                sizeof(g_lc.pending_tx[0]) - 1, json);
        return;
    }
    /* After the handshake: straight into the outbound buffer, which keeps
     * order and has no 8-frame ceiling. The slots below only hold frames
     * written before there is a socket to write them to. */
    if (g_lc.handshake_done && g_lc.fd >= 0) {
        (void)ws_send(json);
        return;
    }
    if (g_lc.pending_n >= (int)(sizeof(g_lc.pending_tx) /
                                sizeof(g_lc.pending_tx[0]))) {
        fprintf(stderr, "rnet_lobby: send queue full before the handshake; "
                        "dropping %.40s...\n", json);
        return;
    }
    memcpy(g_lc.pending_tx[g_lc.pending_n], json, len + 1);
    g_lc.pending_n++;
}

/* Latch the connection as broken; rnet_lobby_pump disconnects. */
static void tx_fail(const char *why)
{
    if (g_lc.tx_failed) return;
    g_lc.tx_failed = 1;
    snprintf(g_lc.tx_error, sizeof(g_lc.tx_error), "%s", why);
    fprintf(stderr, "rnet_lobby: lobby connection lost on send: %s (backlog %zu "
                    "bytes, %llu would-blocks, %llu mid-frame); disconnecting\n",
            why, rnet_ws_tx_pending(&g_lc.tx),
            (unsigned long long)g_lc.tx.would_blocks,
            (unsigned long long)g_lc.tx.split_frames);
}

/* Send what the socket takes now; keep the rest, in order, for the next pump.
 * 0 = fine (sent or buffered), -1 = the connection is broken. */
static int tx_flush(void)
{
    if (g_lc.tx_failed) return -1;
    if (g_lc.fd < 0) return -1;
    if (rnet_ws_tx_flush(&g_lc.tx, g_lc.fd) < 0) {
        tx_fail("socket error");
        return -1;
    }
    return 0;
}

/* One frame, whole: appended to the outbound buffer, then flushed as far as
 * the socket allows. A would-block is not an error -- the frame stays queued
 * and the pump finishes it. */
static int ws_send(const char *json)
{
    int rc;
    if (g_lc.tx_failed) return -1;
    rc = rnet_ws_tx_queue_text(&g_lc.tx, json, 1);
    if (rc == -2) {
        tx_fail("outbound backlog over its cap (server not reading)");
        return -1;
    }
    if (rc < 0) {
        fprintf(stderr, "rnet_lobby: cannot frame a %zu-byte message: %.40s...\n",
                strlen(json), json);
        return -1;
    }
    return tx_flush();
}

static void flush_pending(void)
{
    int i;
    if (!g_lc.handshake_done) {
        return;
    }
    for (i = 0; i < g_lc.pending_n; ++i) {
        if (ws_send(g_lc.pending_tx[i]) < 0) break;
    }
    g_lc.pending_n = 0;
    (void)tx_flush();
}

static int endpoint_has_usable_port(const char *endpoint)
{
    const char *colon;
    unsigned port = 0;
    if (!endpoint || !endpoint[0]) return 0;
    colon = strrchr(endpoint, ':');
    if (!colon || !colon[1]) return 0;
    for (colon++; *colon; ++colon) {
        if (*colon < '0' || *colon > '9') return 0;
        port = port * 10u + (unsigned)(*colon - '0');
        if (port > 65535u) return 0;
    }
    return port != 0;
}

/* Set by each launch: the server named peer-to-peer ICE and allocated no
 * relay. Its endpoints then carry no relay meaning, however they compare. */
static int g_launch_named_ice;

static int using_server_input_relay(const RNetLobbyJoinInfo *j)
{
    /* Two peers behind one NAT -- two instances on one machine, two players
     * on one home network -- can advertise the same public ip:port: with the
     * host relay's port taken first-free from 7777 and the guest's first-free
     * from 7778, a busy 7777 puts both on :7778. The equal-endpoints rule
     * below read that as "the server rewrote both to its relay", so an ICE
     * launch dialled the host's own public address over UDP with nothing
     * listening, and timed out after 30 s. The server's word wins. */
    if (g_launch_named_ice)
        return 0;
    if (g_lc.match_caps.valid && g_lc.match_caps.force_input_relay)
        return 1;
    /* Server rewrote both endpoints to the same relay advertise address. */
    if (j && j->host_endpoint[0] && j->guest_endpoint[0] &&
        endpoint_has_usable_port(j->host_endpoint) &&
        endpoint_has_usable_port(j->guest_endpoint) &&
        strcmp(j->host_endpoint, j->guest_endpoint) == 0 &&
        (!g_lc.my_bind[0] || strcmp(j->host_endpoint, g_lc.my_bind) != 0))
        return 1;
    return 0;
}

static void fill_peer_bind_from_join(void)
{
    RNetLobbyJoinInfo *j = &g_lc.join;
    const char *port;
    const int force_relay = !j->transport_host && using_server_input_relay(j);
    memset(j->bind_hostport, 0, sizeof(j->bind_hostport));
    memset(j->peer_hostport, 0, sizeof(j->peer_hostport));
    if (j->transport_ice_hub) {
        /* Nothing to bind or dial: the agents are already connected. Kept
         * non-empty so a legacy consumer's "usable bind" check passes. */
        strncpy(j->bind_hostport, "0.0.0.0:0", sizeof(j->bind_hostport) - 1);
    } else if (force_relay) {
        /* Everyone dials the lobby-server UDP relay — ephemeral local bind. */
        strncpy(j->bind_hostport, "0.0.0.0:0", sizeof(j->bind_hostport) - 1);
        strncpy(j->peer_hostport, j->host_endpoint, sizeof(j->peer_hostport) - 1);
    } else if (g_lc.is_host) {
        /* host_endpoint is the address advertised to peers. It may be the
         * router's public/NAT address and therefore cannot be bound on this
         * machine. Listen on every local interface using the advertised port. */
        port = strrchr(g_lc.my_bind, ':');
        if (port && port[1]) {
            snprintf(j->bind_hostport, sizeof(j->bind_hostport),
                     "0.0.0.0:%s", port + 1);
        } else {
            strncpy(j->bind_hostport, g_lc.my_bind,
                    sizeof(j->bind_hostport) - 1);
        }
        /* guest_endpoint "ip:0" is unusable — leave peer empty so transport
         * accept_first_peer learns the real source from the first UDP packet.
         * Host relay: the guest's advertised address is its LAN bind, which
         * this host cannot dial across the guest's NAT; the guest dials us
         * (it proved it can), so accept-first / hub always. */
        if (!j->transport_host && endpoint_has_usable_port(j->guest_endpoint))
            strncpy(j->peer_hostport, j->guest_endpoint,
                    sizeof(j->peer_hostport) - 1);
    } else {
        strncpy(j->bind_hostport, g_lc.my_bind, sizeof(j->bind_hostport) - 1);
        strncpy(j->peer_hostport, j->host_endpoint, sizeof(j->peer_hostport) - 1);
    }
    j->bind_hostport[sizeof(j->bind_hostport) - 1] = '\0';
    j->peer_hostport[sizeof(j->peer_hostport) - 1] = '\0';
}

/* Read one seat array into the membership table, appending from `n`.
 *
 * Players and spectators arrive as two arrays of identical rows and land in
 * one table tagged by role, because every consumer -- the UI's two tables
 * included -- wants "who is here and what are they" rather than two parallel
 * lists to keep in step. Returns the new row count.
 *
 * `key` absent is not an error: a server that predates spectators sends no
 * "spectators", and the correct result there is a lobby with an empty gallery. */
static int parse_seat_array(const char *json, const char *key, int is_spectator,
                            int n)
{
    char keybuf[32];
    const char *p;
    snprintf(keybuf, sizeof(keybuf), "\"%s\"", key);
    p = strstr(json, keybuf);
    if (!p) {
        return n;
    }
    p = strchr(p, '[');
    if (!p) {
        return n;
    }
    ++p;
    while (*p && n < RNET_LOBBY_MAX_MEMBERS) {
        const char *obj;
        while (*p && *p != '{') {
            if (*p == ']') {
                return n;
            }
            ++p;
        }
        if (*p != '{') {
            break;
        }
        obj = p;
        {
            int depth = 0;
            const char *end = p;
            do {
                if (*end == '{') {
                    ++depth;
                } else if (*end == '}') {
                    --depth;
                }
                ++end;
            } while (*end && depth > 0);
            {
                /* Wide enough for a slot row carrying a full mod_offer. At
                 * 512 the row was silently clipped and the peer's set parsed
                 * short -- which reads as "missing", so the gate would hold a
                 * match that should have started. */
                char chunk[RNET_LOBBY_MAX_MODS * 256 + 512];
                size_t len = (size_t)(end - obj);
                int clipped = 0;
                if (len >= sizeof(chunk)) {
                    len = sizeof(chunk) - 1;
                    clipped = 1;
                }
                memcpy(chunk, obj, len);
                chunk[len] = '\0';
                g_lc.members[n].slot = json_get_int(chunk, "slot", n);
                json_get_str(chunk, "player_id", g_lc.members[n].player_id,
                             sizeof(g_lc.members[n].player_id));
                g_lc.members[n].path[0] = '\0';
                json_get_str(chunk, "path", g_lc.members[n].path,
                             sizeof(g_lc.members[n].path));
                g_lc.members[n].path_fresh = json_get_bool(chunk, "path_fresh", 0) ? 1 : 0;
                /* mod_offer is an OBJECT -- {"pkgs":[...]} -- not a bare
                 * array. The server requires an object (sanitize_mod_offer
                 * rejects anything else) and echoes it back verbatim, so the
                 * array has to be reached through it.
                 *
                 * Read as a bare array this returned zero rows every time, so
                 * the host saw every peer as owning nothing: a guest with both
                 * mods installed and showing OK on its own screen was still
                 * refused at Play, and the message named a mod it had. */
                g_lc.member_offer_count[n] = 0;
                if (!clipped) {
                    char offer_obj[RNET_LOBBY_MAX_MODS * 256 + 64];
                    if (json_extract_object(chunk, "mod_offer", offer_obj,
                                            sizeof(offer_obj)))
                        g_lc.member_offer_count[n] =
                            parse_mod_pkg_array(offer_obj, "pkgs",
                                                g_lc.member_offer[n],
                                                RNET_LOBBY_MAX_MODS);
                }
                if (clipped)
                    fprintf(stderr,
                            "rnet_lobby: slot %d row did not fit; treating its "
                            "mod set as unknown\n", n);
                json_get_str(chunk, "display_name", g_lc.members[n].display_name,
                             sizeof(g_lc.members[n].display_name));
                g_lc.members[n].ready = json_get_bool(chunk, "ready", 0);
                g_lc.members[n].is_spectator = is_spectator;
                json_get_str(chunk, "country", g_lc.members[n].country,
                             sizeof(g_lc.members[n].country));
                if (g_lc.player_id[0] &&
                    strcmp(g_lc.members[n].player_id, g_lc.player_id) == 0) {
                    g_lc.local_ready = g_lc.members[n].ready;
                    /* Seat swaps only arrive via lobby_update slots — keep
                     * join.local_slot in sync for launch / netplay_cfg. */
                    g_lc.join.local_slot = g_lc.members[n].slot;
                    /* And the role, which a host move can change under us at
                     * any moment. Everything downstream -- whether this build
                     * contributes input, whether it may press Ready -- reads
                     * this, so it has to be refreshed from the same update
                     * that moved the seat. */
                    g_lc.join.local_is_spectator = is_spectator;
                }
                ++n;
                p = end;
            }
        }
    }
    return n;
}

static void chat_push(const char *player_id, const char *account,
                      const char *from, const char *text, const char *mid,
                      int is_system)
{
    RNetLobbyChatMsg *m;
    int idx;
    if (!text || !text[0]) return;
    if (g_lc.chat_count < RNET_LOBBY_CHAT_RING) {
        idx = (g_lc.chat_head + g_lc.chat_count) % RNET_LOBBY_CHAT_RING;
        g_lc.chat_count++;
    } else {
        idx = g_lc.chat_head;
        g_lc.chat_head = (g_lc.chat_head + 1) % RNET_LOBBY_CHAT_RING;
    }
    m = &g_lc.chat[idx];
    memset(m, 0, sizeof(*m));
    snprintf(m->player_id, sizeof(m->player_id), "%s", player_id ? player_id : "");
    snprintf(m->account, sizeof(m->account), "%s", account ? account : "");
    snprintf(m->from, sizeof(m->from), "%s", from ? from : "");
    snprintf(m->text, sizeof(m->text), "%s", text);
    snprintf(m->mid, sizeof(m->mid), "%s", (mid && !is_system) ? mid : "");
    /* Masked on arrival, whatever relayed it: the server already did this,
     * an older server did not, and the rule is that nothing unmasked is
     * ever shown. */
    if (!is_system) (void)rnet_chat_filter_apply(m->text, sizeof(m->text));
    m->is_system = is_system ? 1 : 0;
    /* "Mine" is decided by player id, not by having just sent something: the
     * server echoes our own line back like everyone else's, and that echo is
     * the copy the ring keeps. */
    m->is_local = (!is_system && g_lc.player_id[0] && player_id &&
                   strcmp(player_id, g_lc.player_id) == 0) ? 1 : 0;
    m->seq = ++g_lc.chat_seq;
}

static void schat_push(const char *player_id, const char *account,
                       const char *from, const char *text, const char *mid)
{
    RNetLobbyChatMsg *m;
    int idx;
    if (!text || !text[0]) return;
    if (g_lc.schat_count < RNET_LOBBY_CHAT_RING) {
        idx = (g_lc.schat_head + g_lc.schat_count) % RNET_LOBBY_CHAT_RING;
        g_lc.schat_count++;
    } else {
        idx = g_lc.schat_head;
        g_lc.schat_head = (g_lc.schat_head + 1) % RNET_LOBBY_CHAT_RING;
    }
    m = &g_lc.schat[idx];
    memset(m, 0, sizeof(*m));
    snprintf(m->player_id, sizeof(m->player_id), "%s", player_id ? player_id : "");
    snprintf(m->account, sizeof(m->account), "%s", account ? account : "");
    snprintf(m->from, sizeof(m->from), "%s", from ? from : "");
    snprintf(m->text, sizeof(m->text), "%s", text);
    snprintf(m->mid, sizeof(m->mid), "%s", mid ? mid : "");
    (void)rnet_chat_filter_apply(m->text, sizeof(m->text));
    m->is_local = (g_lc.player_id[0] && player_id &&
                   strcmp(player_id, g_lc.player_id) == 0) ? 1 : 0;
    m->seq = ++g_lc.schat_seq;
}

void rnet_lobby_chat_clear(void)
{
    g_lc.chat_head = 0;
    g_lc.chat_count = 0;
    /* seq keeps counting: a UI comparing "newest seen" must not mistake the
     * first line of a new room for one it already scrolled past. */
}

static void parse_slots_array(const char *json)
{
    int n;
    g_lc.member_count = 0;
    g_lc.local_ready = 0;
    g_lc.join.local_is_spectator = 0;
    /* Default to what we already knew, not to zero.
     *
     * This runs for `launch` as well as `lobby_update`, and the launch message
     * carries no allow_spectators / max_spectators -- it has no reason to.
     * Defaulting those to 0 would erase the gallery's existence at the exact
     * moment the client has to decide whether it is in it. */
    g_lc.join.allow_spectators =
        json_get_bool(json, "allow_spectators", g_lc.join.allow_spectators);
    g_lc.join.max_spectators =
        json_get_int(json, "max_spectators", g_lc.join.max_spectators);
    g_lc.join.spectator_count =
        json_get_int(json, "spectator_count", g_lc.join.spectator_count);
    g_lc.join.spectator_relay_base =
        json_get_int(json, "spectator_relay_base",
                     g_lc.join.spectator_relay_base);
    g_lc.join.spectator_slot_base =
        json_get_int(json, "spectator_slot_base",
                     g_lc.join.spectator_slot_base > 0
                         ? g_lc.join.spectator_slot_base
                         : RNET_LOBBY_SPECTATOR_SLOT_BASE);
    n = parse_seat_array(json, "slots", 0, 0);
    n = parse_seat_array(json, "spectators", 1, n);
    g_lc.member_count = n;
}

static void handle_server_json(const char *json);

/* Parse complete unmasked server text frames from ws_pending; leave remainder. */
static void drain_ws_pending(void)
{
    while (g_lc.ws_pending_len >= 2) {
        size_t i = 0;
        uint8_t b0 = g_lc.ws_pending[i++];
        uint8_t b1 = g_lc.ws_pending[i++];
        int opcode = b0 & 0x0f;
        size_t plen = b1 & 0x7f;
        if (b1 & 0x80) {
            /* Server frames must not be masked. */
            g_lc.ws_pending_len = 0;
            return;
        }
        if (plen == 126) {
            if (g_lc.ws_pending_len < i + 2) {
                return;
            }
            plen = ((size_t)g_lc.ws_pending[i] << 8) | g_lc.ws_pending[i + 1];
            i += 2;
        } else if (plen == 127) {
            g_lc.ws_pending_len = 0;
            return;
        }
        if (g_lc.ws_pending_len < i + plen) {
            return;
        }
        if (opcode == 0x1 && plen + 1 < sizeof(g_lc.rx_http)) {
            char text[4096];
            memcpy(text, g_lc.ws_pending + i, plen);
            text[plen] = '\0';
            handle_server_json(text);
        }
        i += plen;
        memmove(g_lc.ws_pending, g_lc.ws_pending + i, g_lc.ws_pending_len - i);
        g_lc.ws_pending_len -= i;
        if (opcode == 0x8) {
            rnet_lobby_disconnect();
            return;
        }
    }
}

/* The identity message: who we are and what we are playing. Sent once on
 * `welcome` and again whenever the player renames. Both fields are escaped
 * -- a name with a quote in it used to build malformed JSON, which the
 * server drops whole, so the rename simply never happened. */
static void queue_hello(void)
{
    char name_esc[RNET_LOBBY_NAME_LEN * 2 + 8];
    char game_esc[RNET_LOBBY_NAME_LEN * 2 + 8];
    char sess_esc[2048];
    char msg[RNET_LOBBY_NAME_LEN * 4 + 2176];
    const char *sess = rnet_account_session();
    json_escape(g_lc.display_name, name_esc, sizeof(name_esc));
    json_escape(g_lc.filter_game_name, game_esc, sizeof(game_esc));
    /* The session is OPTIONAL and omitted entirely when this client is a
     * guest, which is what keeps an unauthenticated hello byte-identical to
     * the one this client has always sent. */
    if (sess && sess[0]) {
        json_escape(sess, sess_esc, sizeof(sess_esc));
        snprintf(msg, sizeof(msg),
                 "{\"op\":\"hello\",\"display_name\":\"%s\",\"game_name\":\"%s\","
                 "\"session\":\"%s\"}",
                 name_esc, game_esc, sess_esc);
    } else {
        snprintf(msg, sizeof(msg),
                 "{\"op\":\"hello\",\"display_name\":\"%s\",\"game_name\":\"%s\"}",
                 name_esc, game_esc);
    }
    queue_send(msg);
    flush_pending();
}

static void handle_server_json(const char *json)
{
    char op[32];
    json_get_str(json, "op", op, sizeof(op));
    if (strcmp(op, "welcome") == 0) {
        json_get_str(json, "player_id", g_lc.player_id, sizeof(g_lc.player_id));
        {
            /* Say who we are AND what we are playing in the first message:
             * the server scopes players-online and server chat by title, and
             * waiting for a `list` to tell it left a client that chatted
             * first with no title at all. Title only -- never the version. */
            queue_hello();
            fprintf(stderr, "rnet_lobby: hello as \"%s\" for game \"%s\"\n",
                    g_lc.display_name,
                    g_lc.filter_game_name[0] ? g_lc.filter_game_name : "(none)");
        }
        /* The bare list this used to send told the server nothing about the
         * title, so the whole per-game scope stayed empty for this client. */
        queue_list_request();
        /* Prefetch Coturn creds for ICE (no-op reply if server lacks COTURN_*). */
        (void)queue_turn_credentials_request();
        return;
    }
    if (strcmp(op, "turn_credentials") == 0) {
        /* Server sends JSON boolean "ok": true — not an integer. */
        int ok = json_get_bool(json, "ok", 0);
        g_lc.turn_request_pending = 0;
        memset(&g_lc.turn, 0, sizeof(g_lc.turn));
        g_lc.turn_received_at = 0;
        if (!ok) {
            char err[64];
            json_get_str(json, "error", err, sizeof(err));
            fprintf(stderr,
                    "rnet_lobby: turn_credentials failed (%s) — ICE will be "
                    "STUN-only unless the engine's TURN override is set\n",
                    err[0] ? err : "unknown");
            return;
        }
        json_get_str(json, "stun_host", g_lc.turn.stun_host,
                     sizeof(g_lc.turn.stun_host));
        json_get_str(json, "turn_host", g_lc.turn.turn_host,
                     sizeof(g_lc.turn.turn_host));
        json_get_str(json, "username", g_lc.turn.username,
                     sizeof(g_lc.turn.username));
        json_get_str(json, "password", g_lc.turn.password,
                     sizeof(g_lc.turn.password));
        g_lc.turn.stun_port = json_get_int(json, "stun_port", 3478);
        g_lc.turn.turn_port = json_get_int(json, "turn_port", 3478);
        g_lc.turn.ttl_secs = (uint32_t)json_get_int(json, "ttl_secs", 86400);
        if (g_lc.turn.turn_host[0] && g_lc.turn.username[0] &&
            g_lc.turn.password[0]) {
            g_lc.turn.valid = 1;
            g_lc.turn_received_at = time(NULL);
            fprintf(stderr,
                    "rnet_lobby: turn_credentials ok stun=%s:%d turn=%s:%d "
                    "user=%s ttl=%us\n",
                    g_lc.turn.stun_host[0] ? g_lc.turn.stun_host : "(none)",
                    g_lc.turn.stun_port,
                    g_lc.turn.turn_host, g_lc.turn.turn_port,
                    g_lc.turn.username, (unsigned)g_lc.turn.ttl_secs);
        } else {
            fprintf(stderr,
                    "rnet_lobby: turn_credentials ok but incomplete fields\n");
        }
        return;
    }
    if (strcmp(op, "lobby_list") == 0) {
        const char *p = strstr(json, "\"lobbies\"");
        int n = 0;
        lobby_list_parse_players(json);
        g_lc.list_count = 0;
        if (!p) {
            return;
        }
        p = strchr(p, '[');
        if (!p) {
            return;
        }
        ++p;
        while (*p && n < RNET_LOBBY_MAX_LIST) {
            const char *obj;
            while (*p && *p != '{') {
                if (*p == ']') {
                    g_lc.list_count = n;
                    list_probe_on_list();
                    return;
                }
                ++p;
            }
            if (*p != '{') {
                break;
            }
            obj = p;
            {
                int depth = 0;
                const char *end = p;
                do {
                    if (*end == '{') {
                        ++depth;
                    } else if (*end == '}') {
                        --depth;
                    }
                    ++end;
                } while (*end && depth > 0);
                {
                    char chunk[1024];
                    size_t len = (size_t)(end - obj);
                    if (len >= sizeof(chunk)) {
                        len = sizeof(chunk) - 1;
                    }
                    memcpy(chunk, obj, len);
                    chunk[len] = '\0';
                    json_get_str(chunk, "lobby_id", g_lc.list[n].lobby_id, sizeof(g_lc.list[n].lobby_id));
                    json_get_str(chunk, "name", g_lc.list[n].name, sizeof(g_lc.list[n].name));
                    json_get_str(chunk, "game_name", g_lc.list[n].game_name, sizeof(g_lc.list[n].game_name));
                    json_get_str(chunk, "game_version", g_lc.list[n].game_version,
                                 sizeof(g_lc.list[n].game_version));
                    if (!g_lc.list[n].game_version[0])
                        strncpy(g_lc.list[n].game_version, "dev",
                                sizeof(g_lc.list[n].game_version) - 1);
                    if (g_lc.filter_game_name[0] &&
                        strcmp(g_lc.list[n].game_name, g_lc.filter_game_name) != 0) {
                        p = end;
                        continue;
                    }
                    if (list_filter_version_strict()) {
                        const char *want_ver = effective_game_version(NULL);
                        if (want_ver && want_ver[0] &&
                            strcmp(g_lc.list[n].game_version, want_ver) != 0) {
                            p = end;
                            continue;
                        }
                    }
                    g_lc.list[n].player_count = json_get_int(chunk, "player_count", 0);
                    g_lc.list[n].max_slots = json_get_int(chunk, "max_slots", 2);
                    g_lc.list[n].has_password = json_get_bool(chunk, "has_password", 0);
                    json_get_str(chunk, "host_country", g_lc.list[n].host_country,
                                 sizeof(g_lc.list[n].host_country));
                    g_lc.list[n].allow_spectators = json_get_bool(chunk, "allow_spectators", 0);
                    g_lc.list[n].max_spectators = json_get_int(chunk, "max_spectators", 0);
                    g_lc.list[n].spectator_count = json_get_int(chunk, "spectator_count", 0);
                    g_list_host_rtt[n] = json_get_int(chunk, "host_rtt_ms", -1);
                    ++n;
                    p = end;
                }
            }
        }
        g_lc.list_count = n;
        list_probe_on_list();
        return;
    }
    if (strcmp(op, "created") == 0) {
        /* A new room starts with an empty log -- carrying the last room's
         * lines in would show a conversation nobody in this one had. */
        rnet_lobby_chat_clear();
        ice_launch_discard();
        g_lc.in_lobby = 1;
        g_lc.is_host = 1;
        g_lc.join.ok = 1;
        g_lc.launch_pending = 0;
        g_lc.all_ready = 0;
        member_rtt_clear();
        json_get_str(json, "lobby_id", g_lc.join.lobby_id, sizeof(g_lc.join.lobby_id));
        g_lc.join.session_id = (uint32_t)json_get_int(json, "session_id", 1);
        g_lc.join.local_slot = json_get_int(json, "local_slot", 0);
        json_get_str(json, "host_endpoint", g_lc.join.host_endpoint, sizeof(g_lc.join.host_endpoint));
        json_get_str(json, "guest_endpoint", g_lc.join.guest_endpoint, sizeof(g_lc.join.guest_endpoint));
        json_get_str(json, "host_player_id", g_lc.host_player_id,
                     sizeof(g_lc.host_player_id));
        if (!g_lc.host_player_id[0])
            strncpy(g_lc.host_player_id, g_lc.player_id,
                    sizeof(g_lc.host_player_id) - 1);
        /* The server states both; the old constants (1 and 2) stand for a
         * server that does not, and were wrong for any room above two seats
         * until the first lobby_update corrected them. */
        g_lc.join.player_count = json_get_int(json, "player_count", 1);
        g_lc.join.max_slots = json_get_int(json, "max_slots", 2);
        g_lc.join.host_spectates = 0;
        g_lc.join.last_error[0] = '\0';
        ingest_match_caps_from_json(json);
        fill_peer_bind_from_join();
        parse_slots_array(json);
        if (g_lc.member_count == 0) {
            g_lc.members[0].slot = 0;
            strncpy(g_lc.members[0].player_id, g_lc.player_id, sizeof(g_lc.members[0].player_id) - 1);
            strncpy(g_lc.members[0].display_name, g_lc.display_name,
                    sizeof(g_lc.members[0].display_name) - 1);
            g_lc.members[0].ready = 0;
            g_lc.member_count = 1;
            g_lc.local_ready = 0;
        }
        /* Ready UI is gone; auto-ready so older lobby servers that still gate
         * start on all_ready accept host Play. */
        send_set_ready(1);
        flush_pending();
        return;
    }
    if (strcmp(op, "joined") == 0) {
        rnet_lobby_chat_clear();
        /* A ticket that reached a room is spent. `joined` is that moment for
         * automatch (docs/AUTOMATCH.md 8: automatch_accept_ok -> both accepted
         * -> joined -> lobby_update -> launch), and nothing else used to leave
         * ACCEPTED: the state survived the whole match and the accept gate
         * reopened over the lobby list on the way out, offering to wait for a
         * peer to answer an offer the server had closed to create this room.
         * Not an automatch special case -- the server refuses to queue an
         * account that is already in a lobby (`already_in_lobby`), so holding
         * a ticket and being seated are mutually exclusive either way. */
        /* Recorded before the reset below, which is what erases the evidence:
         * ACCEPTED (or FOUND, if the pair resolved in the same breath) is the
         * only signal that this seat came from a queue rather than from
         * somebody's room. */
        g_am.in_automatch_room =
            (g_am.state == RNET_LOBBY_AUTOMATCH_ACCEPTED ||
             g_am.state == RNET_LOBBY_AUTOMATCH_FOUND);
        automatch_reset_queue_state();
        ice_launch_discard();
        g_lc.in_lobby = 1;
        g_lc.is_host = 0;
        g_lc.join.ok = 1;
        g_lc.launch_pending = 0;
        g_lc.all_ready = 0;
        member_rtt_clear();
        json_get_str(json, "lobby_id", g_lc.join.lobby_id, sizeof(g_lc.join.lobby_id));
        g_lc.join.session_id = (uint32_t)json_get_int(json, "session_id", 1);
        g_lc.join.local_slot = json_get_int(json, "local_slot", 1);
        json_get_str(json, "host_endpoint", g_lc.join.host_endpoint, sizeof(g_lc.join.host_endpoint));
        json_get_str(json, "guest_endpoint", g_lc.join.guest_endpoint, sizeof(g_lc.join.guest_endpoint));
        json_get_str(json, "host_player_id", g_lc.host_player_id,
                     sizeof(g_lc.host_player_id));
        g_lc.join.player_count = json_get_int(json, "player_count", 2);
        g_lc.join.max_slots = json_get_int(json, "max_slots", 2);
        g_lc.join.host_spectates = 0;
        g_lc.join.last_error[0] = '\0';
        ingest_match_caps_from_json(json);
        fill_peer_bind_from_join();
        parse_slots_array(json);
        send_set_ready(1);
        flush_pending();
        return;
    }
    if (strcmp(op, "lobby_update") == 0) {
        json_get_str(json, "host_endpoint", g_lc.join.host_endpoint, sizeof(g_lc.join.host_endpoint));
        json_get_str(json, "guest_endpoint", g_lc.join.guest_endpoint, sizeof(g_lc.join.guest_endpoint));
        json_get_str(json, "host_player_id", g_lc.host_player_id,
                     sizeof(g_lc.host_player_id));
        g_lc.join.player_count = json_get_int(json, "player_count", g_lc.join.player_count);
        g_lc.join.max_slots = json_get_int(json, "max_slots", g_lc.join.max_slots);
        g_lc.join.session_id = (uint32_t)json_get_int(json, "session_id", (int)g_lc.join.session_id);
        g_lc.all_ready = json_get_bool(json, "all_ready", 0);
        ingest_match_caps_from_json(json);
        fill_peer_bind_from_join();
        parse_slots_array(json);
        /* The server clears every guest's path report on a roster change and
         * says so only by omitting `path` from the seat rows: prove ours again
         * now (rnet_host_relay_path_cleared). */
        {
            int i;
            for (i = 0; i < g_lc.member_count; ++i) {
                const RNetLobbyMember *m = &g_lc.members[i];
                if (m->is_spectator || !g_lc.player_id[0] ||
                    strcmp(m->player_id, g_lc.player_id) != 0)
                    continue;
                if (!m->path[0]) rnet_host_relay_path_cleared(g_host_relay);
                break;
            }
        }
        /* Kick/move/start clear ready; re-arm so host Play keeps working on
         * servers that still require all_ready.
         *
         * Never from the gallery: the server keeps `ready` in the PLAYER
         * table only (a spectator cannot hold up a start), so a spectator's
         * set_ready changes nothing it will ever read back as ready -- but
         * the server still answers it with a lobby_update, which re-armed it
         * again: an unbounded set_ready / lobby_update storm, tens of
         * thousands of frames per second across the room, behind which the
         * rematch's op:launch was never read (Genesis, 4 players + 1
         * spectator, round 2: no peer launched). */
        if (g_lc.in_lobby && !g_lc.local_ready && !g_lc.join.local_is_spectator &&
            !g_launch_blocked) {
            send_set_ready(1);
            flush_pending();
        }
        return;
    }
    if (strcmp(op, "launch") == 0) {
        char relay_endpoint[RNET_LOBBY_ENDPOINT_LEN];
        char transport[16];
        json_get_str(json, "host_endpoint", g_lc.join.host_endpoint, sizeof(g_lc.join.host_endpoint));
        json_get_str(json, "guest_endpoint", g_lc.join.guest_endpoint, sizeof(g_lc.join.guest_endpoint));
        relay_endpoint[0] = '\0';
        json_get_str(json, "relay_endpoint", relay_endpoint, sizeof(relay_endpoint));
        /* "host": the host carries the match (WS_LOBBY.md "Host relay"); the
         * server sends no relay_endpoint and host_endpoint is the host's
         * advertised port. The waiting-room socket on that port goes now, so
         * the game can bind it; the router mapping stays for the game. */
        transport[0] = '\0';
        json_get_str(json, "transport", transport, sizeof(transport));
        g_lc.join.transport_host = strcmp(transport, "host") == 0 ? 1 : 0;
        g_launch_named_ice = (strcmp(transport, "ice") == 0 ||
                              strcmp(transport, "ice_p2p") == 0) &&
                             !relay_endpoint[0];
        rnet_host_relay_release_port(g_host_relay);
        ice_launch_discard();   /* a previous launch's agents are never reused */
        g_il.error[0] = '\0';
        g_lc.join.player_count = json_get_int(json, "player_count", g_lc.join.player_count);
        g_lc.join.max_slots = json_get_int(json, "max_slots", g_lc.join.max_slots);
        g_lc.join.session_id = (uint32_t)json_get_int(json, "session_id", (int)g_lc.join.session_id);
        /* Settled by the server once, at start; every peer derives its
         * session slot from it, so it is read here and nowhere else. */
        g_lc.join.host_spectates = json_get_bool(json, "host_spectates", 0);
        ingest_match_caps_from_json(json);
        /* The server echoes relay_via in the launch (and in launch.match_caps).
         * "ice" with transport "host" = the host carries the match over the
         * waiting-room agents; host_endpoint is empty by design. */
        {
            /* Decided by THIS launch alone (top level or its match_caps), never
             * by caps carried over from an earlier message. */
            char via[12];
            via[0] = '\0';
            json_get_str(json, "relay_via", via, sizeof(via));
            g_lc.match_caps.relay_via_ice = !strcmp(via, "ice") ? 1 : 0;
            if (g_lc.match_caps.relay_via_ice) g_lc.match_caps.valid = 1;
        }
        g_lc.join.transport_ice_hub =
            (g_lc.join.transport_host && g_lc.match_caps.relay_via_ice) ? 1 : 0;
        if (relay_endpoint[0] && endpoint_has_usable_port(relay_endpoint)) {
            strncpy(g_lc.join.host_endpoint, relay_endpoint,
                    sizeof(g_lc.join.host_endpoint) - 1);
            g_lc.join.host_endpoint[sizeof(g_lc.join.host_endpoint) - 1] = '\0';
            strncpy(g_lc.join.guest_endpoint, relay_endpoint,
                    sizeof(g_lc.join.guest_endpoint) - 1);
            g_lc.join.guest_endpoint[sizeof(g_lc.join.guest_endpoint) - 1] = '\0';
            if (!g_lc.match_caps.valid)
                g_lc.match_caps.valid = 1;
            g_lc.match_caps.force_input_relay = 1;
        }
        /* Decide this match's transport HERE, and record it on the join.
         *
         * It used to be re-read from match_caps at the moment the game
         * actually started, which is a later moment: every lobby_update
         * re-ingests the host's published caps, whose force_input_relay is the
         * host's UI toggle (default off) rather than the server's allocation.
         * A republish landing in that window -- a mod-plan change, a ready
         * toggle, someone joining -- erased the fact that the server had
         * handed out a relay, and the match fell back to p2p ICE. That is why
         * it "happened inconsistently": it depended on lobby traffic timing.
         *
         * Restated on every launch, both ways, so a relayed match cannot leave
         * a 1 behind for the next p2p one. */
        g_lc.join.force_input_relay =
            !g_lc.join.transport_host && using_server_input_relay(&g_lc.join) ? 1 : 0;
        if (g_lc.join.transport_host) g_lc.match_caps.force_input_relay = 0;
        fill_peer_bind_from_join();
        parse_slots_array(json);
        /* Guest must know the host. Host may leave peer empty to learn the
         * guest from the first UDP packet (LAN / legacy guest_bind :0). */
        if (g_lc.join.transport_ice_hub) {
            /* No endpoints: the match rides the agents already connected in
             * the waiting room. Capture them NOW, before the next pump's view
             * (launch_pending) tears them down; the engine takes them. */
            ice_launch_capture();
            if (g_il.error[0]) {
                strncpy(g_lc.join.last_error, "ice_not_connected",
                        sizeof(g_lc.join.last_error) - 1);
                g_lc.launch_pending = 0;
                fprintf(stderr, "rnet_lobby: launch refused - %s\n", g_il.error);
                return;
            }
            g_lc.join.ok = 1;
            g_lc.join.last_error[0] = '\0';
            g_lc.launch_pending = 1;
            return;
        }
        if (!g_lc.join.host_endpoint[0] || !g_lc.join.bind_hostport[0] ||
            (!g_lc.is_host && !g_lc.join.peer_hostport[0] &&
             !using_server_input_relay(&g_lc.join))) {
            strncpy(g_lc.join.last_error, "missing_endpoints",
                    sizeof(g_lc.join.last_error) - 1);
            g_lc.launch_pending = 0;
            return;
        }
        /* A prior lobby error must not leave join.ok=0 or fill_launch will
         * refuse the match forever while launch_pending stays sticky. */
        g_lc.join.ok = 1;
        g_lc.join.last_error[0] = '\0';
        g_lc.launch_pending = 1;
        return;
    }
    if (strcmp(op, "seat_swap_ask") == 0) {
        g_lc.swap_in_valid = 1;
        json_get_str(json, "asker_player_id", g_lc.swap_in_asker_id,
                     sizeof(g_lc.swap_in_asker_id));
        json_get_str(json, "asker_name", g_lc.swap_in_asker_name,
                     sizeof(g_lc.swap_in_asker_name));
        g_lc.swap_in_from_slot = json_get_int(json, "from_slot", -1);
        return;
    }
    if (strcmp(op, "seat_swap_result") == 0) {
        g_lc.swap_out = json_get_bool(json, "accept", 0) ? 2 : -1;
        return;
    }
    if (strcmp(op, "server_chat") == 0) {
        char text[RNET_LOBBY_CHAT_TEXT_LEN];
        char from_id[RNET_LOBBY_ID_LEN];
        char from_acct[RNET_LOBBY_ID_LEN];
        char from[RNET_LOBBY_NAME_LEN];
        char mid[40];
        text[0] = '\0';
        from_id[0] = '\0';
        from_acct[0] = '\0';
        from[0] = '\0';
        mid[0] = '\0';
        json_get_str(json, "text", text, sizeof(text));
        json_get_str(json, "from_player_id", from_id, sizeof(from_id));
        json_get_str(json, "from_account", from_acct, sizeof(from_acct));
        json_get_str(json, "from", from, sizeof(from));
        json_get_str(json, "mid", mid, sizeof(mid));
        schat_push(from_id, from_acct, from, text, mid);
        return;
    }
    if (strcmp(op, "chat") == 0) {
        char text[RNET_LOBBY_CHAT_TEXT_LEN];
        char from_id[RNET_LOBBY_ID_LEN];
        char from_acct[RNET_LOBBY_ID_LEN];
        char from[RNET_LOBBY_NAME_LEN];
        char mid[40];
        text[0] = '\0';
        from_id[0] = '\0';
        from_acct[0] = '\0';
        from[0] = '\0';
        mid[0] = '\0';
        json_get_str(json, "text", text, sizeof(text));
        json_get_str(json, "from_player_id", from_id, sizeof(from_id));
        json_get_str(json, "from_account", from_acct, sizeof(from_acct));
        json_get_str(json, "from", from, sizeof(from));
        json_get_str(json, "mid", mid, sizeof(mid));
        chat_push(from_id, from_acct, from, text, mid,
                  json_get_bool(json, "system", 0));
        return;
    }
    if (strcmp(op, "probe_signal") == 0) {
        char text[2048];
        char from[RNET_LOBBY_ID_LEN];
        char lid[RNET_LOBBY_ID_LEN];
        text[0] = from[0] = lid[0] = '\0';
        json_get_str(json, "text", text, sizeof(text));
        json_get_str(json, "from_player_id", from, sizeof(from));
        json_get_str(json, "lobby_id", lid, sizeof(lid));
        list_probe_on_signal(lid, from, json_get_int(json, "type", 0),
                             json_get_int(json, "flag", 0), text);
        return;
    }
    if (strcmp(op, "signal") == 0) {
        char text[2048];
        char from[RNET_LOBBY_ID_LEN];
        int type = json_get_int(json, "type", 0);
        int flag = json_get_int(json, "flag", 0);
        text[0] = '\0';
        from[0] = '\0';
        json_get_str(json, "text", text, sizeof(text));
        json_get_str(json, "from_player_id", from, sizeof(from));
        if (type == RNET_LOBBY_SIG_RTT_PING) {
            /* Only the host answers latency probes. */
            if (g_lc.is_host)
                (void)rnet_lobby_send_signal(RNET_LOBBY_SIG_RTT_PONG, 0, text);
            return;
        }
        if (type == RNET_LOBBY_SIG_RTT_PONG) {
            unsigned long long sent = 0;
            uint64_t now = lobby_mono_ms();
            int slot;
            if (sscanf(text, "%llu", &sent) == 1 && (uint64_t)sent <= now) {
                int ms = (int)(now - (uint64_t)sent);
                if (ms < 0) ms = 0;
                if (ms > 60000) ms = 60000;
                slot = rtt_index_for_slot(local_member_slot());
                if (slot >= 0)
                    g_lc.member_rtt_ms[slot] = ms;
                /* Tell the host (and peers) our measured RTT to host. */
                {
                    char report[32];
                    snprintf(report, sizeof(report), "%d", ms);
                    (void)rnet_lobby_send_signal(RNET_LOBBY_SIG_RTT_REPORT, 0,
                                                 report);
                }
            }
            return;
        }
        if (type == RNET_LOBBY_SIG_RTT_REPORT) {
            int slot = rtt_index_for_slot(member_slot_for_player(from));
            int ms = (int)strtol(text, NULL, 10);
            if (slot >= 0 && ms >= 0 && ms <= 60000)
                g_lc.member_rtt_ms[slot] = ms;
            return;
        }
        if (type == RNET_LOBBY_SIG_MOD_REQ) {
            mod_xfer_on_request(from, text);
            return;
        }
        if (type == RNET_LOBBY_SIG_MOD_NAK) {
            mod_xfer_fail(text[0] ? text : "the host refused");
            return;
        }
        if (rnet_host_ice_sig_is_ours(type)) {
            /* Host-relay-over-ICE waiting-room agents. Dispatched by the
             * sender's player id, which the SERVER stamps (a client cannot
             * choose it); only a seated player is a peer, and the seat is
             * looked up here, never read from the payload. */
            const int from_slot = member_slot_for_player(from);
            if (!g_host_ice || !from[0] || from_slot < 0 ||
                member_is_spectator(from)) {
                fprintf(stderr, "rnet_lobby: host-ice signal from \"%s\" refused "
                                "(not a seated player)\n", from);
                return;
            }
            (void)rnet_host_ice_push_signal(g_host_ice, from, from_slot, type, flag,
                                            text);
            return;
        }
        if (type > RNET_LOBBY_SIG_MOD_ICE_BASE &&
            type <= RNET_LOBBY_SIG_MOD_ICE_BASE + 6) {
            /* Only from the peer we are actually transferring with. An SDP
             * from anyone else is either a stale exchange or someone else's,
             * and feeding it to the agent breaks the live negotiation. */
            RNetSignal sig;
            const int t = mod_ice_type_for_push(type -
                                                RNET_LOBBY_SIG_MOD_ICE_BASE);
            memset(&sig, 0, sizeof(sig));
            sig.type = (RNetSignalType)t;
            sig.flag = (rnet_u8)flag;
            sig.peer_slot = 0xFF;
            snprintf(sig.text, sizeof(sig.text), "%s", text);
            if (g_lc.xfer && from[0] && !strcmp(from, g_lc.xfer_peer)) {
                rnet_ice_xfer_push_signal(g_lc.xfer, &sig);
            } else if (from[0]) {
                /* No agent yet: the sender is still packing. Hold it, in the
                 * sender's own bucket. Overflowing means the peer gathered far
                 * more candidates than a handshake needs while we did nothing
                 * with them -- the newest is dropped, which keeps the offer. */
                if (rnet_sig_hold_push(&g_lc.sig_hold, from, -1, &sig,
                                       lobby_mono_ms()) != 0)
                    fprintf(stderr, "rnet_lobby: ICE hold buffer full; "
                                    "dropping a candidate\n");
            }
            return;
        }
        if (!ice_signal_is_for_us(type, from))
            return;
        enqueue_signal(type, flag, text);
        (void)flag;
        return;
    }
    if (strcmp(op, "need_mods") == 0) {
        /* Not op:"error": the server answers a refused join with its own op,
         * carrying the list. Falling through to the generic handler used to
         * drop it entirely -- no error code, no list, nothing logged -- so a
         * player saw the join simply do nothing and the log showed a clean
         * session. A refusal has to explain itself where it happens. */
        int i;
        g_lc.need_mods_count = parse_mod_pkg_array(json, "mods", g_lc.need_mods,
                                                   RNET_LOBBY_MAX_MODS);
        g_lc.need_mods_can_transfer = json_get_bool(json, "can_transfer", 0);
        json_get_str(json, "lobby_id", g_lc.need_mods_lobby_id,
                     sizeof(g_lc.need_mods_lobby_id));
        json_get_str(json, "host_player_id", g_lc.need_mods_host_player_id,
                     sizeof(g_lc.need_mods_host_player_id));
        snprintf(g_lc.join.last_error, sizeof(g_lc.join.last_error),
                 "need_mods");
        g_lc.join.ok = 0;
        g_lc.in_lobby = 0;
        fprintf(stderr,
                "rnet_lobby: refused - this lobby needs %d mod(s) this build "
                "does not have (server can_transfer=%d)\n",
                g_lc.need_mods_count, g_lc.need_mods_can_transfer);
        for (i = 0; i < g_lc.need_mods_count; ++i)
            fprintf(stderr, "rnet_lobby:   missing %s@%s%s%s\n",
                    g_lc.need_mods[i].id, g_lc.need_mods[i].ver,
                    g_lc.need_mods[i].name[0] ? " - " : "",
                    g_lc.need_mods[i].name);
        if (g_lc.need_mods_count == 0)
            fprintf(stderr,
                    "rnet_lobby:   the server named none, which means it read "
                    "a plan it could not parse -- check the host's build\n");
        return;
    }
    /* ── automatch ─────────────────────────────────────────────────────── */
    if (strcmp(op, "automatch_rulesets_ok") == 0) {
        const char *p2 = strstr(json, "\"rulesets\"");
        char probe[256];
        int n = 0;
        g_am.have_rulesets = 1;
        g_am.rulesets_in_flight = 0;
        g_am.ruleset_count = 0;
        if (json_extract_object(json, "probe", probe, sizeof(probe)))
            automatch_ingest_probe(probe);
        if (!p2) return;
        p2 = strchr(p2, '[');
        if (!p2) return;
        ++p2;
        while (*p2 && n < RNET_LOBBY_MAX_RULESETS) {
            char obj[1024];
            if (!automatch_next_object(&p2, obj, sizeof(obj))) break;
            {
                RNetLobbyRuleset *r = &g_am.rulesets[n];
                char caps[512];
                memset(r, 0, sizeof(*r));
                json_get_str(obj, "id", r->id, sizeof(r->id));
                json_get_str(obj, "label", r->label, sizeof(r->label));
                json_get_str(obj, "caps_summary", r->caps_summary,
                             sizeof(r->caps_summary));
                json_get_str(obj, "game_version", r->game_version,
                             sizeof(r->game_version));
                if (json_extract_object(obj, "match_caps", caps, sizeof(caps)))
                    parse_match_caps_object(caps, &r->caps);
                if (r->id[0]) ++n;
            }
        }
        g_am.ruleset_count = n;
        fprintf(stderr, "rnet_lobby: automatch %d ruleset(s) for \"%s\"\n",
                n, g_lc.filter_game_name);
        /* Measure now rather than at queue time: a client that has already
         * probed never waits out the server's probe grace. */
        if (n > 0 && g_am.rtt_ms < 0) automatch_probe_send();
        return;
    }
    if (strcmp(op, "automatch_queued") == 0) {
        char probe[256];
        g_am.state = RNET_LOBBY_AUTOMATCH_QUEUED;
        g_am.error[0] = '\0';
        g_am.queue_in_flight = 0;
        json_get_str(json, "ticket_id", g_am.ticket_id, sizeof(g_am.ticket_id));
        g_am.queued_secs = 0;
        g_am.pool = automatch_first_pool(json);
        if (json_extract_object(json, "probe", probe, sizeof(probe)))
            automatch_ingest_probe(probe);
        if (g_am.rtt_ms < 0) automatch_probe_send();
        else automatch_send_rtt();
        fprintf(stderr, "rnet_lobby: automatch queued (pool=%d)\n", g_am.pool);
        return;
    }
    if (strcmp(op, "automatch_status") == 0) {
        /* Pushed at most 1 Hz while queued. Not a state change: a status for
         * a ticket we already gave up on must not resurrect the queue. */
        if (g_am.state != RNET_LOBBY_AUTOMATCH_QUEUED) return;
        g_am.queued_secs = json_get_int(json, "queued_secs", g_am.queued_secs);
        g_am.pool = automatch_first_pool(json);
        return;
    }
    if (strcmp(op, "automatch_found") == 0) {
        memset(&g_am.found, 0, sizeof(g_am.found));
        json_get_str(json, "ticket_id", g_am.ticket_id, sizeof(g_am.ticket_id));
        json_get_str(json, "opponent", g_am.found.opponent,
                     sizeof(g_am.found.opponent));
        json_get_str(json, "opponent_country", g_am.found.opponent_country,
                     sizeof(g_am.found.opponent_country));
        json_get_str(json, "ruleset_id", g_am.found.ruleset_id,
                     sizeof(g_am.found.ruleset_id));
        json_get_str(json, "label", g_am.found.ruleset_label,
                     sizeof(g_am.found.ruleset_label));
        g_am.found.est_rtt_ms = json_get_int(json, "est_rtt_ms", 0);
        g_am.found.accept_secs = json_get_int(json, "accept_secs", 15);
        g_am.found_deadline_ms =
            lobby_mono_ms() + (uint64_t)(g_am.found.accept_secs > 0
                                             ? g_am.found.accept_secs : 0) * 1000ull;
        g_am.state = RNET_LOBBY_AUTOMATCH_FOUND;
        fprintf(stderr, "rnet_lobby: automatch found opponent=\"%s\" "
                        "est_rtt=%d ms, %d s to answer\n",
                g_am.found.opponent, g_am.found.est_rtt_ms,
                g_am.found.accept_secs);
        return;
    }
    if (strcmp(op, "automatch_accept_ok") == 0) {
        /* The echo carries WHICH answer was acknowledged, and honouring it is
         * not optional: this used to set ACCEPTED unconditionally, so a player
         * who clicked Decline went to IDLE, received the server's ack a
         * moment later, and had the accept gate reopen on them reading
         * "Waiting for <opponent> to accept..." -- the dialog for the choice
         * they had just refused, over a match that was already finished.
         *
         * A declined ack ends the ticket here. The server's automatch_cancelled
         * follows and is idempotent with this. */
        if (json_get_bool(json, "accept", 1)) {
            g_am.state = RNET_LOBBY_AUTOMATCH_ACCEPTED;
        } else {
            automatch_reset_queue_state();
        }
        return;
    }
    if (strcmp(op, "automatch_requeue") == 0) {
        /* The other side declined or let it lapse. Back to waiting, with the
         * ticket intact -- this is not a failure and must not read as one. */
        g_am.state = RNET_LOBBY_AUTOMATCH_QUEUED;
        memset(&g_am.found, 0, sizeof(g_am.found));
        g_am.pool = automatch_first_pool(json);
        fprintf(stderr, "rnet_lobby: automatch re-queued (the offer lapsed)\n");
        return;
    }
    if (strcmp(op, "automatch_cancelled") == 0) {
        automatch_reset_queue_state();
        return;
    }
    if (strcmp(op, "automatch_rtt_ok") == 0) {
        return;   /* acknowledgement only */
    }
    if (strcmp(op, "pong") == 0) {
        uint64_t now = lobby_mono_ms();
        if (g_srv_ping_sent_ms && now >= g_srv_ping_sent_ms) {
            int ms = (int)(now - g_srv_ping_sent_ms);
            if (ms > 2000) ms = 2000; /* the server clamps reports here too */
            g_srv_rtt_ms = g_srv_rtt_ms < 0 ? ms : (g_srv_rtt_ms * 3 + ms + 2) / 4;
            g_srv_ping_sent_ms = 0;
            /* Report the first figure, then only real changes. */
            if (g_srv_rtt_reported < 0 || g_srv_rtt_ms > g_srv_rtt_reported + 5 ||
                g_srv_rtt_ms + 5 < g_srv_rtt_reported) {
                char msg[96];
                snprintf(msg, sizeof(msg), "{\"op\":\"automatch_rtt\",\"rtt_ms\":%d}", g_srv_rtt_ms);
                queue_send(msg);
                g_srv_rtt_reported = g_srv_rtt_ms;
            }
        }
        return;
    }
    if (strcmp(op, "error") == 0) {
        char detail[32];
        detail[0] = '\0';
        json_get_str(json, "detail", detail, sizeof(detail));
        if (strcmp(detail, "probe_signal") == 0) {
            /* A server that predates the list probe: not a join failure, and
             * not worth asking again on this connection. The estimate stands. */
            list_probe_unsupported();
            return;
        }
        json_get_str(json, "code", g_lc.join.last_error, sizeof(g_lc.join.last_error));
        /* An automatch refusal arrives as a plain error, so it has to be
         * claimed here or it would be filed as a join failure and the queue
         * would sit waiting for a pairing that was never going to come. Only
         * while an automatch attempt is actually in flight: these codes are
         * automatch's, but `error` is everyone's. */
        if (g_am.queue_in_flight ||
            g_am.state == RNET_LOBBY_AUTOMATCH_QUEUED ||
            g_am.state == RNET_LOBBY_AUTOMATCH_FOUND ||
            g_am.state == RNET_LOBBY_AUTOMATCH_ACCEPTED) {
            const char *code = g_lc.join.last_error;
            const char *why = NULL;
            char line[160];
            if      (!strcmp(code, "need_account"))       why = "Sign in to use automatch";
            else if (!strcmp(code, "automatch_off"))      why = "This server has no automatch queues";
            else if (!strcmp(code, "already_queued"))     why = "This account is already in a queue";
            else if (!strcmp(code, "already_in_lobby"))   why = "Leave the room first";
            else if (!strcmp(code, "unknown_ruleset"))    why = "That queue type is gone -- refresh";
            else if (!strcmp(code, "need_disc_fp"))       why = "The server needs a ROM fingerprint this build did not send";
            else if (!strcmp(code, "version_not_pooled")) why = "This release is not the one this queue pools";
            else if (!strcmp(code, "mods_not_pooled"))    why = "Turn off sim-affecting mods to queue";
            else if (!strcmp(code, "slots_not_pooled"))   why = "Automatch is two-player only";
            else if (!strcmp(code, "queue_full"))         why = "The queue is full -- try again shortly";
            else if (!strcmp(code, "cooldown")) {
                int retry = json_get_int(json, "retry_secs", 0);
                if (retry > 0)
                    snprintf(line, sizeof(line),
                             "%d Second Cooldown For Declining", retry);
                else
                    snprintf(line, sizeof(line), "Cooldown For Declining");
                why = line;
            }
            if (why) {
                g_am.queue_in_flight = 0;
                automatch_fail(why);
                return;
            }
        }
        /* Keep seating valid: start/need_players/etc. must not block a later
         * successful op:launch from filling netplay_launch. */
        if (!g_lc.in_lobby)
            g_lc.join.ok = 0;
        return;
    }
    if (strcmp(op, "lobby_closed") == 0 || strcmp(op, "left") == 0 ||
        strcmp(op, "kicked") == 0) {
        rnet_lobby_chat_clear();
        /* Leaving a room must never leave a queue ticket looking live. The
         * reset on `joined` above should already have done this; repeating it
         * here means a missed or reordered `joined` cannot strand the gate. */
        automatch_reset_queue_state();
        ice_launch_discard();
        g_am.in_automatch_room = 0;   /* no longer seated anywhere */
        g_lc.swap_in_valid = 0;
        g_lc.swap_out = 0;
        g_lc.in_lobby = 0;
        g_lc.is_host = 0;
        g_lc.host_player_id[0] = '\0';
        g_lc.member_count = 0;
        g_lc.local_ready = 0;
        g_lc.all_ready = 0;
        g_lc.launch_pending = 0;
        memset(&g_lc.join, 0, sizeof(g_lc.join));
        match_caps_clear(&g_lc.match_caps);
        member_rtt_clear();
        return;
    }
}

static int set_nonblock(int fd)
{
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
#else
    int fl = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
#endif
}

int rnet_lobby_connect(const char *ws_url)
{
    struct addrinfo hints, *res = NULL, *rp;
    char portstr[16];
    int fd = -1;
    char key_raw[16];
    char key_b64[32];
    char req[512];
    int i;

    /* Name the pin we will present. A join refused with version_mismatch tells
     * a player only that the two builds differ; without both values in the log
     * there is nothing to compare, and "dev" against "dev" used to look like
     * agreement even when the builds shared no code. */
    fprintf(stderr, "rnet_lobby: this build presents game_version=\"%s\"\n",
            effective_game_version(NULL));

    rnet_lobby_disconnect();
#if defined(_WIN32)
    {
        static int wsa;
        if (!wsa) {
            WSADATA d;
            WSAStartup(MAKEWORD(2, 2), &d);
            wsa = 1;
        }
    }
#endif
    {
        const char *use = ws_url && ws_url[0] ? ws_url : rnet_lobby_default_url();
        if (parse_ws_url(use, g_lc.host, sizeof(g_lc.host), &g_lc.port, g_lc.path,
                         sizeof(g_lc.path)) != 0) {
            return -1;
        }
        snprintf(g_lc.url, sizeof(g_lc.url), "%s", use);
    }
    snprintf(portstr, sizeof(portstr), "%d", g_lc.port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(g_lc.host, portstr, &hints, &res) != 0) {
        return -2;
    }
    for (rp = res; rp; rp = rp->ai_next) {
        fd = (int)socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, rp->ai_addr, (int)rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        return -3;
    }
    g_lc.fd = fd;
    for (i = 0; i < 16; ++i) {
        key_raw[i] = (char)(rand() & 0xff);
    }
    /* base64 16 bytes -> 24 chars; reuse server-side style via sha1 helper file's b64? */
    {
        static const char *B64 =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int o = 0;
        for (i = 0; i < 16; i += 3) {
            unsigned v = ((unsigned char)key_raw[i] << 16);
            if (i + 1 < 16) {
                v |= ((unsigned char)key_raw[i + 1] << 8);
            }
            if (i + 2 < 16) {
                v |= (unsigned char)key_raw[i + 2];
            }
            key_b64[o++] = B64[(v >> 18) & 63];
            key_b64[o++] = B64[(v >> 12) & 63];
            key_b64[o++] = (i + 1 < 16) ? B64[(v >> 6) & 63] : '=';
            key_b64[o++] = (i + 2 < 16) ? B64[v & 63] : '=';
        }
        key_b64[o] = '\0';
    }
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n\r\n",
             g_lc.path, g_lc.host, g_lc.port, key_b64);
    if (send(fd, req, (int)strlen(req), 0) < 0) {
        close(fd);
        g_lc.fd = -1;
        return -4;
    }
    set_nonblock(fd);
    g_lc.connected = 1;
    g_lc.handshake_done = 0;
    g_lc.rx_http_len = 0;
    return 0;
}

void rnet_lobby_disconnect(void)
{
    host_relay_leave();
    if (g_lc.fd >= 0) {
        close(g_lc.fd);
    }
    rnet_ws_tx_free(&g_lc.tx);   /* the memset below would leak it */
    {
        char dname[RNET_LOBBY_NAME_LEN];
        char fgame[RNET_LOBBY_NAME_LEN];
        char fver[RNET_LOBBY_VERSION_LEN];
        /* The memset below would drop the pointer, not the agent: closing it
         * first is the difference between ending a transfer and leaking a
         * live UDP socket every time the lobby reconnects. */
        if (g_lc.xfer) rnet_ice_xfer_close(&g_lc.xfer);
        rnet_sig_hold_clear(&g_lc.sig_hold);   /* buckets are heap */
        host_ice_leave();                      /* agents + any launch bundle */
        strncpy(dname, g_lc.display_name, sizeof(dname) - 1);
        dname[sizeof(dname) - 1] = '\0';
        /* The game identity is what this BUILD is, not what this connection
         * was: keep it across the reset, exactly as the display name is kept.
         * rnet_lobby_connect() calls this first, so a caller that sets the
         * identity and then connects (cb_connect does) otherwise reached
         * `welcome` with an empty title -- and a client with no title is one
         * the server cannot scope, which came back as `no_game` the moment it
         * tried to use the per-game server chat. */
        strncpy(fgame, g_lc.filter_game_name, sizeof(fgame) - 1);
        fgame[sizeof(fgame) - 1] = '\0';
        strncpy(fver, g_lc.filter_game_version, sizeof(fver) - 1);
        fver[sizeof(fver) - 1] = '\0';
        memset(&g_lc, 0, sizeof(g_lc));
        g_lc.fd = -1;
        strncpy(g_lc.display_name, dname, sizeof(g_lc.display_name) - 1);
        strncpy(g_lc.filter_game_name, fgame, sizeof(g_lc.filter_game_name) - 1);
        if (fver[0]) {
            strncpy(g_lc.filter_game_version, fver,
                    sizeof(g_lc.filter_game_version) - 1);
        } else {
            strncpy(g_lc.filter_game_version, RNET_LOBBY_DEFAULT_VERSION,
                    sizeof(g_lc.filter_game_version) - 1);
            g_lc.filter_game_version[sizeof(g_lc.filter_game_version) - 1] = '\0';
        }
        member_rtt_clear();
    }
}

int rnet_lobby_connected(void)
{
    return g_lc.connected && g_lc.fd >= 0;
}

const char *rnet_lobby_url(void)
{
    if (!rnet_lobby_connected() || !g_lc.url[0])
        return "";
    return g_lc.url;
}

void rnet_lobby_set_display_name(const char *name)
{
    char prev[RNET_LOBBY_NAME_LEN];
    if (!name) {
        return;
    }
    snprintf(prev, sizeof(prev), "%s", g_lc.display_name);
    strncpy(g_lc.display_name, name, sizeof(g_lc.display_name) - 1);
    g_lc.display_name[sizeof(g_lc.display_name) - 1] = '\0';
    /* A rename after connect has to reach the server, or the players-online
     * list and our seat keep the name from the first hello until the next
     * reconnect. `hello` IS the identity message and the server takes it at
     * any time, so re-sending it is the whole rename protocol. */
    if (rnet_lobby_connected() && strcmp(prev, g_lc.display_name) != 0)
        queue_hello();
}

const char *rnet_lobby_display_name(void)
{
    return g_lc.display_name;
}

const char *rnet_lobby_player_id(void)
{
    return g_lc.player_id;
}

void rnet_lobby_pump(void)
{
    char buf[4096];
#if defined(_WIN32)
    int n;
#else
    ssize_t n;
#endif
    /* Host relay: hold / probe / report to match the room. Before the
     * connected() check so a dropped WS releases the port the same pump. */
    host_relay_step();
    host_ice_step();
    /* Driven from the lobby pump so a transfer runs while the player sits in
     * the waiting room -- which is the only time one happens. Deliberately
     * before the connected() check: an agent mid-handshake still has to be
     * pumped so it can fail cleanly rather than hang if the WS drops. */
    rnet_lobby_mod_xfer_pump();
    /* Same reasoning as the transfer above: the probe runs while the player
     * sits in a queue, which is a state with no session and no room, so
     * nothing else would drive it. Before the connected() check so an
     * outstanding probe still times out cleanly if the WS drops. */
    automatch_probe_poll();
    list_probe_pump();
    if (!rnet_lobby_connected()) {
        g_srv_ping_sent_ms = 0;
        g_srv_ping_wanted = 1;   /* time the next connection once */
        g_srv_rtt_reported = -1; /* a new connection is a new record */
        list_probe_reset();
        return;
    }
    {
        uint64_t now = lobby_mono_ms();
        if (g_srv_ping_sent_ms && now - g_srv_ping_sent_ms > SERVER_PING_TIMEOUT_MS)
            g_srv_ping_sent_ms = 0; /* lost: the next list request asks again */
        if (g_srv_ping_wanted && !g_srv_ping_sent_ms) {
            queue_send("{\"op\":\"ping\"}");
            flush_pending();
            g_srv_ping_sent_ms = now;
            g_srv_ping_wanted = 0;
        }
    }
    if (g_lc.tx_failed) {
        rnet_lobby_disconnect();
        return;
    }
    if (!g_lc.handshake_done) {
        n = recv(g_lc.fd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (socket_would_block()) {
                return;
            }
            rnet_lobby_disconnect();
            return;
        }
        if (n == 0) {
            rnet_lobby_disconnect();
            return;
        }
        if (g_lc.rx_http_len + (size_t)n >= sizeof(g_lc.rx_http)) {
            rnet_lobby_disconnect();
            return;
        }
        memcpy(g_lc.rx_http + g_lc.rx_http_len, buf, (size_t)n);
        g_lc.rx_http_len += (size_t)n;
        g_lc.rx_http[g_lc.rx_http_len] = '\0';
        {
            char *hdr_end = strstr(g_lc.rx_http, "\r\n\r\n");
            if (hdr_end) {
                size_t hdr_len;
                size_t leftover;
                if (!strstr(g_lc.rx_http, "101")) {
                    rnet_lobby_disconnect();
                    return;
                }
                hdr_len = (size_t)(hdr_end - g_lc.rx_http) + 4;
                leftover = g_lc.rx_http_len > hdr_len ? g_lc.rx_http_len - hdr_len : 0;
                g_lc.handshake_done = 1;
                g_lc.ws_pending_len = 0;
                if (leftover > 0 && leftover <= sizeof(g_lc.ws_pending)) {
                    memcpy(g_lc.ws_pending, g_lc.rx_http + hdr_len, leftover);
                    g_lc.ws_pending_len = leftover;
                }
                g_lc.rx_http_len = 0;
                flush_pending();
                drain_ws_pending();
            }
        }
        return;
    }
    flush_pending();
    if (g_lc.tx_failed) {
        rnet_lobby_disconnect();
        return;
    }
    drain_ws_pending();
    for (;;) {
        size_t available = sizeof(g_lc.ws_pending) - g_lc.ws_pending_len;
        if (available == 0) {
            rnet_lobby_disconnect();
            return;
        }
        n = recv(g_lc.fd,
                 (char *)g_lc.ws_pending + g_lc.ws_pending_len,
                 (int)available, 0);
        if (n < 0) {
            if (socket_would_block()) break;
            rnet_lobby_disconnect();
            return;
        }
        if (n == 0) {
            rnet_lobby_disconnect();
            return;
        }
        g_lc.ws_pending_len += (size_t)n;
        drain_ws_pending();
        if (!rnet_lobby_connected()) {
            break;
        }
    }
    /* Guests: probe host RTT about once per second while seated. */
    if (g_lc.in_lobby && !g_lc.is_host && !g_lc.launch_pending) {
        uint64_t now = lobby_mono_ms();
        if (now >= g_lc.rtt_next_ping_ms) {
            char ts[32];
            snprintf(ts, sizeof(ts), "%llu", (unsigned long long)now);
            (void)rnet_lobby_send_signal(RNET_LOBBY_SIG_RTT_PING, 0, ts);
            g_lc.rtt_next_ping_ms = now + 1000ull;
        }
    }
}

/*
 * RNET_LOBBY_GAME_VERSION (or <legacy prefix>GAME_VERSION, e.g.
 * SNES_NET_GAME_VERSION) -- pin the announced version for one run.
 *
 * The derived pin deliberately makes two builds match ONLY when they really
 * are the same build: a clean release is its tag, and a development build
 * carries its commit plus a hash of its uncommitted diff. That is what stops
 * two peers whose trees differ from pairing and then desyncing, and it must
 * stay the default for anyone who does not ask otherwise.
 *
 * But it also makes DEVELOPMENT testing awkward. Two machines mid-change are
 * exactly the pair that will not match, and reconfiguring both to agree means
 * a CMake cache edit and a rebuild on each -- for a run whose whole purpose is
 * to test the change that made them differ.
 *
 * So the override is here, deliberately as an environment variable: it is
 * per-run rather than baked into an artifact, it takes an explicit act on both
 * machines, and it announces itself loudly enough that nobody mistakes an
 * overridden build for an honest one. It is a TESTING tool. Two peers who set
 * it to the same string are asserting their builds agree; the runtime cannot
 * check that for them, and a desync under an override is the answer to a
 * question the override asked.
 */
static const char *identity_version_override(void)
{
    const char *e = lobby_env("RNET_LOBBY_GAME_VERSION", "GAME_VERSION");
    return (e && e[0]) ? e : NULL;
}

void rnet_lobby_set_game_identity(const char *game_name,
                                  const char *game_version)
{
    const char *forced = identity_version_override();

    if (game_name) {
        strncpy(g_lc.filter_game_name, game_name,
                sizeof(g_lc.filter_game_name) - 1);
        g_lc.filter_game_name[sizeof(g_lc.filter_game_name) - 1] = '\0';
    } else {
        g_lc.filter_game_name[0] = '\0';
    }
    if (forced) {
        strncpy(g_lc.filter_game_version, forced,
                sizeof(g_lc.filter_game_version) - 1);
        g_lc.filter_game_version[sizeof(g_lc.filter_game_version) - 1] = '\0';
        /* Said on every identity change, not once: the honest pin this is
         * standing in for is the thing a later desync report needs, and a
         * line printed only at startup is the line nobody scrolls back to. */
        fprintf(stderr,
                "rnet_lobby: game_version FORCED to \"%s\" by "
                "RNET_LOBBY_GAME_VERSION%s%s%s (this build is really "
                "\"%s\"). Testing override -- both peers must be the same "
                "build.\n",
                g_lc.filter_game_version,
                g_cfg.legacy_env_prefix[0] ? " / " : "",
                g_cfg.legacy_env_prefix,
                g_cfg.legacy_env_prefix[0] ? "GAME_VERSION" : "",
                (game_version && game_version[0]) ? game_version
                                                  : RNET_LOBBY_DEFAULT_VERSION);
        return;
    }
    if (game_version && game_version[0]) {
        strncpy(g_lc.filter_game_version, game_version,
                sizeof(g_lc.filter_game_version) - 1);
        g_lc.filter_game_version[sizeof(g_lc.filter_game_version) - 1] = '\0';
    } else {
        strncpy(g_lc.filter_game_version, RNET_LOBBY_DEFAULT_VERSION,
                sizeof(g_lc.filter_game_version) - 1);
        g_lc.filter_game_version[sizeof(g_lc.filter_game_version) - 1] = '\0';
    }
}

void rnet_lobby_configure(const RNetLobbyConfig *cfg)
{
    if (!cfg) return;
    snprintf(g_cfg.platform, sizeof(g_cfg.platform), "%s",
             cfg->platform && cfg->platform[0] ? cfg->platform : "unknown");
    g_cfg.max_players = cfg->max_players > 0 ? cfg->max_players : 4;
    if (g_cfg.max_players < 2) g_cfg.max_players = 2;
    if (g_cfg.max_players > RNET_LOBBY_MAX_PLAYERS)
        g_cfg.max_players = RNET_LOBBY_MAX_PLAYERS;
    g_cfg.automatch_slots = cfg->automatch_slots >= 2 ? cfg->automatch_slots : 2;
    snprintf(g_cfg.legacy_env_prefix, sizeof(g_cfg.legacy_env_prefix), "%s",
             cfg->legacy_env_prefix ? cfg->legacy_env_prefix : "");
    /* Identity last: the version override reads the legacy prefix above. */
    if (cfg->game_name || cfg->game_version)
        rnet_lobby_set_game_identity(
            cfg->game_name ? cfg->game_name : g_lc.filter_game_name,
            cfg->game_version);
}

void rnet_lobby_set_caps_codec(const RNetLobbyCapsCodec *codec)
{
    if (codec) g_codec = *codec;
    else memset(&g_codec, 0, sizeof(g_codec));
}

int rnet_lobby_ready(void)
{
    return rnet_lobby_connected() && g_lc.handshake_done && g_lc.player_id[0];
}

const char *rnet_lobby_game_version(void)
{
    return effective_game_version(NULL);
}

int rnet_lobby_version_filter_strict(void)
{
    return list_filter_version_strict();
}

void rnet_lobby_request_list(void)
{
    queue_list_request();
    flush_pending();
    /* An explicit request (first open, Refresh) is the one moment the list's
     * latencies are measured; the server's change pushes are not. */
    g_srv_ping_wanted = 1;
    g_list_probe_armed = 1;
}

int rnet_lobby_list_count(void)
{
    return g_lc.list_count;
}

int rnet_lobby_server_rtt_ms(void)
{
    return rnet_lobby_connected() ? g_srv_rtt_ms : -1;
}

int rnet_lobby_list_latency_estimate_ms(int index)
{
    int host;
    if (index < 0 || index >= g_lc.list_count)
        return -1;
    host = g_list_host_rtt[index];
    if (host < 0 || rnet_lobby_server_rtt_ms() < 0)
        return -1;
    return g_srv_rtt_ms + host;
}

int rnet_lobby_list_latency_ms(int index, int *is_direct)
{
    const ListProbe *lp;
    if (is_direct)
        *is_direct = 0;
    if (index < 0 || index >= g_lc.list_count)
        return -1;
    lp = list_probe_find(g_lc.list[index].lobby_id);
    if (lp && lp->rtt_ms >= 0) {
        if (is_direct)
            *is_direct = 1;
        return lp->rtt_ms;
    }
    return rnet_lobby_list_latency_estimate_ms(index);
}

int rnet_lobby_list_get(int index, RNetLobbyRow *out)
{
    if (!out || index < 0 || index >= g_lc.list_count) {
        return 0;
    }
    *out = g_lc.list[index];
    return 1;
}

/*
 * Serialise caps for an outbound op, or publish none at all.
 *
 * append_match_caps_json returns 0 when the object would not fit (or would
 * exceed the 4096 the lobby server silently discards) -- but snprintf has
 * already left a TRUNCATED, NUL-terminated string in `dst` by then. Every
 * caller ignored that return and pasted the result straight into its message,
 * so an oversized caps object did not drop out, it went in as a fragment cut
 * mid-token. The op was then malformed JSON, the server dropped it, and the
 * caller reported success.
 *
 * That is what made "Create Lobby" close its modal and create nothing. The
 * same two lines existed in set_match_caps and in start, which would have
 * failed the same silent way once the caps grew past their buffers -- so this
 * is one function rather than three fixed copies.
 *
 * Caps are dropped rather than the op refused: the settings matter,
 * but a lobby that exists without published caps is recoverable and a lobby
 * that never exists is not. The log line is the part that must not be
 * missing.
 */
static void caps_json_build(char *dst, size_t cap,
                            const RNetLobbyMatchCaps *caps, const char *op)
{
    dst[0] = '\0';
    if (!caps || !caps->valid) return;
    if (append_match_caps_json(dst, cap, caps) > 0) return;
    dst[0] = '\0';
    fprintf(stderr, "rnet_lobby: match caps would not serialise for `%s` -- "
                    "sending it without them\n", op ? op : "?");
}

int rnet_lobby_create(const char *name, const char *game_name,
                     const char *game_version, const char *password,
                     const char *host_bind, const RNetLobbyMatchCaps *match_caps,
                     int max_slots)
{
    /* Sized against what append_match_caps_json will actually emit -- it caps
     * itself at 4000 bytes -- rather than a round number that a mod plan, a
     * mod_set and a digest-pinned allowlist grew past. */
    char msg[5120];
    char caps_json[4160];
    char name_esc[JSON_ESC_CAP(128)];
    char gn_esc[JSON_ESC_CAP(RNET_LOBBY_NAME_LEN)];
    char gv_esc[JSON_ESC_CAP(RNET_LOBBY_VERSION_LEN)];
    char pw_esc[JSON_ESC_CAP(128)];
    char bind_esc[JSON_ESC_CAP(RNET_LOBBY_ENDPOINT_LEN)];
    char dn_esc[JSON_ESC_CAP(RNET_LOBBY_NAME_LEN)];
    const char *gn;
    const char *gv;
    int n;
    int slots;
    if (!rnet_lobby_connected()) {
        return -1;
    }
    slots = max_slots;
    if (slots < 2) slots = 2;
    /* PLAYERS, not members. The membership table grew to hold the gallery;
     * the number of people who can pick up a controller did not. */
    if (slots > g_cfg.max_players) slots = g_cfg.max_players;
    gn = game_name && game_name[0] ? game_name
         : (g_lc.filter_game_name[0] ? g_lc.filter_game_name : "Game");
    gv = effective_game_version(game_version);
    if (game_name && game_name[0])
        rnet_lobby_set_game_identity(game_name, gv);
    strncpy(g_lc.my_bind, host_bind && host_bind[0] ? host_bind : "0.0.0.0:7777",
            sizeof(g_lc.my_bind) - 1);
    g_lc.join.last_error[0] = '\0';
    if (match_caps && match_caps->valid) g_lc.match_caps = *match_caps;
    caps_json_build(caps_json, sizeof(caps_json), match_caps, "create");
    json_escape(name && name[0] ? name : "Lobby", name_esc, sizeof(name_esc));
    json_escape(gn, gn_esc, sizeof(gn_esc));
    json_escape(gv, gv_esc, sizeof(gv_esc));
    json_escape(password ? password : "", pw_esc, sizeof(pw_esc));
    json_escape(g_lc.my_bind, bind_esc, sizeof(bind_esc));
    json_escape(g_lc.display_name[0] ? g_lc.display_name : "Host",
                dn_esc, sizeof(dn_esc));
    n = snprintf(msg, sizeof(msg),
                 "{\"op\":\"create\",\"name\":\"%s\",\"game_name\":\"%s\",\"game_version\":\"%s\",\"password\":\"%s\","
                 "\"max_slots\":%d,\"allow_spectators\":%s,"
                 "\"host_bind\":\"%s\",\"display_name\":\"%s\"%s}",
                 name_esc, gn_esc, gv_esc, pw_esc, slots,
                 g_allow_spectators_pref ? "true" : "false", bind_esc,
                 dn_esc, caps_json);
    if (n < 0 || (size_t)n >= sizeof(msg)) return -1;
    queue_send(msg);
    flush_pending();
    return 0;
}

/* Prefer caller bind; never advertise :0 — lobby rewrites that to peer_ip:0
 * and rnet_session_start_lan rejects port 0 (host falls offline, guest alone). */
static void rnet_lobby_normalize_guest_bind(const char *guest_bind, char *out,
                                            size_t out_cap)
{
    int port;
    if (!out || out_cap < 8)
        return;
    out[0] = '\0';
    if (guest_bind && guest_bind[0] && endpoint_has_usable_port(guest_bind)) {
        strncpy(out, guest_bind, out_cap - 1);
        out[out_cap - 1] = '\0';
        return;
    }
    port = rnet_udp_find_free_port(/*preferred=*/7778, 32);
    if (port <= 0)
        port = 7778;
    snprintf(out, out_cap, "0.0.0.0:%d", port);
}

int rnet_lobby_join(const char *lobby_id, const char *password, const char *guest_bind)
{
    char msg[RNET_LOBBY_MAX_MODS * 256 + 1792];
    char offer[RNET_LOBBY_MAX_MODS * 256 + 64];
    char lid_esc[JSON_ESC_CAP(RNET_LOBBY_ID_LEN)];
    char pw_esc[JSON_ESC_CAP(128)];
    char bind_esc[JSON_ESC_CAP(RNET_LOBBY_ENDPOINT_LEN)];
    char dn_esc[JSON_ESC_CAP(RNET_LOBBY_NAME_LEN)];
    char gn_esc[JSON_ESC_CAP(RNET_LOBBY_NAME_LEN)];
    char gv_esc[JSON_ESC_CAP(RNET_LOBBY_VERSION_LEN)];
    const char *gn;
    const char *gv;
    int n;
    if (!rnet_lobby_connected() || !lobby_id) {
        return -1;
    }
    gn = g_lc.filter_game_name;
    gv = effective_game_version(NULL);
    rnet_lobby_normalize_guest_bind(guest_bind, g_lc.my_bind, sizeof(g_lc.my_bind));
    g_lc.join.last_error[0] = '\0';
    offer[0] = '\0';
    if (g_mod_offer_fn && !append_mod_offer(offer, sizeof(offer))) {
        /* Refuse the join rather than send a short offer. A truncated offer
         * would have the server turn this peer away over mods it is holding,
         * and the player would be told to install something already present --
         * an error nobody can act on. */
        snprintf(g_lc.join.last_error, sizeof(g_lc.join.last_error),
                 "mod_offer_too_large");
        return -1;
    }
    json_escape(lobby_id, lid_esc, sizeof(lid_esc));
    json_escape(password ? password : "", pw_esc, sizeof(pw_esc));
    json_escape(g_lc.my_bind, bind_esc, sizeof(bind_esc));
    json_escape(g_lc.display_name[0] ? g_lc.display_name : "Guest",
                dn_esc, sizeof(dn_esc));
    json_escape(gn, gn_esc, sizeof(gn_esc));
    json_escape(gv, gv_esc, sizeof(gv_esc));
    n = snprintf(msg, sizeof(msg),
             "{\"op\":\"join\",\"lobby_id\":\"%s\",\"password\":\"%s\",\"guest_bind\":\"%s\","
             "\"display_name\":\"%s\",\"game_name\":\"%s\",\"game_version\":\"%s\"%s}",
             lid_esc, pw_esc, bind_esc, dn_esc, gn_esc, gv_esc, offer);
    /* A truncated frame is a frame the server drops whole, so the join would
     * silently never happen. create checked this; join did not. */
    if (n < 0 || (size_t)n >= sizeof(msg)) {
        snprintf(g_lc.join.last_error, sizeof(g_lc.join.last_error),
                 "join_too_large");
        return -1;
    }
    queue_send(msg);
    flush_pending();
    return 0;
}

int rnet_lobby_leave(void)
{
    host_relay_leave(); /* unmap the router, close the port, forget reports */
    ice_launch_discard();
    g_il.error[0] = '\0';
    queue_send("{\"op\":\"leave\"}");
    flush_pending();
    g_lc.in_lobby = 0;
    g_am.in_automatch_room = 0;
    g_lc.is_host = 0;
    g_lc.host_player_id[0] = '\0';
    g_lc.member_count = 0;
    g_lc.local_ready = 0;
    g_lc.all_ready = 0;
    g_lc.launch_pending = 0;
    match_caps_clear(&g_lc.match_caps);
    return 0;
}

int rnet_lobby_seat_valid(int slot);

int rnet_lobby_kick(int slot)
{
    char msg[64];
    if (!rnet_lobby_connected() || !g_lc.in_lobby || !g_lc.is_host)
        return -1;
    if (!rnet_lobby_seat_valid(slot))
        return -1;
    snprintf(msg, sizeof(msg), "{\"op\":\"kick\",\"slot\":%d}", slot);
    queue_send(msg);
    flush_pending();
    return 0;
}

/* A lobby seat index the server would accept: a player seat, or a gallery
 * seat at or above the base the server published (never valid before it
 * did -- a server that predates spectators has no gallery to move into). */
int rnet_lobby_seat_valid(int slot)
{
    const int base = rnet_lobby_spectator_slot_base();
    if (slot < 0) return 0;
    if (slot < RNET_LOBBY_MAX_MEMBERS) return 1;
    if (base <= 0) return 0;
#ifdef RNET_LOBBY_MAX_SPECTATORS
    return slot >= base && slot < base + RNET_LOBBY_MAX_SPECTATORS;
#else
    return slot >= base && slot < base + RNET_LOBBY_MAX_MEMBERS;
#endif
}

int rnet_lobby_move(int from_slot, int to_slot)
{
    char msg[96];
    if (!rnet_lobby_connected() || !g_lc.in_lobby || !g_lc.is_host)
        return -1;
    /* Either seat may be in the gallery: this is the call that promotes and
     * demotes as well as reorders. Gallery seats live at the server's
     * spectator base, far above the player table. */
    if (!rnet_lobby_seat_valid(from_slot) || !rnet_lobby_seat_valid(to_slot) ||
        from_slot == to_slot)
        return -1;
    snprintf(msg, sizeof(msg),
             "{\"op\":\"move\",\"from_slot\":%d,\"to_slot\":%d}",
             from_slot, to_slot);
    queue_send(msg);
    flush_pending();
    return 0;
}

int rnet_lobby_in_lobby(void)
{
    return g_lc.in_lobby;
}

int rnet_lobby_is_host(void)
{
    return g_lc.is_host;
}

const char *rnet_lobby_host_player_id(void)
{
    return g_lc.host_player_id;
}

const RNetLobbyJoinInfo *rnet_lobby_join_info(void)
{
    return &g_lc.join;
}

int rnet_lobby_need_mods_count(void)
{
    return g_lc.need_mods_count;
}

const RNetLobbyModPkg *rnet_lobby_need_mods_get(int index)
{
    if (index < 0 || index >= g_lc.need_mods_count)
        return NULL;
    return &g_lc.need_mods[index];
}

int rnet_lobby_need_mods_can_transfer(void)
{
    return g_lc.need_mods_can_transfer;
}

const RNetLobbyMatchCaps *rnet_lobby_match_caps(void)
{
    return &g_lc.match_caps;
}

int rnet_lobby_set_match_caps(const RNetLobbyMatchCaps *caps)
{
    char msg[4352];
    char caps_json[4160];
    int n;
    if (!rnet_lobby_connected() || !g_lc.in_lobby || !g_lc.is_host || !caps || !caps->valid)
        return -1;
    g_lc.match_caps = *caps;
    caps_json_build(caps_json, sizeof(caps_json), caps, "set_match_caps");
    n = snprintf(msg, sizeof(msg), "{\"op\":\"set_match_caps\"%s}", caps_json);
    if (n < 0 || (size_t)n >= sizeof(msg)) return -1;
    queue_send(msg);
    flush_pending();
    return 0;
}

int rnet_lobby_seat_move_self(int to_slot)
{
    char msg[80];
    if (!rnet_lobby_connected() || !g_lc.in_lobby) return -1;
    /* A player seat or a gallery seat: the server takes either, empty only. */
    if (!rnet_lobby_seat_valid(to_slot)) return -1;
    snprintf(msg, sizeof(msg), "{\"op\":\"seat_move\",\"to_slot\":%d}", to_slot);
    queue_send(msg);
    flush_pending();
    return 0;
}

int rnet_lobby_seat_swap_request(int target_slot)
{
    char msg[96];
    if (!rnet_lobby_connected() || !g_lc.in_lobby) return -1;
    /* A player seat or a gallery seat: the occupant of either can be asked. */
    if (!rnet_lobby_seat_valid(target_slot)) return -1;
    if (g_lc.swap_out == 1) return -1; /* one ask at a time */
    snprintf(msg, sizeof(msg),
             "{\"op\":\"seat_swap_request\",\"target_slot\":%d}", target_slot);
    queue_send(msg);
    flush_pending();
    g_lc.swap_out = 1;
    return 0;
}

int rnet_lobby_seat_swap_incoming(char *who, size_t who_cap, int *from_slot)
{
    if (!g_lc.swap_in_valid) return 0;
    if (who && who_cap) snprintf(who, who_cap, "%s", g_lc.swap_in_asker_name);
    if (from_slot) *from_slot = g_lc.swap_in_from_slot;
    return 1;
}

int rnet_lobby_seat_swap_respond(int accept)
{
    char msg[160];
    if (!g_lc.swap_in_valid) return -1;
    g_lc.swap_in_valid = 0;
    if (!rnet_lobby_connected() || !g_lc.in_lobby) return -1;
    {
        char asker_esc[JSON_ESC_CAP(RNET_LOBBY_ID_LEN)];
        json_escape(g_lc.swap_in_asker_id, asker_esc, sizeof(asker_esc));
        snprintf(msg, sizeof(msg),
                 "{\"op\":\"seat_swap_answer\",\"accept\":%s,"
                 "\"asker_player_id\":\"%s\"}",
                 accept ? "true" : "false", asker_esc);
    }
    queue_send(msg);
    flush_pending();
    return 0;
}

int rnet_lobby_seat_swap_outgoing(void)
{
    return g_lc.swap_out;
}

void rnet_lobby_seat_swap_clear(void)
{
    if (g_lc.swap_out != 1) g_lc.swap_out = 0;
}

int rnet_lobby_report_chat(const char *const *mids, int mid_count,
                           const char *reason, const char *note)
{
    /* Thin on purpose. What a report CONTAINS is console-agnostic and lives in
     * recomp-net (recomp_net/chat_report.h); this function's whole job is to
     * say where this particular client is and hand the frame to the socket.
     * The PSX lobby client's copy of this is the same eight lines. */
    RNetChatReportMeta meta;
    char msg[2048];
    size_t n;

    if (!rnet_lobby_connected())
        return -1;

    memset(&meta, 0, sizeof(meta));
    meta.game = g_lc.filter_game_name;
    meta.game_version = effective_game_version(NULL);
    /* Metadata only. Nothing downstream may name a file or a directory after
     * it -- see the header: one queue, read by one person, and splitting the
     * evidence by console fragments a moderation record along a line that has
     * nothing to do with moderation. */
    meta.platform = g_cfg.platform;
    meta.server = rnet_lobby_url();
    meta.lobby = g_lc.join.lobby_id[0] ? g_lc.join.lobby_id : "";
    meta.scope = g_lc.in_lobby ? "lobby" : "server";

    n = rnet_chat_report_build(msg, sizeof(msg), mids, mid_count,
                               reason, note, &meta);
    if (n == 0)
        return -1;
    queue_send(msg);
    flush_pending();
    return 0;
}

int rnet_lobby_send_chat(const char *text)
{
    char esc[RNET_LOBBY_CHAT_TEXT_LEN * 2 + 8];
    char msg[RNET_LOBBY_CHAT_TEXT_LEN * 2 + 64];
    int n;
    if (!rnet_lobby_connected() || !g_lc.in_lobby) return -1;
    if (!text || !text[0]) return -1;
    json_escape(text, esc, sizeof(esc));
    n = snprintf(msg, sizeof(msg), "{\"op\":\"chat\",\"text\":\"%s\"}", esc);
    if (n < 0 || (size_t)n >= sizeof(msg)) return -1;
    queue_send(msg);
    flush_pending();
    return 0;
}

int rnet_lobby_chat_count(void)
{
    return g_lc.chat_count;
}

int rnet_lobby_send_server_chat(const char *text)
{
    char esc[RNET_LOBBY_CHAT_TEXT_LEN * 2 + 8];
    char game_esc[JSON_ESC_CAP(RNET_LOBBY_NAME_LEN)];
    char msg[RNET_LOBBY_CHAT_TEXT_LEN * 2 + 256];
    int n;
    if (!rnet_lobby_connected()) return -1;
    if (!text || !text[0]) return -1;
    json_escape(text, esc, sizeof(esc));
    /* Carry the title on the line itself: the server scopes by it, and this
     * works even against a server that has not seen our `list` yet. */
    json_escape(g_lc.filter_game_name, game_esc, sizeof(game_esc));
    n = snprintf(msg, sizeof(msg),
                 "{\"op\":\"server_chat\",\"game_name\":\"%s\",\"text\":\"%s\"}",
                 game_esc, esc);
    if (n < 0 || (size_t)n >= sizeof(msg)) return -1;
    queue_send(msg);
    flush_pending();
    return 0;
}

int rnet_lobby_server_chat_count(void)
{
    return g_lc.schat_count;
}

int rnet_lobby_server_chat_get(int index, RNetLobbyChatMsg *out)
{
    if (!out || index < 0 || index >= g_lc.schat_count) return 0;
    *out = g_lc.schat[(g_lc.schat_head + index) % RNET_LOBBY_CHAT_RING];
    return 1;
}

int rnet_lobby_chat_get(int index, RNetLobbyChatMsg *out)
{
    if (!out || index < 0 || index >= g_lc.chat_count) return 0;
    *out = g_lc.chat[(g_lc.chat_head + index) % RNET_LOBBY_CHAT_RING];
    return 1;
}

void rnet_lobby_set_allow_spectators(int allow)
{
    g_allow_spectators_pref = allow ? 1 : 0;
}

int rnet_lobby_allow_spectators_pref(void)
{
    return g_allow_spectators_pref;
}

int rnet_lobby_allow_spectators(void)
{
    return g_lc.join.allow_spectators ? 1 : 0;
}

int rnet_lobby_max_spectators(void)
{
    return g_lc.join.max_spectators;
}

int rnet_lobby_spectator_count(void)
{
    return g_lc.join.spectator_count;
}

int rnet_lobby_local_is_spectator(void)
{
    return g_lc.join.local_is_spectator ? 1 : 0;
}

int rnet_lobby_spectator_slot_base(void)
{
    /* The server republishes its base in every update, and the namespace is
     * the server's to define. The compiled-in value is only a fallback for an
     * update that arrives without it, so `move` stays addressable. */
    return g_lc.join.spectator_slot_base > 0 ? g_lc.join.spectator_slot_base
                                             : RNET_LOBBY_SPECTATOR_SLOT_BASE;
}

int rnet_lobby_spectator_slot(int index)
{
    if (index < 0 || index >= RNET_LOBBY_MAX_SPECTATORS) return -1;
    return rnet_lobby_spectator_slot_base() + index;
}

int rnet_lobby_local_wire_slot(void)
{
    int gallery_index;
    if (!g_lc.join.local_is_spectator) return -1;
    if (g_lc.join.spectator_relay_base <= 0) return -1;
    gallery_index = g_lc.join.local_slot - rnet_lobby_spectator_slot_base();
    if (gallery_index < 0 || gallery_index >= RNET_LOBBY_MAX_SPECTATORS)
        return -1;
    return g_lc.join.spectator_relay_base + gallery_index;
}

int rnet_lobby_member_count(void)
{
    return g_lc.member_count;
}

int rnet_lobby_member_get(int index, RNetLobbyMember *out)
{
    if (!out || index < 0 || index >= g_lc.member_count) {
        return 0;
    }
    *out = g_lc.members[index];
    return 1;
}

int rnet_lobby_member_latency_ms(int slot)
{
    const int idx = rtt_index_for_slot(slot);
    const int me = local_member_slot();
    int host_row = 0;
    if (idx < 0 || slot == me)
        return -1; /* our own row: no latency to ourselves */
    if (g_lc.host_player_id[0]) {
        int i;
        for (i = 0; i < g_lc.member_count; ++i) {
            if (g_lc.members[i].slot == slot &&
                strcmp(g_lc.members[i].player_id, g_lc.host_player_id) == 0)
                host_row = 1;
        }
    }
    /* The direct link's round trip to that peer when the waiting room has
     * one; the server-relayed ping only stands in until then. */
    {
        const int ice_ms = rnet_host_ice_peer_rtt_ms(g_host_ice, slot);
        if (ice_ms >= 0)
            return ice_ms;
    }
    /* A guest's row for the host used to read nothing at all: the only
     * figure it had (its own ping to the host, through the server) was kept
     * on the guest's own row. Show it where it belongs. */
    if (host_row) {
        const int mine = me >= 0 ? rtt_index_for_slot(me) : -1;
        return mine >= 0 ? g_lc.member_rtt_ms[mine] : -1;
    }
    return g_lc.member_rtt_ms[idx];
}

int rnet_lobby_member_is_host(const RNetLobbyMember *member)
{
    const char *host_id;
    if (!member || !member->player_id[0])
        return 0;
    host_id = rnet_lobby_host_player_id();
    return host_id && host_id[0] && strcmp(member->player_id, host_id) == 0;
}

int rnet_lobby_local_ready(void)
{
    return g_lc.local_ready;
}

int rnet_lobby_all_ready(void)
{
    return g_lc.all_ready != 0 && g_lc.in_lobby && g_lc.join.player_count >= 2;
}

/* Every set_ready carries this peer's installed set.
 *
 * The server stores it against our seat and echoes it to the whole room, which
 * is how the HOST learns what each peer actually has -- the input to the
 * launch gate. It rides on set_ready rather than only on join because the set
 * can change while sitting in the lobby (a player installs the missing mod, or
 * a transfer completes), and a stale offer would keep the match locked after
 * the reason to lock it is gone. */
static void send_set_ready(int ready)
{
    char msg[RNET_LOBBY_MAX_MODS * 256 + 256];
    char offer[RNET_LOBBY_MAX_MODS * 256 + 64];
    int n;

    /* Every caller, the auto-arms included: a peer with nothing to boot is
     * not ready, whoever asks. */
    if (g_launch_blocked) ready = 0;
    offer[0] = '\0';
    if (g_mod_offer_fn && !append_mod_offer(offer, sizeof(offer))) {
        /* Ready without the offer rather than not ready at all: the host's
         * gate then sees "this peer claims nothing" and holds the match, which
         * is the safe direction, and the log says why. */
        offer[0] = '\0';
        fprintf(stderr, "rnet_lobby: could not announce the installed mod set "
                        "(too large); the host will see this peer as having "
                        "none\n");
    }
    n = snprintf(msg, sizeof(msg), "{\"op\":\"set_ready\",\"ready\":%s%s}",
                 ready ? "true" : "false", offer);
    if (n < 0 || (size_t)n >= sizeof(msg)) {
        queue_send(ready ? "{\"op\":\"set_ready\",\"ready\":true}"
                         : "{\"op\":\"set_ready\",\"ready\":false}");
        return;
    }
    queue_send(msg);
}

int rnet_lobby_set_ready(int ready)
{
    if (!rnet_lobby_connected() || !g_lc.in_lobby) {
        return -1;
    }
    send_set_ready(ready);
    flush_pending();
    return 0;
}

void rnet_lobby_set_launch_blocked(int blocked)
{
    blocked = blocked ? 1 : 0;
    if (blocked == g_launch_blocked) return;
    g_launch_blocked = blocked;
    fprintf(stderr, "rnet_lobby: %s\n",
            blocked ? "no bootable game image; announcing not ready"
                    : "game image ready; announcing ready");
    /* A spectator's ready is never read back (see lobby_update). */
    if (!rnet_lobby_connected() || !g_lc.in_lobby ||
        g_lc.join.local_is_spectator)
        return;
    send_set_ready(!blocked);
    flush_pending();
}

int rnet_lobby_match_blocked_by_unready(char *who, size_t who_cap)
{
    int n;
    int blocked = 0;

    if (who && who_cap) who[0] = '\0';
    if (!g_lc.in_lobby)
        return 0;
    for (n = 0; n < g_lc.member_count; ++n) {
        if (!strcmp(g_lc.members[n].player_id, g_lc.player_id))
            continue;              /* the host checks its own image locally */
        if (g_lc.members[n].is_spectator || g_lc.members[n].ready)
            continue;
        ++blocked;
        if (who && who_cap && !who[0])
            snprintf(who, who_cap, "%s", g_lc.members[n].display_name);
    }
    return blocked;
}

/*
 * Report a simulation-state fork to the server.
 *
 * WHAT THIS IS FOR, and what it is NOT.
 *
 * In rollback both peers digest the same simulation every tick, so any state
 * divergence is visible by construction -- the entire class of cheats that
 * alters the simulation cannot hide from it. Until now that signal was
 * computed, named down to the subsystem, printed to a local log, and thrown
 * away. Reporting it is what turns a detection nobody sees into evidence
 * somebody can act on.
 *
 * A fork is NOT an accusation and this call must never be written as though
 * it were. It says two peers disagreed. Version skew says that. A genuine
 * emulation bug says that -- this project has found several, and each one
 * would have produced these reports from two entirely honest players. Which
 * side MOVED is unknowable from either end of a two-peer disagreement, and is
 * only ever answerable by looking at many matches against many opponents,
 * which is a thing a server with accounts can do and a client cannot. So both
 * digests go on the wire, both peers report independently, and the server
 * stores rather than judges.
 *
 * Best-effort: a report that cannot be sent is dropped rather than retried or
 * queued. A desync usually ends the match, the connection often goes with it,
 * and making a diagnostic hold a teardown open would be a worse bug than the
 * missing row.
 */
int rnet_lobby_report_desync(const RNetLobbyDesyncReport *r)
{
    char msg[1024];
    char part_esc[96];
    char exempt_esc[512];
    char gv_esc[RNET_LOBBY_VERSION_LEN * 2 + 4];
    const char *lid;
    int n;

    if (!r || !rnet_lobby_connected())
        return -1;
    lid = g_lc.join.lobby_id[0] ? g_lc.join.lobby_id : "";

    json_escape(r->partition ? r->partition : "?", part_esc, sizeof(part_esc));
    json_escape(r->mod_exempt ? r->mod_exempt : "", exempt_esc,
                sizeof(exempt_esc));
    json_escape(effective_game_version(NULL), gv_esc, sizeof(gv_esc));

    /* Digests as hex STRINGS, not numbers. They are 32-bit and JSON numbers
     * are doubles in most readers; a value above 2^53 would be safe but a
     * reader that decides to print one as 4.29497e+09 has silently destroyed
     * the only field the row exists to compare. */
    n = snprintf(msg, sizeof(msg),
                 "{\"op\":\"desync_report\",\"v\":1,"
                 "\"lobby_id\":\"%s\",\"game_version\":\"%s\","
                 "\"tick\":%u,\"partition\":\"%s\","
                 "\"mine\":\"%08x\",\"theirs\":\"%08x\","
                 "\"role\":\"%s\",\"disc_fp\":\"%s\","
                 "\"mod_exempt\":\"%s\"}",
                 lid, gv_esc, (unsigned)r->tick, part_esc,
                 (unsigned)r->mine, (unsigned)r->theirs,
                 r->is_host ? "host" : "guest",
                 rnet_lobby_disc_fp() ? rnet_lobby_disc_fp() : "",
                 exempt_esc);
    if (n < 0 || (size_t)n >= sizeof(msg))
        return -1;
    queue_send(msg);
    flush_pending();
    return 0;
}

/* Does the peer in `slot` hold every package the host's plan names?
 *
 * A peer that has announced nothing counts as missing everything. That is the
 * safe reading and NOT a guess: a peer running this build always announces,
 * so silence means either an older build or a set too large to state, and in
 * both cases we do not know that it can play. */
static int member_missing_count(int slot)
{
    const RNetLobbyMatchCaps *caps = &g_lc.match_caps;
    int missing = 0;
    int i;
    int j;

    if (slot < 0 || slot >= RNET_LOBBY_MAX_MEMBERS)
        return 0;
    for (i = 0; i < caps->mod_count; ++i) {
        int have = 0;
        for (j = 0; j < g_lc.member_offer_count[slot]; ++j) {
            /* Matched on id ALONE, deliberately.
             *
             * The lobby's job is "does this player have the package at all",
             * because that is the question a download answers. Whether the
             * two builds resolve to the SAME simulation is settled later and
             * far more precisely: the engine's rollback session (snesrecomp's
             * snes_netplay_rb) exchanges the whole effective set at session
             * start -- per feature, with versions and resolved option values
             * -- and refuses on any difference, with its own verdict for
             * "right mod, wrong version". Repeating a coarser version check here only adds a
             * second, earlier, less informative way to say no. */
            if (!strcmp(g_lc.member_offer[slot][j].id, caps->mods[i].id)) {
                have = 1;
                break;
            }
        }
        if (!have)
            missing++;
    }
    return missing;
}

int rnet_lobby_match_blocked_by_mods(char *who, size_t who_cap,
                                     char *what, size_t what_cap)
{
    int n;
    int blocked = 0;

    if (who && who_cap) who[0] = '\0';
    if (what && what_cap) what[0] = '\0';
    if (!g_lc.in_lobby || g_lc.match_caps.mod_count <= 0)
        return 0;
    for (n = 0; n < g_lc.member_count; ++n) {
        int missing;
        if (!strcmp(g_lc.members[n].player_id, g_lc.player_id))
            continue;              /* the host runs the plan by definition */
        missing = member_missing_count(n);
        if (missing <= 0)
            continue;
        blocked += missing;
        if (who && who_cap && !who[0])
            snprintf(who, who_cap, "%s", g_lc.members[n].display_name);
        if (what && what_cap && !what[0]) {
            const RNetLobbyMatchCaps *caps = &g_lc.match_caps;
            int i;
            int j;
            for (i = 0; i < caps->mod_count; ++i) {
                int have = 0;
                for (j = 0; j < g_lc.member_offer_count[n]; ++j)
                    if (!strcmp(g_lc.member_offer[n][j].id, caps->mods[i].id)) {
                        have = 1;
                        break;
                    }
                if (!have) {
                    snprintf(what, what_cap, "%s@%s", caps->mods[i].id,
                             caps->mods[i].ver);
                    break;
                }
            }
        }
    }
    return blocked;
}

int rnet_lobby_local_missing_mods(void)
{
    int n;
    for (n = 0; n < g_lc.member_count; ++n)
        if (!strcmp(g_lc.members[n].player_id, g_lc.player_id))
            return member_missing_count(n);
    return 0;
}

/* Is `item` one of the entries of the comma-separated `list`? */
static int csv_has(const char *list, const char *item, size_t item_len)
{
    const char *p = list;
    if (!list || !item || !item_len) return 0;
    while (*p) {
        const char *end = strchr(p, ',');
        const size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == item_len && !strncmp(p, item, len)) return 1;
        if (!end) break;
        p = end + 1;
    }
    return 0;
}

/* Plan features the member at `n` holds the package for but has not provided
 * the required files for, by its own offer. Appends "package/feature" pairs,
 * ';'-separated, to `what` when given. A package the peer lacks entirely is
 * counted by member_missing_count instead -- it cannot have files for code it
 * does not have, and the remedy (download it) is a different one. */
static int member_files_missing_count(int n, char *what, size_t what_cap)
{
    const RNetLobbyMatchCaps *caps = &g_lc.match_caps;
    int missing = 0;
    int i;
    int j;

    if (what && what_cap) what[0] = '\0';
    if (n < 0 || n >= RNET_LOBBY_MAX_MEMBERS)
        return 0;
    for (i = 0; i < caps->mod_count; ++i) {
        const RNetLobbyModPkg *offer = NULL;
        const char *p;
        for (j = 0; j < g_lc.member_offer_count[n]; ++j)
            if (!strcmp(g_lc.member_offer[n][j].id, caps->mods[i].id)) {
                offer = &g_lc.member_offer[n][j];
                break;
            }
        if (!offer || !offer->nf[0])
            continue;
        for (p = caps->mods[i].feats; *p;) {
            const char *end = strchr(p, ',');
            const size_t len = end ? (size_t)(end - p) : strlen(p);
            if (csv_has(offer->nf, p, len)) {
                missing++;
                if (what && what_cap) {
                    const size_t used = strlen(what);
                    snprintf(what + used, what_cap - used, "%s%s/%.*s",
                             used ? ";" : "", caps->mods[i].id, (int)len, p);
                }
            }
            if (!end) break;
            p = end + 1;
        }
    }
    return missing;
}

int rnet_lobby_match_blocked_by_files(char *who, size_t who_cap,
                                      char *what, size_t what_cap)
{
    int n;
    int blocked = 0;

    if (who && who_cap) who[0] = '\0';
    if (what && what_cap) what[0] = '\0';
    if (!g_lc.in_lobby || g_lc.match_caps.mod_count <= 0)
        return 0;
    for (n = 0; n < g_lc.member_count; ++n) {
        char pairs[RNET_LOBBY_MOD_ID_LEN + RNET_LOBBY_MOD_FEATS_LEN + 2];
        int missing;
        if (!strcmp(g_lc.members[n].player_id, g_lc.player_id))
            continue;              /* the host checks its own files locally */
        if (g_lc.members[n].is_spectator)
            continue;
        missing = member_files_missing_count(n, pairs, sizeof(pairs));
        if (missing <= 0)
            continue;
        blocked += missing;
        if (who && who_cap && !who[0])
            snprintf(who, who_cap, "%s", g_lc.members[n].display_name);
        if (what && what_cap && !what[0]) {
            const char *semi = strchr(pairs, ';');
            snprintf(what, what_cap, "%.*s",
                     semi ? (int)(semi - pairs) : (int)strlen(pairs), pairs);
        }
    }
    return blocked;
}

int rnet_lobby_member_mod_readiness(int index, int *mods_missing,
                                    int *files_missing, char *files,
                                    size_t files_cap)
{
    if (mods_missing) *mods_missing = 0;
    if (files_missing) *files_missing = 0;
    if (files && files_cap) files[0] = '\0';
    if (!g_lc.in_lobby || index < 0 || index >= g_lc.member_count ||
        g_lc.match_caps.mod_count <= 0)
        return 0;
    if (mods_missing) *mods_missing = member_missing_count(index);
    if (files_missing)
        *files_missing = member_files_missing_count(index, files, files_cap);
    else
        (void)member_files_missing_count(index, files, files_cap);
    return 1;
}

int rnet_lobby_request_start(const RNetLobbyMatchCaps *match_caps)
{
    char msg[RNET_LOBBY_MAX_MODS * 256 + 1024];
    char caps_json[RNET_LOBBY_MAX_MODS * 256 + 512];
    char who[RNET_LOBBY_NAME_LEN];
    char what[RNET_LOBBY_MOD_ID_LEN + RNET_LOBBY_MOD_FEATS_LEN + 2];
    int n;
    if (!rnet_lobby_connected() || !g_lc.in_lobby || !g_lc.is_host) {
        return -1;
    }
    /* THE gate. Not the door: a peer without the mods is welcome in the room,
     * and is expected to be here -- this is where they see what is missing and
     * pull it from the host. What cannot happen is the match starting while
     * two peers would patch guest memory differently, because that is a desync
     * dressed up as a match. Checked against every seated peer at the moment
     * of starting, which is the only moment the answer is current. */
    if (rnet_lobby_match_blocked_by_mods(who, sizeof(who), what,
                                         sizeof(what)) > 0) {
        snprintf(g_lc.join.last_error, sizeof(g_lc.join.last_error),
                 "peer_needs_mods");
        fprintf(stderr,
                "rnet_lobby: not starting -- %s does not have %s (and possibly "
                "more). They can download it from you in the lobby.\n",
                who[0] ? who : "a player", what[0] ? what : "a required mod");
        return -1;
    }
    /* Having the package is not enough when one of its enabled features runs
     * on an owner-supplied file (another game's ROM, say): the peer's commit
     * would refuse the plan at launch. Hold here instead, where the host can
     * see who is still choosing a file. */
    if (rnet_lobby_match_blocked_by_files(who, sizeof(who), what,
                                          sizeof(what)) > 0) {
        snprintf(g_lc.join.last_error, sizeof(g_lc.join.last_error),
                 "peer_needs_files");
        fprintf(stderr,
                "rnet_lobby: not starting -- %s has not selected the files %s "
                "needs (and possibly more).\n",
                who[0] ? who : "a player", what[0] ? what : "a required mod");
        return -1;
    }
    /* Nothing above helps a peer that has no game to boot: it would be
     * launched into a match it never joins. It says so by staying not ready
     * (rnet_lobby_set_launch_blocked). */
    if (g_launch_blocked) {
        snprintf(g_lc.join.last_error, sizeof(g_lc.join.last_error),
                 "local_not_ready");
        fprintf(stderr, "rnet_lobby: not starting -- this peer has no "
                        "bootable game image.\n");
        return -1;
    }
    if (rnet_lobby_match_blocked_by_unready(who, sizeof(who)) > 0) {
        snprintf(g_lc.join.last_error, sizeof(g_lc.join.last_error),
                 "peer_not_ready");
        fprintf(stderr, "rnet_lobby: not starting -- %s is not ready (no "
                        "game image selected, or still settling).\n",
                who[0] ? who : "a player");
        return -1;
    }
    if (match_caps && match_caps->valid) g_lc.match_caps = *match_caps;
    caps_json_build(caps_json, sizeof(caps_json), match_caps, "start");
    n = snprintf(msg, sizeof(msg), "{\"op\":\"start\"%s}", caps_json);
    if (n < 0 || (size_t)n >= sizeof(msg)) return -1;
    queue_send(msg);
    flush_pending();
    return 0;
}

int rnet_lobby_launch_pending(void)
{
    return g_lc.launch_pending;
}

void rnet_lobby_clear_launch_pending(void)
{
    g_lc.launch_pending = 0;
}

void rnet_lobby_clear_last_error(void)
{
    g_lc.join.last_error[0] = '\0';
}

/* ---- host relay over ICE: the launch handover ---------------------------
 * Built from the agents the waiting room already connected. A seat that is
 * not COMPLETED is a refused launch with a reason, never a smaller room. */
static void ice_launch_capture(void)
{
    int i;
    ice_launch_discard();
    g_il.error[0] = '\0';
    g_il.is_host = g_lc.is_host ? 1 : 0;
    g_il.born_ms = lobby_mono_ms();
    if (!g_host_ice) {
        snprintf(g_il.error, sizeof(g_il.error),
                 "no ICE agents exist (this build cannot run ICE, or the room was "
                 "not seated)");
        return;
    }
    if (g_il.is_host) {
        for (i = 0; i < g_lc.member_count; ++i) {
            const RNetLobbyMember *m = &g_lc.members[i];
            RNetIceAgent *a;
            if (m->is_spectator || !m->player_id[0] ||
                strcmp(m->player_id, g_lc.player_id) == 0)
                continue;
            if (g_il.n >= RNET_HOST_ICE_MAX_PEERS) {
                snprintf(g_il.error, sizeof(g_il.error), "too many guests for the ICE hub");
                break;
            }
            a = rnet_host_ice_take_completed(g_host_ice, m->player_id);
            if (!a) {
                snprintf(g_il.error, sizeof(g_il.error),
                         "\"%s\" (seat %d) is not connected over ICE",
                         m->display_name[0] ? m->display_name : m->player_id, m->slot);
                break;
            }
            g_il.seat[g_il.n].lobby_slot = m->slot;
            snprintf(g_il.seat[g_il.n].player_id, sizeof(g_il.seat[g_il.n].player_id),
                     "%s", m->player_id);
            g_il.seat[g_il.n].agent = a;
            g_il.n++;
        }
        if (!g_il.error[0] && g_il.n == 0)
            snprintf(g_il.error, sizeof(g_il.error), "no guest is seated");
    } else {
        g_il.guest = rnet_host_ice_take_completed(g_host_ice, g_lc.host_player_id);
        if (!g_il.guest)
            snprintf(g_il.error, sizeof(g_il.error),
                     "this client is not connected to the host over ICE");
    }
    if (g_il.error[0]) {
        char why[sizeof(g_il.error)];
        snprintf(why, sizeof(why), "%s", g_il.error);
        ice_launch_discard();
        snprintf(g_il.error, sizeof(g_il.error), "%s", why);
        return;
    }
    g_il.valid = 1;
}

int rnet_lobby_ice_take_hub(RNetLobbyIceSeat *out, int max)
{
    int n;
    if (!out || max < 1) return -1;
    if (!g_il.valid || !g_il.is_host) {
        if (!g_il.error[0])
            snprintf(g_il.error, sizeof(g_il.error), "no ICE hub handover is pending");
        return -1;
    }
    if (max < g_il.n) {
        snprintf(g_il.error, sizeof(g_il.error), "output array too small for %d seats", g_il.n);
        return -1;
    }
    n = g_il.n;
    memcpy(out, g_il.seat, sizeof(out[0]) * (size_t)n);
    memset(&g_il, 0, offsetof(IceLaunch, error));   /* ownership is the caller's */
    return n;
}

RNetIceAgent *rnet_lobby_ice_take_guest_agent(void)
{
    RNetIceAgent *a;
    if (!g_il.valid || g_il.is_host || !g_il.guest) {
        if (!g_il.error[0])
            snprintf(g_il.error, sizeof(g_il.error), "no ICE guest handover is pending");
        return NULL;
    }
    a = g_il.guest;
    memset(&g_il, 0, offsetof(IceLaunch, error));
    return a;
}

const char *rnet_lobby_ice_launch_error(void) { return g_il.error; }

void rnet_lobby_ice_discard(void) { ice_launch_discard(); }

int rnet_lobby_try_fill_launch(RNetLobbyJoinInfo *out)
{
    const RNetLobbyJoinInfo *join;
    if (!out || !g_lc.launch_pending)
        return 0;
    join = &g_lc.join;
    if (!join->bind_hostport[0])
        return 0;
    /* Guests need a concrete host peer. Host may leave peer empty so transport
     * learns the guest from the first UDP packet. */
    if (join->local_slot != 0 && !join->peer_hostport[0] && !join->transport_ice_hub)
        return 0;
    *out = *join;
    return 1;
}

/* ---- peer-to-peer mod transfer -----------------------------------------
 *
 * Bytes go over a dedicated ICE agent straight between the two players. The
 * lobby server carries only the SDP and candidate lines needed to build that
 * connection, on the same relay the seats already use.
 */

/* What an agent EMITS is not what its peer must be PUSHED.
 *
 * An agent describes itself with LOCAL_SDP / LOCAL_CANDIDATE; the far side has
 * to receive those as REMOTE_*, because to it they are the remote description.
 * Forwarded unchanged, both agents gather candidates, neither ever learns the
 * other's description, and both sit in "connecting" until they time out --
 * with nothing in either log to show they were talking past each other.
 *
 * An engine's own session does the same translation (snesrecomp's
 * snes_netplay.c says so in a one-line comment). Kept as a named function
 * here so the rule is a thing that can be asserted, not a pair of ifs buried
 * in a message handler.
 *
 * GATHERING_DONE and SET_CONTROLLING mean the same on both sides and pass
 * through. */
static int mod_ice_type_for_push(int emitted_type)
{
    if (emitted_type == (int)RNET_SIGNAL_LOCAL_SDP)
        return (int)RNET_SIGNAL_REMOTE_SDP;
    if (emitted_type == (int)RNET_SIGNAL_LOCAL_CANDIDATE)
        return (int)RNET_SIGNAL_REMOTE_CANDIDATE;
    return emitted_type;
}

/* Is this ICE pair relayed? Only "relay" is; "unknown" is not an answer, and
 * the caller must not treat it as one -- see rnet_lobby_mod_relay_size_allows. */
static int mod_path_is_relay(const char *path)
{
    return path && !strcmp(path, "relay");
}

static int mod_path_is_known(const char *path)
{
    if (!path || !path[0]) return 0;
    return !strcmp(path, "relay") || !strcmp(path, "host") ||
           !strcmp(path, "srflx") || !strcmp(path, "prflx");
}

int rnet_lobby_mod_relay_size_allows(const char *path, uint64_t bytes,
                                     const char *package_id,
                                     char *reason, size_t reason_cap)
{
    if (reason && reason_cap) reason[0] = '\0';
    if (!mod_path_is_relay(path)) return 1;
    if (bytes <= (uint64_t)RNET_LOBBY_MOD_RELAY_MAX_BYTES) return 1;

    if (reason && reason_cap) {
        /* Say the size, say the cap, and say what to do instead. A player who
         * is only told "too large" has no way to tell a mod that is over by a
         * hair from one that was never going to fit, and no idea that the
         * same download would have worked on a different network. */
        snprintf(reason, reason_cap,
                 "%s is %.1f MB. This connection could not find a direct route "
                 "between the two of you, so the file would have to go through "
                 "the relay server, which is capped at %u MB. Download the mod "
                 "from its original source instead.",
                 (package_id && package_id[0]) ? package_id : "that mod",
                 (double)bytes / (1024.0 * 1024.0),
                 (unsigned)(RNET_LOBBY_MOD_RELAY_MAX_BYTES / (1024u * 1024u)));
    }
    return 0;
}

static void mod_xfer_emit(const RNetSignal *msg, void *user)
{
    (void)user;
    if (!msg) return;
    (void)rnet_lobby_send_signal_to(g_lc.xfer_peer,
                                    RNET_LOBBY_SIG_MOD_ICE_BASE + (int)msg->type,
                                    (int)msg->flag, msg->text);
}

static void mod_xfer_reset(void)
{
    if (g_lc.xfer) rnet_ice_xfer_close(&g_lc.xfer);
    g_lc.xfer = NULL;
    g_lc.xfer_busy = 0;
    g_lc.xfer_sending = 0;
    g_lc.xfer_peer[0] = '\0';
    g_lc.xfer_id[0] = '\0';
    g_lc.xfer_ver[0] = '\0';
    g_lc.xfer_sha[0] = '\0';
    g_lc.xfer_expect = 0;
    /* Held bytes that never reached the queue are ours to release. Freed with
     * the exporter's own free hook, because the exporter allocated them --
     * only a blob that made it into rnet_ice_xfer_queue_blob belongs to the
     * transfer, which frees it itself. */
    if (g_lc.xfer_hold) {
        if (g_mod_free_fn) g_mod_free_fn(g_lc.xfer_hold);
        g_lc.xfer_hold = NULL;
    }
    g_lc.xfer_hold_len = 0;
    g_lc.xfer_hold_hdr[0] = '\0';
    g_lc.xfer_path_priced = 0;
    rnet_sig_hold_clear(&g_lc.sig_hold);
}

static void mod_xfer_fail(const char *why)
{
    snprintf(g_lc.xfer_err, sizeof(g_lc.xfer_err), "%s",
             why && why[0] ? why : "transfer failed");
    fprintf(stderr, "rnet_lobby: mod transfer failed - %s\n", g_lc.xfer_err);
    g_lc.xfer_progress = -2;
    mod_xfer_reset();
}

/* Build the ICE config from the lobby's Coturn mint. Copied into g_lc because
 * RNetIceConfig holds borrowed pointers that must outlive the agent. */
static int mod_xfer_ice_config(RNetIceConfig *ice, int controlling)
{
    const RNetLobbyTurnCredentials *tc = rnet_lobby_turn_credentials();
    rnet_ice_config_init_defaults(ice);
    /* The role is decided HERE, before the agent exists.
     *
     * rnet_ice_config_init_defaults leaves controlling = 1, and
     * rnet_ice_xfer_open starts gathering immediately -- so leaving it alone
     * made both peers offerers, which is what libjuice was reporting as
     * "ICE role conflict (both controlling)". It still connected, because ICE
     * resolves a conflict by comparing tiebreakers, but the two then raced to
     * offer instead of one offering and one answering.
     *
     * The answerer defers gathering until the offer arrives (see
     * rnet_ice_agent_start_gathering), which is also why the requester used to
     * emit candidates before the sender had an agent at all. */
    ice->controlling = (rnet_u8)(controlling ? 1 : 0);
    if (!tc || !tc->valid)
        return 0;              /* host-candidate only; fine on a LAN */
    if (tc->stun_host[0]) {
        snprintf(g_lc.ice_stun, sizeof(g_lc.ice_stun), "%s", tc->stun_host);
        ice->stun_host = g_lc.ice_stun;
        ice->stun_port = (rnet_u16)(tc->stun_port > 0 ? tc->stun_port : 3478);
    }
    if (tc->turn_host[0]) {
        snprintf(g_lc.ice_turn, sizeof(g_lc.ice_turn), "%s", tc->turn_host);
        snprintf(g_lc.ice_user, sizeof(g_lc.ice_user), "%s", tc->username);
        snprintf(g_lc.ice_pass, sizeof(g_lc.ice_pass), "%s", tc->password);
        ice->turn_host = g_lc.ice_turn;
        ice->turn_port = (rnet_u16)(tc->turn_port > 0 ? tc->turn_port : 3478);
        ice->turn_user = g_lc.ice_user;
        ice->turn_pass = g_lc.ice_pass;
    }
    return 1;
}

static int mod_xfer_open(const char *peer, int controlling)
{
    RNetIceConfig ice;

    if (g_lc.xfer) rnet_ice_xfer_close(&g_lc.xfer);
    (void)mod_xfer_ice_config(&ice, controlling);
    if (rnet_ice_xfer_open(&g_lc.xfer, &ice, mod_xfer_emit, NULL) != 0 ||
        !g_lc.xfer) {
        mod_xfer_fail("could not open a direct connection");
        return -1;
    }
    snprintf(g_lc.xfer_peer, sizeof(g_lc.xfer_peer), "%s", peer ? peer : "");
    /* No SET_CONTROLLING signal is pushed here. It would arrive after
     * rnet_ice_xfer_open has already started gathering, so it could only
     * change a flag after the decision it governs had been made -- the role
     * belongs in the config above, where the agent reads it. */
    g_lc.xfer_busy = 1;
    g_lc.xfer_progress = 0;
    g_lc.xfer_err[0] = '\0';
    g_lc.xfer_last_state = -1;
    g_lc.xfer_started_ms = lobby_mono_ms();
    g_lc.xfer_connected_ms = 0;

    /* Replay anything that arrived while we were still getting ready, but
     * only from the peer this agent is for -- a hold from an abandoned
     * exchange would poison the new one. */
    {
        const int held = rnet_sig_hold_count(&g_lc.sig_hold, g_lc.xfer_peer);
        if (held > 0) {
            int i;
            fprintf(stderr, "rnet_lobby: replaying %d held ICE signal(s)\n", held);
            for (i = 0; i < held; ++i) {
                const RNetSigHoldEntry *e =
                    rnet_sig_hold_get(&g_lc.sig_hold, g_lc.xfer_peer, i);
                if (e) rnet_ice_xfer_push_signal(g_lc.xfer, &e->sig);
            }
        }
        /* Only this peer's bucket is consumed; a hold from an abandoned
         * exchange with someone else ages out rather than poisoning this one. */
        rnet_sig_hold_drop(&g_lc.sig_hold, g_lc.xfer_peer);
        rnet_sig_hold_expire(&g_lc.sig_hold, lobby_mono_ms(), 30000u);
    }
    return 0;
}

/* HOST side: a seated peer asked for a package. */
static void mod_xfer_on_request(const char *from, const char *text)
{
    char id[RNET_LOBBY_MOD_ID_LEN];
    char ver[RNET_LOBBY_MOD_VER_LEN];
    const char *at;
    size_t idlen;
    uint8_t *blob = NULL;
    uint32_t len = 0;
    char sha[65];
    char err[192];
    char header[512];

    /* Every refusal below is reported to the asking peer AND logged here.
     * The first version only sent the reason down the wire, so a host whose
     * export failed showed a clean log while the guest was told "could not
     * pack the mod" -- the one machine that knew why said nothing. */
    err[0] = '\0';
    sha[0] = '\0';

    if (!from || !from[0] || !text) return;
    fprintf(stderr, "rnet_lobby: %s asked for \"%s\"\n", from, text);
    if (g_lc.xfer_busy) {
        fprintf(stderr, "rnet_lobby: refusing - already transferring\n");
        (void)rnet_lobby_send_signal_to(from, RNET_LOBBY_SIG_MOD_NAK, 0,
                                        "the host is already sending a mod; "
                                        "try again in a moment");
        return;
    }
    at = strchr(text, '@');
    if (!at) return;
    idlen = (size_t)(at - text);
    if (idlen == 0 || idlen >= sizeof(id)) return;
    memcpy(id, text, idlen);
    id[idlen] = '\0';
    snprintf(ver, sizeof(ver), "%s", at + 1);

    /* Only ever send something this host actually runs. The plan is the list
     * the guest was shown; anything else is a request we have no reason to
     * honour, and honouring it would let a peer pull arbitrary packages off
     * this machine by name. */
    {
        int i;
        int in_plan = 0;
        for (i = 0; i < g_lc.match_caps.mod_count; ++i)
            if (!strcmp(g_lc.match_caps.mods[i].id, id)) {
                in_plan = 1;
                snprintf(ver, sizeof(ver), "%s", g_lc.match_caps.mods[i].ver);
                break;
            }
        if (!in_plan) {
            fprintf(stderr,
                    "rnet_lobby: refusing - \"%s\" is not in this host's "
                    "published plan (%d package(s))\n",
                    id, g_lc.match_caps.mod_count);
            (void)rnet_lobby_send_signal_to(from, RNET_LOBBY_SIG_MOD_NAK, 0,
                                            "that mod is not part of this "
                                            "lobby's plan");
            return;
        }
    }

    if (!g_mod_export_fn) {
        /* Distinct from an export that ran and failed: this build never
         * installed the hook, which is a wiring fault on this side, not
         * anything about the package. Previously both said "could not pack
         * the mod" -- and read err[] uninitialized to decide which. */
        fprintf(stderr, "rnet_lobby: refusing - no mod export hook is "
                        "installed in this build\n");
        (void)rnet_lobby_send_signal_to(from, RNET_LOBBY_SIG_MOD_NAK, 0,
                                        "the host's build cannot send mods");
        return;
    }
    if (g_mod_export_fn(id, ver, &blob, &len, sha, sizeof(sha), err,
                           sizeof(err), g_mod_hook_ctx) != 1) {
        fprintf(stderr, "rnet_lobby: refusing - packing %s@%s failed: %s\n",
                id, ver, err[0] ? err : "(the exporter gave no reason)");
        (void)rnet_lobby_send_signal_to(from, RNET_LOBBY_SIG_MOD_NAK, 0,
                                        err[0] ? err : "could not pack the mod");
        return;
    }

    if (mod_xfer_open(from, /*controlling=*/1) != 0) {
        if (g_mod_free_fn) g_mod_free_fn(blob);
        return;
    }
    g_lc.xfer_sending = 1;
    snprintf(g_lc.xfer_id, sizeof(g_lc.xfer_id), "%s", id);
    snprintf(g_lc.xfer_ver, sizeof(g_lc.xfer_ver), "%s", ver);

    /* Two blobs: what is coming, then the thing itself. The digest travels in
     * the header so the receiver can check the payload against a value that
     * did not come from the payload.
     *
     * Neither goes out yet. Whether this transfer is allowed to be this big
     * depends on whether ICE ends up on a direct pair or on the relay, and
     * that is not decided until the agent connects -- so both are held and
     * the pump releases them once it can price the path. */
    snprintf(header, sizeof(header),
             "{\"id\":\"%s\",\"ver\":\"%s\",\"len\":%u,\"sha256\":\"%s\"}",
             id, ver, (unsigned)len, sha);
    snprintf(g_lc.xfer_hold_hdr, sizeof(g_lc.xfer_hold_hdr), "%s", header);
    g_lc.xfer_hold = blob;
    g_lc.xfer_hold_len = (size_t)len;
    g_lc.xfer_path_priced = 0;
    fprintf(stderr,
            "rnet_lobby: packed %s@%s (%u bytes) for %s - waiting for the "
            "path before sending\n",
            id, ver, (unsigned)len, from);
}

/* HOST side: the agent is up, so the pair type is finally knowable. Decide
 * whether the held archive may go, then either queue it or refuse it. */
static void mod_xfer_release_held(void)
{
    char path[32];
    char reason[256];
    uint8_t *hdr_copy;
    size_t hdr_len;
    uint8_t *blob;
    size_t blob_len;

    if (!g_lc.xfer || !g_lc.xfer_hold || g_lc.xfer_path_priced)
        return;

    rnet_ice_xfer_path(g_lc.xfer, path, sizeof(path));
    if (!mod_path_is_known(path)) {
        /* Connected but the pair has no name yet. Wait -- briefly. Refusing
         * here would cap direct transfers we merely failed to identify, and
         * allowing here would let an oversized one onto the relay by being
         * asked a moment too early. Two seconds, then take the transfer at
         * its word and say so. */
        if (g_lc.xfer_connected_ms &&
            lobby_mono_ms() - g_lc.xfer_connected_ms < 2000u)
            return;
        fprintf(stderr,
                "rnet_lobby: could not tell whether this link is relayed "
                "(path=\"%s\") - sending %s@%s uncapped\n",
                path, g_lc.xfer_id, g_lc.xfer_ver);
    }

    if (!rnet_lobby_mod_relay_size_allows(path, (uint64_t)g_lc.xfer_hold_len,
                                          g_lc.xfer_id, reason,
                                          sizeof(reason))) {
        fprintf(stderr, "rnet_lobby: refusing to relay %s@%s - %s\n",
                g_lc.xfer_id, g_lc.xfer_ver, reason);
        (void)rnet_lobby_send_signal_to(g_lc.xfer_peer,
                                        RNET_LOBBY_SIG_MOD_NAK, 0, reason);
        /* Not mod_xfer_fail: nothing failed on this machine, and the host's
         * own panel should not light up red because a guest asked for
         * something the network cannot carry. */
        g_lc.xfer_progress = -1;
        mod_xfer_reset();
        return;
    }

    hdr_len = strlen(g_lc.xfer_hold_hdr);
    hdr_copy = (uint8_t *)malloc(hdr_len + 1);
    if (!hdr_copy) {
        mod_xfer_fail("out of memory");
        return;
    }
    memcpy(hdr_copy, g_lc.xfer_hold_hdr, hdr_len + 1);

    /* Ownership moves to the transfer on a successful queue, so drop our
     * pointer FIRST -- mod_xfer_reset frees xfer_hold, and a failure below
     * would otherwise free bytes the transfer already owns. */
    blob = g_lc.xfer_hold;
    blob_len = g_lc.xfer_hold_len;
    g_lc.xfer_hold = NULL;
    g_lc.xfer_hold_len = 0;
    g_lc.xfer_path_priced = 1;

    /* Queued one at a time, not short-circuited: rnet_ice_xfer_queue_blob
     * takes ownership on every path, so if the header fails to queue the
     * archive is still ours and has to be released here. */
    if (rnet_ice_xfer_queue_blob(g_lc.xfer, hdr_copy, hdr_len) != 0) {
        if (g_mod_free_fn) g_mod_free_fn(blob);
        mod_xfer_fail("could not queue the mod for sending");
        return;
    }
    if (rnet_ice_xfer_queue_blob(g_lc.xfer, blob, blob_len) != 0) {
        mod_xfer_fail("could not queue the mod for sending");
        return;
    }
    fprintf(stderr, "rnet_lobby: sending %s@%s (%u bytes) over the %s path\n",
            g_lc.xfer_id, g_lc.xfer_ver, (unsigned)blob_len, path);
}

/* GUEST side: a completed blob arrived. */
static void mod_xfer_on_blob(uint8_t *data, size_t len)
{
    char err[192];
    char id[96];
    char ver[32];

    fprintf(stderr, "rnet_lobby: received %u byte(s) over the direct link\n",
            (unsigned)len);
    if (!g_lc.xfer_sha[0]) {
        /* First blob is the header. */
        char text[600];
        size_t n = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
        memcpy(text, data, n);
        text[n] = '\0';
        char got_id[RNET_LOBBY_MOD_ID_LEN];
        got_id[0] = '\0';
        json_get_str(text, "sha256", g_lc.xfer_sha, sizeof(g_lc.xfer_sha));
        json_get_str(text, "id", got_id, sizeof(got_id));
        /* The host is answering a specific request. If it describes a
         * different package, something is confused on one side or the other
         * and installing it anyway would put a mod on this machine that
         * nobody asked for. */
        if (g_lc.xfer_id[0] && got_id[0] && strcmp(got_id, g_lc.xfer_id) != 0) {
            free(data);
            mod_xfer_fail("the host offered a different mod than the one "
                          "requested");
            return;
        }
        if (got_id[0])
            snprintf(g_lc.xfer_id, sizeof(g_lc.xfer_id), "%s", got_id);
        json_get_str(text, "ver", g_lc.xfer_ver, sizeof(g_lc.xfer_ver));
        g_lc.xfer_expect = (uint32_t)json_get_int(text, "len", 0);
        free(data);
        if (!g_lc.xfer_sha[0] || !g_lc.xfer_expect) {
            mod_xfer_fail("the host described the mod in a way we cannot read");
            return;
        }
        /* The same cap, applied on the receiving side.
         *
         * The host checks before it queues, so in a matched pair this never
         * fires. It is here because the header is the first moment THIS
         * machine knows the size, and a peer running an older or altered
         * build would otherwise be able to push an oversized archive across
         * the relay -- the cap protects the relay operator, so it cannot
         * depend on the other end choosing to honour it. */
        {
            char path[32];
            char reason[256];
            rnet_ice_xfer_path(g_lc.xfer, path, sizeof(path));
            if (!rnet_lobby_mod_relay_size_allows(path,
                                                  (uint64_t)g_lc.xfer_expect,
                                                  g_lc.xfer_id, reason,
                                                  sizeof(reason))) {
                mod_xfer_fail(reason);
                return;
            }
        }
        return;
    }

    if ((uint32_t)len != g_lc.xfer_expect) {
        free(data);
        mod_xfer_fail("the mod arrived a different size than the host said");
        return;
    }
    if (!g_mod_install_fn) {
        free(data);
        mod_xfer_fail("this build cannot install mods");
        return;
    }
    /* The digest is checked inside the install callback, before anything is
     * unpacked -- the engine's contract (snesrecomp:
     * snes_mod_runtime_install_blob_c). */
    if (g_mod_install_fn(data, (uint32_t)len, g_lc.xfer_sha, id, sizeof(id),
                            ver, sizeof(ver), err, sizeof(err),
                            g_mod_hook_ctx) != 1) {
        free(data);
        mod_xfer_fail(err[0] ? err : "the mod could not be installed");
        return;
    }
    free(data);
    fprintf(stderr, "rnet_lobby: installed %s@%s from the host\n", id, ver);
    g_lc.xfer_progress = 100;
    mod_xfer_reset();
    /* Re-announce: we now hold something we did not a moment ago, and the
     * host's launch gate is reading that announcement. */
    send_set_ready(g_lc.local_ready ? 1 : 0);
    flush_pending();
}

void rnet_lobby_mod_xfer_pump(void)
{
    uint8_t *data = NULL;
    size_t len = 0;
    char err[160];

    if (!g_lc.xfer) return;
    rnet_ice_xfer_pump(g_lc.xfer);

    /* Narrate the handshake. Without this a transfer that never connects and
     * one that connects and stalls look identical from the log: both are just
     * silence after "asked the host". */
    {
        const RNetIceState st = rnet_ice_xfer_state(g_lc.xfer);
        if ((int)st != g_lc.xfer_last_state) {
            g_lc.xfer_last_state = (int)st;
            fprintf(stderr, "rnet_lobby: mod transfer link is %s\n",
                    rnet_ice_state_name(st));
            if ((st == RNET_ICE_STATE_CONNECTED ||
                 st == RNET_ICE_STATE_COMPLETED) && !g_lc.xfer_connected_ms)
                g_lc.xfer_connected_ms = lobby_mono_ms();
        }
    }

    if (rnet_ice_xfer_failed(g_lc.xfer, err, sizeof(err))) {
        mod_xfer_fail(err);
        return;
    }

    /* Stall watchdog.
     *
     * A transfer that never connects used to sit at "busy" forever, and
     * because only one runs at a time every later attempt was refused with
     * "could not start the download" -- a message about the second click that
     * was really about the first one never ending. ICE either finds a path in
     * well under this or it is not going to. */
    {
        const uint64_t now = lobby_mono_ms();
        const uint64_t age = now - g_lc.xfer_started_ms;
        if (!g_lc.xfer_connected_ms && age > 45000u) {
            mod_xfer_fail("could not open a direct connection to the other "
                          "player (no route found)");
            return;
        }
        if (g_lc.xfer_connected_ms &&
            now - g_lc.xfer_connected_ms > 180000u) {
            mod_xfer_fail("the transfer stopped making progress");
            return;
        }
    }

    /* HOST: nothing has been queued yet -- the archive is held until the pair
     * type is known, so the relay cap is decided on the path the bytes would
     * actually take rather than on a guess made before ICE connected. */
    if (g_lc.xfer_hold && g_lc.xfer_connected_ms) {
        mod_xfer_release_held();
        if (!g_lc.xfer) return;      /* refused or failed inside */
    }

    {
        const int p = rnet_ice_xfer_progress(g_lc.xfer);
        if (p >= 0) g_lc.xfer_progress = p;
    }
    while (rnet_ice_xfer_take_blob(g_lc.xfer, &data, &len)) {
        mod_xfer_on_blob(data, len);
        if (!g_lc.xfer) return;      /* finished or failed inside */
        data = NULL;
        len = 0;
    }
    /* The sender is done when everything queued has left -- and not before
     * anything was queued. An idle send queue used to mean "finished"; now it
     * also describes a transfer still holding its archive while ICE connects,
     * and treating that as finished would close the link the instant it
     * opened, every time. xfer_path_priced is what tells the two apart. */
    if (g_lc.xfer_sending && g_lc.xfer_path_priced &&
        rnet_ice_xfer_send_idle(g_lc.xfer)) {
        fprintf(stderr, "rnet_lobby: %s@%s sent\n", g_lc.xfer_id, g_lc.xfer_ver);
        g_lc.xfer_progress = -1;
        mod_xfer_reset();
    }
}

int rnet_lobby_mod_request(const char *package_id, const char *version)
{
    char text[192];
    const char *host = g_lc.host_player_id;

    if (!rnet_lobby_connected() || !g_lc.in_lobby) return -1;
    if (g_lc.is_host) return -1;            /* the host IS the source */
    if (!host || !host[0]) return -1;
    if (g_lc.xfer_busy) {
        fprintf(stderr, "rnet_lobby: not asking for %s - already transferring "
                        "%s\n", package_id ? package_id : "?", g_lc.xfer_id);
        return -2;              /* distinct: busy, not broken */
    }
    if (!package_id || !package_id[0]) return -1;

    if (mod_xfer_open(host, /*controlling=*/0) != 0) return -1;
    g_lc.xfer_sending = 0;
    g_lc.xfer_sha[0] = '\0';
    g_lc.xfer_expect = 0;
    /* Named NOW, not when the header arrives.
     *
     * rnet_lobby_mod_in_flight() is what attributes progress to a row, and
     * what the "already transferring" message quotes. Left empty until the
     * first blob, the requesting side spent the whole connect phase unable to
     * say which package it was fetching: no row showed a bar, and the busy
     * refusal read "already transferring " with a blank where the name goes. */
    snprintf(g_lc.xfer_id, sizeof(g_lc.xfer_id), "%s", package_id);
    snprintf(g_lc.xfer_ver, sizeof(g_lc.xfer_ver), "%s", version ? version : "");
    snprintf(text, sizeof(text), "%s@%s", package_id, version ? version : "");
    if (rnet_lobby_send_signal_to(host, RNET_LOBBY_SIG_MOD_REQ, 0, text) != 0) {
        mod_xfer_fail("could not reach the host");
        return -1;
    }
    fprintf(stderr, "rnet_lobby: asked the host for %s\n", text);
    return 0;
}

void rnet_lobby_mod_cancel(void)
{
    if (!g_lc.xfer_busy) return;
    fprintf(stderr, "rnet_lobby: mod transfer cancelled\n");
    g_lc.xfer_progress = -1;
    mod_xfer_reset();
}

int rnet_lobby_mod_progress(void)
{
    return g_lc.xfer_busy ? g_lc.xfer_progress : (g_lc.xfer_progress == -2 ? -2 : -1);
}

int rnet_lobby_mod_failed(char *err, size_t err_cap)
{
    if (!g_lc.xfer_err[0]) return 0;
    if (err && err_cap) snprintf(err, err_cap, "%s", g_lc.xfer_err);
    return 1;
}

const char *rnet_lobby_mod_in_flight(void)
{
    return g_lc.xfer_busy ? g_lc.xfer_id : "";
}

void rnet_lobby_set_mod_transfer_hooks(RNetLobbyModExportFn export_fn,
                                       RNetLobbyModFreeFn free_fn,
                                       RNetLobbyModInstallFn install_fn,
                                       void *ctx)
{
    g_mod_export_fn = export_fn;
    g_mod_free_fn = free_fn;
    g_mod_install_fn = install_fn;
    g_mod_hook_ctx = ctx;
}

int rnet_lobby_send_signal_to(const char *to_player_id, int type, int flag,
                              const char *text)
{
    char esc[4096];
    char lid_esc[JSON_ESC_CAP(RNET_LOBBY_ID_LEN)];
    char to_esc[JSON_ESC_CAP(RNET_LOBBY_ID_LEN)];
    char msg[4608];
    const char *lid;
    if (!rnet_lobby_connected() || !g_lc.in_lobby) {
        return -1;
    }
    lid = g_lc.join.lobby_id[0] ? g_lc.join.lobby_id : "";
    json_escape(text ? text : "", esc, sizeof(esc));
    json_escape(lid, lid_esc, sizeof(lid_esc));
    /* An empty to_player_id broadcasts to the other seated members, which is
     * right for the game's own ICE but wrong for a transfer: a third player
     * would push a stranger's SDP into their agent and corrupt a negotiation
     * they are not part of. */
    json_escape(to_player_id ? to_player_id : "", to_esc, sizeof(to_esc));
    snprintf(msg, sizeof(msg),
             "{\"op\":\"signal\",\"lobby_id\":\"%s\",\"to_player_id\":\"%s\","
             "\"type\":%d,\"flag\":%d,\"text\":\"%s\"}",
             lid_esc, to_esc, type, flag, esc);
    /* Write immediately — ICE candidates arrive in bursts larger than
     * pending_tx. Through the outbound buffer: a would-block keeps the frame
     * (and the rest of a half-sent one) for the next pump. */
    if (g_lc.handshake_done && g_lc.fd >= 0) {
        return ws_send(msg) < 0 ? -1 : 0;
    }
    queue_send(msg);
    return 0;
}

int rnet_lobby_send_signal(int type, int flag, const char *text)
{
    return rnet_lobby_send_signal_to("", type, flag, text);
}

int rnet_lobby_poll_signal(int *type, int *flag, char *text, size_t text_cap)
{
    int i;
    if (g_lc.sig_count <= 0) {
        return 0;
    }
    i = g_lc.sig_head;
    if (type) *type = g_lc.sig_q[i].type;
    if (flag) *flag = g_lc.sig_q[i].flag;
    if (text && text_cap) {
        strncpy(text, g_lc.sig_q[i].text, text_cap - 1);
        text[text_cap - 1] = '\0';
    }
    g_lc.sig_head = (g_lc.sig_head + 1) % (int)(sizeof(g_lc.sig_q) / sizeof(g_lc.sig_q[0]));
    g_lc.sig_count--;
    return 1;
}

int rnet_lobby_request_turn_credentials(void)
{
    if (!rnet_lobby_connected())
        return -1;
    /* Refresh if missing, expired, or never requested. */
    if (g_lc.turn.valid && g_lc.turn_received_at > 0 && g_lc.turn.ttl_secs > 0) {
        time_t now = time(NULL);
        if (now >= g_lc.turn_received_at &&
            (uint32_t)(now - g_lc.turn_received_at) + 60u < g_lc.turn.ttl_secs) {
            return 0; /* still fresh (60s skew margin) */
        }
    }
    return queue_turn_credentials_request();
}

const RNetLobbyTurnCredentials *rnet_lobby_turn_credentials(void)
{
    if (g_lc.turn.valid && g_lc.turn_received_at > 0 && g_lc.turn.ttl_secs > 0) {
        time_t now = time(NULL);
        if (now < g_lc.turn_received_at ||
            (uint32_t)(now - g_lc.turn_received_at) >= g_lc.turn.ttl_secs) {
            clear_turn_credentials();
        }
    }
    return &g_lc.turn;
}
