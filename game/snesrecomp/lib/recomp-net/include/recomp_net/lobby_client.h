#ifndef RNET_LOBBY_CLIENT_H
#define RNET_LOBBY_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RNET_LOBBY_ID_LEN 40
#define RNET_LOBBY_NAME_LEN 64
#define RNET_LOBBY_VERSION_LEN 32
#define RNET_LOBBY_ENDPOINT_LEN 64
#define RNET_LOBBY_MAX_LIST 32
/* Largest outbound frame the client queues (bytes, NUL included). */
#define RNET_LOBBY_TX_MAX 12288

/* Player seats the client can TRACK. A title's own ceiling is smaller and is
 * set at run time (RNetLobbyConfig.max_players); this is the array size, and
 * matches recomp-ui's RECOMP_LAUNCHER_NETPLAY_MAX_MEMBERS. */
#define RNET_LOBBY_MAX_PLAYERS 8
/* Spectator seats a host may open, on top of the players. */
#define RNET_LOBBY_MAX_SPECTATORS 4
/* Rows in the membership table: both tables land in it, tagged by role. */
#define RNET_LOBBY_MAX_MEMBERS \
    (RNET_LOBBY_MAX_PLAYERS + RNET_LOBBY_MAX_SPECTATORS)

/* Seat indices are one namespace, matching the server: below the base is a
 * player seat, at or above it is `index - base` in the gallery. The server
 * republishes its own base in every lobby_update; this is the compiled-in
 * default for the case where it does not. */
#define RNET_LOBBY_SPECTATOR_SLOT_BASE 64

/* ── Configuration ──────────────────────────────────────────────────────────
 *
 * Set once at start-up, before the first connect. Every field is optional;
 * the zero value of each is the documented default. It survives
 * rnet_lobby_disconnect() (it describes the BUILD, not a connection).
 */
typedef struct RNetLobbyConfig {
    /* Title and release pin, exactly as rnet_lobby_set_game_identity takes
     * them. NULL leaves whatever was set before. */
    const char *game_name;
    const char *game_version;
    /* Moderation metadata only ("snes", "psx", "n64"). NULL/"" -> "unknown".
     * Nothing may key a file, a queue or a directory on it. */
    const char *platform;
    /* PLAYER seat ceiling this title supports, 2..RNET_LOBBY_MAX_PLAYERS.
     * create() clamps to it. 0 -> 4 (the SNES client's historic ceiling). */
    int max_players;
    /* Seats an automatch ticket asks for. 0 -> 2 (the server pools two-player
     * matches only today and answers slots_not_pooled otherwise). */
    int automatch_slots;
    /* An older environment spelling to accept beside the RNET_LOBBY_* names,
     * e.g. "SNES_NET_" makes SNES_NET_LOBBY_URL and SNES_NET_GAME_VERSION
     * work as they did before the lift. The RNET_LOBBY_* name wins when both
     * are set (and a disagreement is logged). NULL = no alias. */
    const char *legacy_env_prefix;
} RNetLobbyConfig;

void rnet_lobby_configure(const RNetLobbyConfig *cfg);

typedef struct RNetLobbyRow {
    char     lobby_id[RNET_LOBBY_ID_LEN];
    char     name[RNET_LOBBY_NAME_LEN];
    char     game_name[RNET_LOBBY_NAME_LEN];
    char     game_version[RNET_LOBBY_VERSION_LEN];
    int      player_count;
    int      max_slots;
    int      has_password;
    /* Host's country (alpha-2) from the server's GeoIP; "" unknown. */
    char     host_country[4];
    /* Gallery: 0 = none; else how many watch out of how many seats. */
    int      allow_spectators;
    int      max_spectators;
    int      spectator_count;
} RNetLobbyRow;

/* One player connected to the hub (the lobby_list `players` array). */
typedef struct RNetLobbyOnlinePlayer {
    char display_name[RNET_LOBBY_NAME_LEN];
    char country[4];
    char lobby_id[RNET_LOBBY_ID_LEN];
    char lobby_name[RNET_LOBBY_NAME_LEN];
    int  hosting;
    /* First 8 characters of the player's connection id: enough to tell
     * "which row is me" without publishing whole ids to browsers. */
    char tag[12];
    /* Opaque account id behind this player; "" for a guest. Not a name and
     * not a Discord identifier -- a key a local ignore/block list can use
     * because it survives the player reconnecting and renaming. */
    char account[RNET_LOBBY_ID_LEN];
    /* The title that player is browsing for; rows of other titles are
     * dropped at parse time when this client has a game identity. */
    char game_name[RNET_LOBBY_NAME_LEN];
} RNetLobbyOnlinePlayer;
#define RNET_LOBBY_MAX_ONLINE 64

/* One lobby chat line.
 *
 * The server echoes every line to everyone in the room INCLUDING the sender,
 * so this ring is the room's order rather than ours -- the client never
 * appends its own send locally, or two peers would disagree about what was
 * said when. */
#define RNET_LOBBY_CHAT_TEXT_LEN 256
#define RNET_LOBBY_CHAT_RING 64
typedef struct RNetLobbyChatMsg {
    char     player_id[RNET_LOBBY_ID_LEN];
    /* Opaque account id behind this player; "" for a guest. Not a name and
     * not a Discord identifier -- a key a local ignore/block list can use
     * because it survives the player reconnecting and renaming. */
    char     account[RNET_LOBBY_ID_LEN];
    char     from[RNET_LOBBY_NAME_LEN];
    char     text[RNET_LOBBY_CHAT_TEXT_LEN];
    /* The SERVER's id for this line, and what rnet_lobby_report_chat names.
     * Empty for a system line, for a locally generated one, and for anything
     * relayed by a server too old to assign one -- in all three cases the
     * line cannot be reported, because there is no agreed referent for it.
     *
     * A report carries this and NOT the text: the server writes down what it
     * relayed under this id. Sending the words would let a client fabricate a
     * message and have somebody sanctioned for it. */
    char     mid[40];
    int      is_local;
    int      is_system;
    uint32_t seq;
} RNetLobbyChatMsg;

typedef struct RNetLobbyMember {
    /* Seat index in the shared namespace: a player seat, or
     * spectator_slot_base + gallery index. Pass it back to kick / move as-is. */
    int  slot;
    char player_id[RNET_LOBBY_ID_LEN];
    char display_name[RNET_LOBBY_NAME_LEN];
    int  ready;
    /* 1 when this row is in the gallery. Read this rather than comparing
     * `slot` against a base: the base is the server's to choose. */
    int  is_spectator;
    /* Country (alpha-2) from the server's GeoIP; "" unknown. */
    char country[4];
    /* Host-relay room: this guest's latest path_report ("direct" | "fail" |
     * "relay"), "" when none, and whether the server still trusts it at start
     * (WS_LOBBY.md "Host relay"). From a server that publishes it; else "". */
    char path[8];
    int  path_fresh;
} RNetLobbyMember;

/* One package on the lobby wire -- a row of the host's required plan, or of a
 * peer's offer of what it already has. Deliberately independent of the mod
 * runtime's own types: the lobby transports package identity and knows nothing
 * about what a package contains. */
#define RNET_LOBBY_MAX_MODS      16
#define RNET_LOBBY_MOD_ID_LEN    96
#define RNET_LOBBY_MOD_VER_LEN   32
#define RNET_LOBBY_MOD_NAME_LEN  64
#define RNET_LOBBY_MOD_FEATS_LEN 192

typedef struct RNetLobbyModPkg {
    char id[RNET_LOBBY_MOD_ID_LEN];
    char ver[RNET_LOBBY_MOD_VER_LEN];
    /* Display name, for a player reading the list. Empty on an offer row. */
    char name[RNET_LOBBY_MOD_NAME_LEN];
    /* Comma-separated ids of the features the host enabled. Empty on an offer
     * row, which claims possession only. Never part of the seat decision --
     * the server matches on (id, ver). */
    char feats[RNET_LOBBY_MOD_FEATS_LEN];
    /* Offer rows only: comma-separated ids of this package's features whose
     * required owner files (a source ROM, say) this peer has not provided.
     * Every such feature, not just the ones the host enabled, so the claim
     * stays true when the host changes the plan and the host intersects it
     * with its own. Empty on a plan row, and on the wire only when non-empty:
     * an offer without it reads as "nothing missing", which is what an older
     * peer that cannot tell would have said. */
    char nf[RNET_LOBBY_MOD_FEATS_LEN];
} RNetLobbyModPkg;

/* Fills `out` with the packages this peer already has, returning the count.
 *
 * Installed by the host layer, because the lobby transports package identity
 * and deliberately knows nothing about what a package is or where it lives. */
typedef int (*RNetLobbyModOfferFn)(RNetLobbyModPkg *out, int max, void *ctx);

/* Without a supplier this peer announces nothing, and every other peer reads
 * that as "has no mods" -- so the host's launch gate holds the match. That is
 * the safe direction (held rather than started into a desync), but it is only
 * the TRUE answer if the peer genuinely has nothing. Any build with a mod
 * runtime must install one. */
void rnet_lobby_set_mod_offer_supplier(RNetLobbyModOfferFn fn, void *ctx);

/* The last need_mods refusal: how many packages the server said were missing,
 * and each row. Valid until the next join attempt. `can_transfer` is the
 * server's claim that a transfer channel is available -- it is the server's
 * opinion about ITSELF, not a promise that this build can drive one. */
int rnet_lobby_need_mods_count(void);
const RNetLobbyModPkg *rnet_lobby_need_mods_get(int index);
int rnet_lobby_need_mods_can_transfer(void);

/* The launch gate. Returns how many plan packages the OTHER seated peers are
 * missing (0 = the match may start), naming the first offender and the first
 * package they lack. Joining is deliberately not gated on this -- a peer
 * without the mods belongs in the lobby, where they can get them. */
int rnet_lobby_match_blocked_by_mods(char *who, size_t who_cap,
                                     char *what, size_t what_cap);

/* How many plan packages THIS peer is missing, as the host's published plan
 * and our own announced offer see it. */
int rnet_lobby_local_missing_mods(void);

/* The second half of the launch gate: plan features whose required owner files
 * a seated peer says it has not provided (RNetLobbyModPkg.nf), counted over the
 * OTHER seated players. Names the first offender and its first "package/feature"
 * pair. A package the peer does not have at all is the mods gate's business
 * and is not counted here. */
int rnet_lobby_match_blocked_by_files(char *who, size_t who_cap,
                                      char *what, size_t what_cap);

/* Mod readiness of member `index` (as rnet_lobby_member_get indexes it)
 * against the published plan: plan packages it does not have, and plan
 * features it has the package for but lacks required files for. `files`, when
 * given, receives those features as ';'-separated "package/feature" pairs.
 * Returns 1 when filled, 0 for a bad index or no plan. */
int rnet_lobby_member_mod_readiness(int index, int *mods_missing,
                                    int *files_missing, char *files,
                                    size_t files_cap);

/*
 * Host-authoritative sim settings negotiated over the lobby.
 * Guests apply these on launch so both peers boot with matching caps.
 *
 * Only the keys every engine shares are fields here. A title's own keys (the
 * SNES host's widescreen settlement, say) travel in `ext`, which this library
 * copies byte for byte and never reads: the engine writes and parses them
 * through the caps codec (rnet_lobby_set_caps_codec). Wire key names are the
 * engine's to choose and must not collide with the ones below.
 */
#define RNET_LOBBY_CAPS_EXT_BYTES 64
#define RNET_HAS_SESSION_VARIANT 1

typedef struct RNetLobbyMatchCaps {
    int  valid;            /* 1 when a host blob was received / set */
    int  input_delay;      /* recomp-net delay frames, clamped 2..20; default 6 */
    /* Rollback invent runway P, frames, clamped 2..16 when published. 0 = the
     * host published none, and the engine keeps its own default. Emitted on
     * the wire only when set, so a caps blob without it is byte-identical to
     * what a pre-lift client sent. */
    int  input_prediction;
    int  force_turn;       /* 0/1 — host: ICE relay-only (TURN) for all peers */
    int  force_input_relay; /* 0/1 — lobby-server UDP input relay */
    /* 0/1 -- host: ask the server for the HOST RELAY (match_caps.relay =
     * "host"): the host carries the match on its own UDP port and guests dial
     * it; the server falls back to its relay unless every guest proved the
     * path (recomp_net/host_relay.h). Guests read it to know the room asks. */
    int  relay_host;
    /* 0/1 -- match_caps.relay_via = "ice": with relay_host, the host carries
     * the match over ICE agents the waiting room connects (one per guest;
     * recomp_net/host_ice.h) instead of a UDP port it advertises. Published by
     * a host whose rnet_lobby_relay_via_ice() is on and whose build has ICE;
     * read by everyone. */
    int  relay_via_ice;
    int  rollback;         /* 0/1 — session mode; lobby default ON */
    /* The host's required mod plan, one row per PACKAGE.
     *
     * Host-authoritative like everything else here: mods patch guest memory,
     * so a peer running a different set is running a different game.
     *
     * Published as `match_caps.mod_plan`, deliberately NOT as `match_caps.mods`.
     *
     * `mods` is the key the lobby server enforces: required_mod_rows() reads it
     * and REFUSES TO SEAT a joiner whose mod_offer does not cover it. That is
     * the wrong door for this title. A player who lacks a mod should still get
     * into the room -- to see what is needed, to talk to the host, and above
     * all to download it from them. What must not happen is the MATCH starting
     * while the peers would simulate differently, and that gate belongs at
     * launch, where it can be enforced against every seated peer rather than
     * against one joiner at the moment they knock.
     *
     * The server passes unknown match_caps keys through untouched and echoes
     * them to members, so `mod_plan` reaches every peer while leaving the seat
     * gate inert. It is a policy difference, not a workaround: this title says
     * who may PLAY, the server says who may SIT.
     *
     * Encoded as an array of objects either way. An older encoding put the plan
     * in a ';'-separated STRING, which is valid JSON but not an array, so
     * anything reading it with as_array() got nothing -- and read a host with a
     * full plan as a host requiring nothing. */
    int mod_count;
    RNetLobbyModPkg mods[RNET_LOBBY_MAX_MODS];
    /* The host's EFFECTIVE set: one entry per enabled feature with its
     * resolved option values, ';' where the canonical text has newlines
     * (json_get_str does not unescape, so the text cannot carry them).
     *
     * The plan above says which packages a peer must HAVE. This says how the
     * host has them configured, which is a different and stricter question --
     * and the one the match actually enforces. A guest that owns both mods but
     * has enabled neither passes the plan and is still refused at launch,
     * because the two would simulate differently.
     *
     * Carried so a guest can match the host BEFORE launching rather than
     * discovering the difference from a refused match. */
    char mod_set[512];
    /* Which packages this match's AUTHORITY grants the presentation-only
     * exemption to: ';'-separated `id@version` or `id@version#sha256`.
     *
     * A mod declaring `presentation_only` in its own manifest is only asking.
     * This is the grant, and it comes from the server (in an automatch
     * ruleset) or from the host (in a lobby) -- never from the machine that
     * wants the exemption, because a cheat author writes manifests too.
     *
     * EMPTY MEANS NOTHING IS EXEMPT, which is what an older host and a
     * ruleset without the key both produce, and is the answer that fails
     * safe: an ungranted claim is treated as an ordinary simulation mod, so
     * it must match or be switched off.
     *
     * Names bytes, not just a string, when the `#sha256` form is used --
     * otherwise the list is keyed on an id the client picks for itself, and a
     * mod gets exempted by calling itself something allowlisted. */
    char mod_cosmetic_allow[512];
    /* Engine-owned keys, opaque here. Zeroed whenever the caps are cleared or
     * parsed; the codec's parse hook fills it from the caps object. The union
     * only forces alignment so an engine may overlay its own struct. */
    union {
        unsigned char bytes[RNET_LOBBY_CAPS_EXT_BYTES];
        long long     align_;
        double        align_d_;
    } ext;
    /* Append-only: opaque engine-defined hardware/rules variant; 0 = legacy
     * default. Mirrors the LAN lobby field. The engine validates support and
     * includes it in its startup identity before any simulation executes. */
    int session_variant;
} RNetLobbyMatchCaps;

/* Engine hook for match_caps keys this library does not know.
 *
 * write_extra: append `,"key":value` members for caps->ext to `out` (the
 *   generic members are already written; do not open or close the object).
 *   Return the bytes written (0 = nothing), or <0 when they do not fit -- the
 *   whole caps object is then withheld rather than published short, exactly
 *   as an oversized mod plan is. Free text MUST go through
 *   rnet_lobby_json_escape.
 * parse_extra: read the engine's keys out of one caps object (`caps_json` is
 *   the `{...}` text) into caps->ext. Called after the generic keys are parsed
 *   and ext has been zeroed, for the lobby's caps and for every automatch
 *   ruleset's caps alike.
 *
 * Either may be NULL. Without a codec `ext` stays zero and nothing extra is
 * sent. */
typedef struct RNetLobbyCapsCodec {
    int  (*write_extra)(const RNetLobbyMatchCaps *caps, char *out, size_t cap,
                        void *ctx);
    void (*parse_extra)(const char *caps_json, RNetLobbyMatchCaps *caps,
                        void *ctx);
    void *ctx;
} RNetLobbyCapsCodec;

void rnet_lobby_set_caps_codec(const RNetLobbyCapsCodec *codec);

/* The JSON helpers the client itself uses, for codec hooks. Flat key search:
 * they find the first `"key":` in `json`, so hand them the caps object, not
 * a whole message. json_get_str unescapes; json_escape doubles at most (size
 * `out` as 2 * strlen(in) + 8 and nothing is ever dropped). */
int         rnet_lobby_json_get_int(const char *json, const char *key, int def);
int         rnet_lobby_json_get_bool(const char *json, const char *key, int def);
const char *rnet_lobby_json_get_str(const char *json, const char *key,
                                    char *out, size_t cap);
size_t      rnet_lobby_json_escape(const char *in, char *out, size_t cap);

typedef struct RNetLobbyJoinInfo {
    int      ok;
    char     lobby_id[RNET_LOBBY_ID_LEN];
    uint32_t session_id;
    int      local_slot;
    char     host_endpoint[RNET_LOBBY_ENDPOINT_LEN];
    char     guest_endpoint[RNET_LOBBY_ENDPOINT_LEN];
    char     bind_hostport[RNET_LOBBY_ENDPOINT_LEN];
    char     peer_hostport[RNET_LOBBY_ENDPOINT_LEN];
    int      player_count;
    int      max_slots;
    /* 1 when this client holds a gallery seat: it runs the match and shows it,
     * and contributes no input to anyone. */
    int      local_is_spectator;
    /* Host opt-in, echoed by the server. 0 when the server predates
     * spectators, which is why every spectator control is gated on it. */
    int      allow_spectators;
    int      max_spectators;
    int      spectator_count;
    int      spectator_slot_base;
    /* Where the gallery starts in the RELAY's slot space -- a different
     * namespace from the lobby seat index above, and the one the engine needs.
     * The relay forwards nothing from a slot at or beyond its player count, so
     * this is the number that makes a spectator unable to send. */
    int      spectator_relay_base;
    /* This match's transport, as the LAUNCH stated it: 1 = the lobby server
     * allocated its UDP input relay and everyone dials it, 0 = peer-to-peer.
     *
     * Deliberately not read back out of match_caps at start time. The caps
     * copy of this flag is also written by the HOST's published caps, where it
     * means the host's UI toggle, so an unrelated republish landing between
     * the launch and the start silently reverted a relayed match to p2p. It is
     * recorded here, once per launch, by the only message that knows. */
    int      force_input_relay;
    /* The launch said transport "host": no relay_endpoint, the host carries
     * the match on host_endpoint (its advertised public port) and every guest
     * dials it. The host binds its own port (bind_hostport) and accepts the
     * guest (2 seats) or hubs (3+). 0 on an SFU launch and from a server that
     * predates the host relay. */
    int      transport_host;
    /* The launch said transport "host" AND relay_via "ice": the match rides the
     * ICE agents the waiting room already connected. host_endpoint is empty,
     * bind_hostport / peer_hostport are not to be used (the transport_host UDP
     * path must NOT be taken). Obtain the agents with rnet_lobby_ice_take_hub
     * (host) or rnet_lobby_ice_take_guest_agent (guest) and give them to the
     * session (rnet_session_start_ice_hub_adopt / rnet_session_adopt_ice_agent).
     * transport_host is also 1 in this case; test transport_ice_hub first. */
    int      transport_ice_hub;
    /* The launch said the host runs the match from the gallery (session slot
     * 0, pad muted, players at lobby seat + 1). 0 from a server that predates
     * it and on every launch where the host holds a player seat. */
    int      host_spectates;
    char     last_error[64]; /* need_password | bad_password | … */
} RNetLobbyJoinInfo;

/* RNET_LOBBY_URL (or <legacy_env_prefix>LOBBY_URL), else
 * ws://netplay.retcomm.net:8765 */
const char *rnet_lobby_default_url(void);

int  rnet_lobby_connect(const char *ws_url); /* 0 ok */
void rnet_lobby_disconnect(void);
int  rnet_lobby_connected(void);
/* Current WS URL when connected (else empty string). */
const char *rnet_lobby_url(void);

void rnet_lobby_set_display_name(const char *name);
const char *rnet_lobby_display_name(void);
const char *rnet_lobby_player_id(void);

/* Non-blocking pump — call every frame from the launcher. */
void rnet_lobby_pump(void);
/* 1 once the server's `welcome` has assigned a player id: the WebSocket
 * upgrade is done and ops will be answered. */
int  rnet_lobby_ready(void);

/* Title + release pin used for create/join matching and list filters.
 *
 * RNET_LOBBY_GAME_VERSION (or <legacy_env_prefix>GAME_VERSION) overrides the
 * pin for one run, for TESTING two
 * development builds against each other without reconfiguring and rebuilding
 * both. Set the same string on both machines. It is announced on stderr with
 * the real pin beside it every time the identity is set, because a build
 * lying about which build it is has to be visible in the log a desync report
 * is written from. Unset (the normal case) keeps the derived pin, which
 * matches only builds that really are the same build. */
void rnet_lobby_set_game_identity(const char *game_name,
                                  const char *game_version);
/* The pin actually presented on the wire -- the override when one is set. */
const char *rnet_lobby_game_version(void);
/*
 * Should a browser hide rooms whose game_version differs from ours?
 *
 * True only for a plain RELEASED version. Every development build carries a
 * qualifier ("dev+abc12345", "0.1.5+abc12345-dirty.1234abcd") and lists
 * UNFILTERED: the pin is still enforced at the join, but hiding the room robs
 * the player of the explanation, and "my friend's lobby isn't showing up" is
 * far worse to debug than "this lobby is a different build".
 *
 * Exported so the LAN beacon browser applies the SAME rule as the lobby-server
 * browser. It did not, and two developers on one LAN -- whose dirty-diff
 * hashes necessarily differ -- silently could not see each other at all.
 */
int rnet_lobby_version_filter_strict(void);

void rnet_lobby_request_list(void);
int  rnet_lobby_list_count(void);
int  rnet_lobby_list_get(int index, RNetLobbyRow *out);

/* Room-list latency in ms, before joining. Each rnet_lobby_request_list
 * (first open, Refresh) probes every listed room without a password once:
 * a short direct ICE link to its host, a few pings, closed. That figure is
 * returned with *is_direct = 1 (is_direct may be NULL). Until it lands, or
 * for a room it could not reach, the estimate below stands in (*is_direct
 * = 0). -1 when neither is known. */
int  rnet_lobby_list_latency_ms(int index, int *is_direct);
/* Through-the-server estimate: our round trip to the lobby server plus the
 * room host's (the server's per-row host_rtt_ms). Overstates a direct link.
 * -1 when either half is unknown (or an older server). */
int  rnet_lobby_list_latency_estimate_ms(int index);
/* Our round trip to the lobby server, timed once per connection and once per
 * list request; -1 until measured or when offline. */
int  rnet_lobby_server_rtt_ms(void);
#define RNET_LOBBY_HAS_LIST_LATENCY 1
#define RNET_LOBBY_HAS_LIST_DIRECT_LATENCY 1
/* Players connected to the hub, refreshed with every lobby_list. */
int  rnet_lobby_online_count(void);
int  rnet_lobby_online_get(int index, RNetLobbyOnlinePlayer *out);

/*
 * Create lobby. host_bind e.g. "0.0.0.0:7777". password may be NULL/empty.
 * match_caps may be NULL (legacy); when non-NULL and valid, sent to the server
 * so guests join with the host's sim settings.
 * Returns 0 if request sent; poll rnet_lobby_join_info() / in_lobby().
 */
int  rnet_lobby_create(const char *name, const char *game_name,
                      const char *game_version, const char *password,
                      const char *host_bind,
                      const RNetLobbyMatchCaps *match_caps,
                      int max_slots);

/* Join lobby. guest_bind may be NULL/empty/"host:0" — the client always
 * advertises a concrete UDP bind (prefers 7778..) so server-hosted launches
 * never hand the host peer_ip:0 (rnet_session_start_lan rejects port 0). */
int  rnet_lobby_join(const char *lobby_id, const char *password,
                    const char *guest_bind);

int  rnet_lobby_leave(void);

/* ---- spectators --------------------------------------------------------
 *
 * A gallery seat runs the match locally, in sync, and contributes nothing.
 * The server enforces the "contributes nothing" half at the relay; this side
 * enforces the rest -- no Ready vote, no controller reaching the sim.
 *
 * Every one of these reads 0 against a server that predates spectators, so a
 * caller that gates its UI on allow_spectators degrades to the old lobby. */

/* Host: open a gallery on the NEXT create. Sticky, like the max_slots
 * default -- create carries whatever this was last set to. */
void rnet_lobby_set_allow_spectators(int allow);
/* What the toggle is set to, for the UI to render. Distinct from the next
 * call: this is what the host ASKED for and has not created yet. */
int  rnet_lobby_allow_spectators_pref(void);
/* What the current lobby actually has (server-echoed), not what was asked.
 * These disagree whenever the server predates spectators -- which is exactly
 * the case the UI must not offer a gallery in. */
int  rnet_lobby_allow_spectators(void);
int  rnet_lobby_max_spectators(void);
int  rnet_lobby_spectator_count(void);
/* 1 when THIS client is in the gallery. The one call the engine needs. */
int  rnet_lobby_local_is_spectator(void);
/* Base of the gallery half of the seat namespace, as the server reports it. */
int  rnet_lobby_spectator_slot_base(void);
/* 1 when `slot` is a seat the server would accept in move / kick. */
int  rnet_lobby_seat_valid(int slot);
/* Seat index for gallery position `index`, for move / kick. */
int  rnet_lobby_spectator_slot(int index);
/* This client's slot in the RELAY's namespace, for RNetConfig.wire_slot.
 * -1 when this client is not a spectator or the server published no relay
 * base -- and a spectator without one must not launch, because sending as a
 * player slot is exactly what it must not do. */
int  rnet_lobby_local_wire_slot(void);

/* ---- lobby chat --------------------------------------------------------
 * Spectators take part: the server fans a line out to everyone in the room,
 * and talking is not affecting the match. */
/* Report somebody's chat line to the server for moderation.
 *
 * `mids` are RNetLobbyChatMsg.mid values -- several at once, because
 * harassment is usually a burst rather than a line. An entry with an empty id
 * is skipped; a line with no id cannot be reported at all. `reason` is one of
 * RNET_REPORT_* (anything unrecognised is filed as "other" rather than
 * refused); `note` is an optional sentence for whoever reads the queue.
 *
 * The text is never sent: see RNetLobbyChatMsg.mid.
 *
 * Returns 0 if the report was handed to the server. That is not the same as
 * accepted -- the server refuses a line that has scrolled out of its ring
 * ("message_expired"), one from a signed-out sender, one that is your own, and
 * anything over the per-account rate limit. */
/* Categories live in recomp_net/chat_report.h (RNET_REPORT_*), because they
 * are the same list on every console and the server matches on them. */
int  rnet_lobby_report_chat(const char *const *mids, int mid_count,
                            const char *reason, const char *note);

int  rnet_lobby_send_chat(const char *text);

/* Server chat: per-game, outside any room (op server_chat). Its own ring,
 * cleared on disconnect; lines are masked on arrival like lobby chat. */
int  rnet_lobby_send_server_chat(const char *text);
int  rnet_lobby_server_chat_count(void);
int  rnet_lobby_server_chat_get(int index, RNetLobbyChatMsg *out);

/* Seat self-service (server ops seat_move / seat_swap_request /
 * seat_swap_answer): move yourself to a FREE player seat, or ask the
 * occupant of a taken one to trade. incoming: 1 while somebody is asking
 * this player (who + their seat); respond answers it. outgoing: 0 idle,
 * 1 waiting, 2 accepted, -1 declined; clear returns a finished result to 0. */
int  rnet_lobby_seat_move_self(int to_slot);
int  rnet_lobby_seat_swap_request(int target_slot);
int  rnet_lobby_seat_swap_incoming(char *who, size_t who_cap, int *from_slot);
int  rnet_lobby_seat_swap_respond(int accept);
int  rnet_lobby_seat_swap_outgoing(void);
void rnet_lobby_seat_swap_clear(void);
int  rnet_lobby_chat_count(void);
int  rnet_lobby_chat_get(int index, RNetLobbyChatMsg *out);
void rnet_lobby_chat_clear(void);

/* Host: remove the player seated in `slot` (not the host). Returns 0 if sent. */
int  rnet_lobby_kick(int slot);

/* Host: swap (or move into empty) seats. Returns 0 if sent; server broadcasts
 * lobby_update. LAN file-registry hosts handle seat flips in the game callback. */
int  rnet_lobby_move(int from_slot, int to_slot);

int  rnet_lobby_in_lobby(void);
int  rnet_lobby_is_host(void);
/* Lobby host's player_id (stable across seat swaps); empty if unknown. */
const char *rnet_lobby_host_player_id(void);
/* Filled after create/join/lobby_update; peer endpoints for PsxNetplayConfig. */
const RNetLobbyJoinInfo *rnet_lobby_join_info(void);

/*
 * Host relay (recomp_net/host_relay.h), driven from rnet_lobby_pump while
 * seated online. The host's preference: 1 publishes match_caps.relay =
 * "host", holds the game port while the room waits (UPnP / NAT-PMP / STUN),
 * advertises it with set_host_endpoint and answers probes; every guest in a
 * room that asks probes the advertised endpoint and sends path_report. The
 * default is ON: the lobby server's relay carries a match only when a guest
 * cannot reach the host. Applies to rooms this client creates from now on.
 */
void rnet_lobby_set_relay_host_pref(int on);
int  rnet_lobby_relay_host_pref(void);
/*
 * Host relay over ICE (recomp_net/host_ice.h). The host's preference, default
 * ON: with relay_host, publish match_caps.relay_via = "ice" (only when this
 * build has ICE; otherwise the legacy advertised-port relay is used). In that
 * room the host holds NO port (no set_host_endpoint, no UPnP / NAT-PMP / STUN):
 * every seated guest proves the path with an ICE agent pair and reports
 * path_report direct. Also sent as "ice_relay": true in automatch_queue.
 */
void rnet_lobby_set_relay_via_ice(int on);
int  rnet_lobby_relay_via_ice(void);
/* Live state of the waiting-room agents (RNetHostIceStatus, host_ice.h).
 * Returns 1 when the room runs them, 0 when idle. */
struct RNetHostIceStatus;
int  rnet_lobby_host_ice_status(struct RNetHostIceStatus *out);

/*
 * Launch handover for transport_ice_hub (see RNetLobbyJoinInfo). The agents
 * are captured by the client when `launch` arrives, and are the engine's to
 * take once -- BEFORE rnet_lobby_clear_launch_pending(), though an untaken
 * bundle is kept for 60 s, until the next launch, or until leave/disconnect.
 *
 * HOST: fills out[0..n) and returns n >= 1; ownership of each agent passes to
 * the caller, who must pass it to rnet_session_start_ice_hub_adopt with the
 * SESSION slot it maps lobby_slot to (this library does not know the engine's
 * slot mapping, host_spectates offset, etc.). Returns -1 when no handover is
 * pending or max is too small; see rnet_lobby_ice_launch_error.
 * GUEST: returns the one agent connected to the host (ownership to the
 * caller: rnet_session_adopt_ice_agent), or NULL.
 * A launch in which any seated guest (or the guest's own link to the host) is
 * not COMPLETED is refused by the client itself: launch_pending stays 0,
 * join.last_error = "ice_not_connected" and rnet_lobby_ice_launch_error()
 * names the seat -- never a silently smaller room.
 */
typedef struct RNetLobbyIceSeat {
    int  lobby_slot;                       /* the guest's seat in the lobby */
    char player_id[RNET_LOBBY_ID_LEN];
    struct RNetIceAgent *agent;            /* COMPLETED, opaque, owned by caller */
} RNetLobbyIceSeat;
int  rnet_lobby_ice_take_hub(RNetLobbyIceSeat *out, int max);
struct RNetIceAgent *rnet_lobby_ice_take_guest_agent(void);
/* Why the last ICE launch was refused or a take failed ("" when none). */
const char *rnet_lobby_ice_launch_error(void);
/* Destroy an untaken bundle now (the engine will not start this match). */
void rnet_lobby_ice_discard(void);

/* Live state for the waiting room (see RNetHostRelayStatus). Returns 1 when
 * the orchestration is doing anything (host or guest role), 0 when idle. */
struct RNetHostRelayStatus;
int  rnet_lobby_host_relay_status(struct RNetHostRelayStatus *out);
/* The game is about to bind the host port: close the waiting-room socket and
 * keep the router mapping. Called by the client itself on `launch`; exposed
 * for a host layer that binds earlier. */
void rnet_lobby_host_relay_release_port(void);

/* Latest host match_caps (valid==0 until create/join/launch delivers one). */
const RNetLobbyMatchCaps *rnet_lobby_match_caps(void);

/* Host: push updated caps while in lobby (clears ready via lobby_update). */
int  rnet_lobby_set_match_caps(const RNetLobbyMatchCaps *caps);

/* Live member table from lobby_update (and create/join). */
int  rnet_lobby_member_count(void);
int  rnet_lobby_member_get(int index, RNetLobbyMember *out);

/* Waiting-room RTT to the lobby host in ms for `slot`, or -1 if unknown.
 * Host's own seat is always -1. Guests measure via signal ping; hosts learn
 * guest RTT from peer reports. */
int  rnet_lobby_member_latency_ms(int slot);

/* True when member.player_id matches rnet_lobby_host_player_id().
 * Prefer this over `slot == 0` — seats can move. */
int  rnet_lobby_member_is_host(const RNetLobbyMember *member);

/* Local ready flag (from last lobby_update matching our player_id). */
int  rnet_lobby_local_ready(void);
/* True when every seated player is ready and player_count >= 2. */
int  rnet_lobby_all_ready(void);

/* Toggle ready in the current lobby. */
int  rnet_lobby_set_ready(int ready);

/* This peer cannot launch at all right now -- it has no verified copy of the
 * game to boot. While blocked, every set_ready this client sends says false
 * (the join and lobby_update auto-arm included), so the host's launch gate
 * holds the match and names this player until the block lifts; lifting it
 * re-announces ready. A local fact, not per-connection state: it survives a
 * disconnect. The launcher sets it from the same check that enables PLAY. */
#define RNET_HAS_LAUNCH_BLOCKED 1
void rnet_lobby_set_launch_blocked(int blocked);

/* Seated players, other than this one, whose last set_ready said false. Names
 * the first. The host's start gate: a peer that cannot boot the game must not
 * be launched into a match it would never join. */
int  rnet_lobby_match_blocked_by_unready(char *who, size_t who_cap);

/*
 * Host: ask server to broadcast launch. When match_caps is non-NULL and valid,
 * it is attached to start so launch freezes the latest host settings.
 */
int  rnet_lobby_request_start(const RNetLobbyMatchCaps *match_caps);

/*
 * Set when server sends op:launch. Both host and guests should boot netplay.
 * Cleared by rnet_lobby_clear_launch_pending() after consuming.
 */
int  rnet_lobby_launch_pending(void);
void rnet_lobby_clear_launch_pending(void);
void rnet_lobby_clear_last_error(void);

/* After op:launch: copy seating endpoints into *out when launch_pending and
 * bind/peer are usable. Returns 1 if filled. Does not clear launch_pending.
 * Games wire this from RecompLauncherCNetplayCallbacks.fill_launch. */
int  rnet_lobby_try_fill_launch(RNetLobbyJoinInfo *out);

/*
 * ICE signaling relay (MotK WS op:signal). text is SDP/candidate (max 2047).
 * send returns 0 if queued/written; poll returns 1 when an inbound signal was
 * copied out (LOCAL_* types as emitted by the peer — remap to REMOTE_* before
 * rnet_session_push_signal).
 */
/* Addressed variant. An empty/NULL to_player_id broadcasts to the other
 * seated members, which is what the game's own ICE wants; a point-to-point
 * exchange must name its peer. */
int  rnet_lobby_send_signal_to(const char *to_player_id, int type, int flag,
                              const char *text);
int  rnet_lobby_send_signal(int type, int flag, const char *text);

/* ---- peer-to-peer mod transfer -----------------------------------------
 *
 * The package travels over its own ICE agent, directly between the two
 * players. The lobby server relays only SDP and candidate lines, on the same
 * seated `signal` channel the room already uses -- no file byte passes
 * through it, and it needs no support for any of this.
 *
 * Hooks rather than direct calls into the mod runtime: the lobby transports
 * package identity and bytes, and knows nothing about what a package is.
 * export writes a self-contained archive plus its digest; install is handed
 * that digest and must verify BEFORE unpacking. */
typedef int (*RNetLobbyModExportFn)(const char *package_id, const char *version,
                                    uint8_t **out, uint32_t *out_len,
                                    char *sha256_hex, uint32_t sha_cap,
                                    char *err, uint32_t err_cap, void *ctx);
typedef void (*RNetLobbyModFreeFn)(uint8_t *blob);
typedef int (*RNetLobbyModInstallFn)(const uint8_t *data, uint32_t len,
                                     const char *expect_sha256,
                                     char *installed_id, uint32_t id_cap,
                                     char *installed_ver, uint32_t ver_cap,
                                     char *err, uint32_t err_cap, void *ctx);
void rnet_lobby_set_mod_transfer_hooks(RNetLobbyModExportFn export_fn,
                                       RNetLobbyModFreeFn free_fn,
                                       RNetLobbyModInstallFn install_fn,
                                       void *ctx);

/*
 * A relayed transfer spends the TURN server's bandwidth, not the two players'.
 * A direct pair (host/srflx/prflx) costs nobody anything and is left uncapped;
 * a relayed pair is capped, and a package over the cap is refused with a
 * reason that tells the player to get it from its source instead.
 *
 * Bytes, not megabytes, so the check needs no unit conversion at the call
 * site: 5 MiB.
 */
#define RNET_LOBBY_MOD_RELAY_MAX_BYTES (5u * 1024u * 1024u)

/*
 * May a transfer of `bytes` proceed over an ICE pair of type `path`?
 *
 * `path` is what rnet_ice_xfer_path reports: "host", "srflx", "prflx",
 * "relay", "unknown" or "none". Only "relay" is capped. Returns 1 to allow,
 * 0 to refuse -- and on a refusal writes a player-facing sentence naming the
 * package, its size and the cap.
 *
 * An unknown path ALLOWS. The alternative is refusing transfers on a direct
 * pair we merely failed to name, which breaks the case this cap exists to
 * protect. Callers gate on knowing the path first; see the pump.
 *
 * Pure: no globals, no I/O. Both ends call it -- the host before it queues
 * anything, the guest when the header says how big the payload will be -- so
 * a peer that skips the check on its side still cannot land an oversized
 * relayed package on the other.
 */
int  rnet_lobby_mod_relay_size_allows(const char *path, uint64_t bytes,
                                      const char *package_id,
                                      char *reason, size_t reason_cap);

/* Guest: ask the host for one package. 0 = asked. */
int  rnet_lobby_mod_request(const char *package_id, const char *version);
void rnet_lobby_mod_cancel(void);
/* -1 idle, -2 failed, 0..100 in flight. */
int  rnet_lobby_mod_progress(void);
int  rnet_lobby_mod_failed(char *err, size_t err_cap);
/* Package id currently moving, or "" when idle. */
const char *rnet_lobby_mod_in_flight(void);
/* Pumped from rnet_lobby_pump; exposed for hosts that drive their own loop. */
void rnet_lobby_mod_xfer_pump(void);
int  rnet_lobby_poll_signal(int *type, int *flag, char *text, size_t text_cap);

/*
 * Coturn / ICE credentials minted by the WS lobby
 * (`get_turn_credentials` → `turn_credentials`). Valid until disconnect or TTL.
 * Strings are stable until the next successful mint or disconnect — safe to
 * pass into RNetIceConfig for juice_create.
 */
typedef struct RNetLobbyTurnCredentials {
    int      valid; /* 1 when ok mint cached and not expired */
    char     stun_host[128];
    int      stun_port;
    char     turn_host[128];
    int      turn_port;
    char     username[192];
    char     password[128];
    uint32_t ttl_secs;
} RNetLobbyTurnCredentials;

/* Queue WS get_turn_credentials. Returns 0 if sent/queued. */
int  rnet_lobby_request_turn_credentials(void);
/* Non-NULL; valid==0 when unavailable / expired / STUN-only. */
const RNetLobbyTurnCredentials *rnet_lobby_turn_credentials(void);

/* ── ROM fingerprint ────────────────────────────────────────────────────────
 *
 * Lower-case hex SHA-256 of the guest image this build is actually running,
 * 64 characters. The host sets it once at startup; "" means unknown.
 *
 * In `join` an empty fingerprint means "legacy host, no check" -- a wildcard.
 * That is tolerable between two people who chose each other's room and can
 * talk about it. It is NOT tolerable in a queue: a wildcard there silently
 * pairs a Track-01-only dump against a full one, which is exactly what the
 * fingerprint was added to catch, so automatch refuses to queue without a
 * real one.
 */
void rnet_lobby_set_disc_fp(const char *hex64);
const char *rnet_lobby_disc_fp(void);

/* ── Automatch ──────────────────────────────────────────────────────────────
 *
 * Server-run pairing: the player asks for a match and the SERVER creates the
 * room, is its host, and owns its match_caps from a named ruleset. Protocol:
 * recomp-net-server docs/AUTOMATCH.md. Everything downstream of the pairing is
 * the ordinary `joined` / `lobby_update` / `launch` path already in this file,
 * so this adds a queue and an accept gate and nothing else.
 *
 * Requires a signed-in account: the accept gate charges a dodge cooldown, and
 * a cost that reconnecting erases is not a cost.
 */
#define RNET_LOBBY_MAX_RULESETS 8
#define RNET_LOBBY_RULESET_ID_LEN 48
#define RNET_LOBBY_RULESET_LABEL_LEN 64
#define RNET_LOBBY_CAPS_SUMMARY_LEN 96

/* One queue type this server offers for this title. */
typedef struct RNetLobbyRuleset {
    char id[RNET_LOBBY_RULESET_ID_LEN];
    char label[RNET_LOBBY_RULESET_LABEL_LEN];
    /* Server-derived one-liner ("Delay 2 - Rollback on"). Derived from the
     * caps rather than authored, so it cannot drift from what the match runs. */
    char caps_summary[RNET_LOBBY_CAPS_SUMMARY_LEN];
    /* The caps the match will run. Parsed like a host's blob, because that is
     * exactly what it is -- the server is the host of an automatch room. */
    RNetLobbyMatchCaps caps;
    /* Empty = any release may queue (still only pooling with its own). */
    char game_version[RNET_LOBBY_VERSION_LEN];
} RNetLobbyRuleset;

/* Matches RECOMP_LAUNCHER_AUTOMATCH_* so the host layer can hand these
 * straight to the launcher without a translation table that could drift. */
enum {
    RNET_LOBBY_AUTOMATCH_IDLE = 0,
    RNET_LOBBY_AUTOMATCH_QUEUED = 1,
    RNET_LOBBY_AUTOMATCH_FOUND = 2,
    RNET_LOBBY_AUTOMATCH_ACCEPTED = 3,
    RNET_LOBBY_AUTOMATCH_FAILED = 4
};

/* The offer on the table while state is FOUND. */
typedef struct RNetLobbyAutomatchFound {
    char opponent[RNET_LOBBY_NAME_LEN];
    char opponent_country[8];
    char ruleset_id[RNET_LOBBY_RULESET_ID_LEN];
    char ruleset_label[RNET_LOBBY_RULESET_LABEL_LEN];
    int  est_rtt_ms;      /* the pair's estimate: rtt_a + rtt_b */
    int  accept_secs;     /* seconds left to answer */
} RNetLobbyAutomatchFound;

/* Ask the server what queues it offers for this title. Answered into
 * rnet_lobby_ruleset_count/get; 0 of them means automatch is off here. */
/*
 * One simulation-state fork, as reported to the server.
 *
 * Evidence, not a verdict: both digests, from both peers independently. See
 * rnet_lobby_report_desync for why a fork is never an accusation on its own.
 */
typedef struct RNetLobbyDesyncReport {
    uint32_t tick;
    const char *partition;   /* "wram", "apu", "ppu", "post", "other" */
    uint32_t mine;           /* local master digest */
    uint32_t theirs;         /* the peer's, as received */
    int is_host;
    /* The cosmetic exemptions this peer was running, so a report can be read
     * beside what the match approved. ';'-separated `id@version#sha256`. */
    const char *mod_exempt;
} RNetLobbyDesyncReport;

int  rnet_lobby_report_desync(const RNetLobbyDesyncReport *r);

/*
 * Tell the server which accounts this player has blocked.
 *
 * ';'-separated opaque account ids, replacing the whole set -- the client's
 * own file is the authority, so an unblock needs no separate op.
 *
 * Sent to the SERVER rather than applied only here, because two of the three
 * things a block has to do cannot be done from this side: the matchmaker must
 * not pair the two, and the blocked player must not see or be able to join
 * the blocker's room. A client can hide what it was sent; it cannot know it
 * was blocked by somebody else, and that is the direction that matters.
 */
int  rnet_lobby_set_blocks(const char *accounts);

int  rnet_lobby_automatch_request_rulesets(void);
int  rnet_lobby_automatch_available(void);
int  rnet_lobby_automatch_ruleset_count(void);
int  rnet_lobby_automatch_ruleset_get(int index, RNetLobbyRuleset *out);

/*
 * Join the queue. `ruleset_id` NULL/"" takes the first one.
 *
 * `mods_enabled` is the caller's assertion that a SIM-AFFECTING mod feature is
 * on locally, beyond whatever the ruleset itself imposes. The server refuses a
 * true with mods_not_pooled and cannot check it -- the assertion exists so a
 * modified client makes a deliberate false statement rather than exploiting an
 * omission (AUTOMATCH.md §5). The host layer decides it; this file only
 * carries it.
 *
 * 0 = sent. <0 = refused locally (not connected, no account, already queued).
 * A server refusal arrives asynchronously as state FAILED with a reason in
 * rnet_lobby_automatch_error().
 */
/*
 * `mod_exempt` is the EVIDENCE for every cosmetic exemption this build is
 * taking: ';'-separated `id@version#sha256`, from
 * the engine's mod runtime. NULL or "" means none.
 *
 * It is sent so the SERVER can check those exemptions against its own
 * allowlist. `mods_enabled` beside it is still this client's own verdict and
 * still refused when true -- the two are layers, not alternatives: an old
 * server ignores the evidence and the boolean keeps working, a new server
 * checks the evidence and stops trusting the boolean to be the whole story.
 */
int  rnet_lobby_automatch_queue(const char *ruleset_id, int mods_enabled,
                                const char *mod_exempt);
int  rnet_lobby_automatch_cancel(void);
int  rnet_lobby_automatch_state(void);
int  rnet_lobby_automatch_queued_secs(void);
int  rnet_lobby_automatch_pool(void);
int  rnet_lobby_automatch_found_get(RNetLobbyAutomatchFound *out);
/* accept != 0 accepts; 0 declines and takes the dodge strike. */
int  rnet_lobby_automatch_accept(int accept);
/* Refuse a queue locally, with the reason the player sees. Used when the HOST
 * already knows the server would bounce the ticket -- there is no value in a
 * round trip that ends in a generic code when the client can name the actual
 * feature that is in the way. */
/*
 * Non-zero while the room this client is seated in was created by AUTOMATCH.
 *
 * The distinction matters on the way OUT of a match. A human-hosted room is
 * somebody's room: it survives the match, and staying seated is how a rematch
 * happens. An automatch room is the server's, it is created at both-accept
 * and is not joinable by anyone, and there is no host to rematch with -- so
 * staying in it leaves the player parked in a room that can never fill, and
 * the server refuses their next ticket with `already_in_lobby`.
 *
 * Cleared by leaving, and by anything else that ends the seating.
 */
int rnet_lobby_automatch_room(void);
void rnet_lobby_automatch_refuse_local(const char *why);
const char *rnet_lobby_automatch_error(void);

#ifdef __cplusplus
}
#endif

#endif /* RNET_LOBBY_CLIENT_H */
