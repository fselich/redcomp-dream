#ifndef RECOMP_NET_HOST_ICE_H
#define RECOMP_NET_HOST_ICE_H

/*
 * Host relay over ICE: the waiting-room proof.
 *
 * The host carries the match for its room (it is the hub; see
 * rnet_session_start_ice_hub). Before the match, each guest has to PROVE it
 * can reach the host, and the proof is the same thing the match will use: a
 * real ICE agent pair.
 *
 *   HOST   one answerer agent per currently seated guest. Agents are added
 *          and removed as members join / leave / move seats, and each binds
 *          an ephemeral port (never a fixed one: N agents cannot share it).
 *   GUEST  one offerer agent whose peer is the host.
 *
 * Roles are fixed: the host answers, guests offer. No TURN and no
 * force-relay -- a hub agent that cannot connect directly is a failure, and
 * the lobby server's relay carries the match instead.
 *
 * Signalling rides the lobby `signal` op addressed with to_player_id, in its
 * own type range so it cannot collide with the legacy game ICE (1-6), the
 * RTT probe (100-102), or mod transfer (110/111, 121-126):
 *
 *     wire type = RNET_LOBBY_SIG_HOSTICE_BASE + RNetSignalType      (131..135)
 *
 * `flag` carries the negotiation id (1..255) the offerer chose for its
 * current agent. The answerer rebinds to a new id (a fresh agent) whenever an
 * SDP arrives with one it has not seen, and drops every signal stamped with a
 * stale id, so a guest that moves seats or retries after a failure cannot be
 * poisoned by its own earlier exchange.
 *
 * When a guest's agent reaches COMPLETED it sends the lobby
 * {"op":"path_report","path":"direct"[,"ice":"host|srflx|prflx"]} (the op the
 * server already accepts), refreshed every 45 s; on FAILED, path "fail", and
 * it retries with a fresh negotiation after 20 s. Every agent is FROZEN at
 * COMPLETED: no later SDP or candidate may rebuild a live link.
 *
 * At launch the lobby client hands the COMPLETED agents to the engine, which
 * adopts them into its session with rnet_session_start_ice_hub_adopt /
 * rnet_session_adopt_ice_agent (nothing is renegotiated). See
 * rnet_lobby_ice_take_hub / rnet_lobby_ice_take_guest_agent.
 *
 * Environment (test aids, so a loopback test can drive the real lobby client):
 *   RNET_HOST_ICE_BIND       bind every agent to this address (e.g. 127.0.0.1)
 *   RNET_HOST_ICE_NO_STUN    "1": no STUN server (host candidates only)
 *
 * Pump-driven like host_relay.h: no threads here, nothing blocks. Needs
 * RNET_ENABLE_ICE; without it rnet_host_ice_available() is 0, no agent is
 * ever created, and a guest reports path "fail" honestly.
 */

#include "recomp_net/ice.h"
#include "recomp_net/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lobby `signal` types for this channel: BASE + RNetSignalType. */
#define RNET_LOBBY_SIG_HOSTICE_BASE 130
#define RNET_HOST_ICE_MAX_PEERS 8
#define RNET_HOST_ICE_ID_LEN 64

typedef struct RNetHostIce RNetHostIce;

/* A peer an agent serves: its lobby player id and the seat it holds. */
typedef struct RNetHostIcePeer {
    const char *player_id;
    int slot;
} RNetHostIcePeer;

/* What the lobby client knows this pump. Strings are borrowed for the call. */
typedef struct RNetHostIceView {
    int active;      /* online room, seated, relay_via "ice", before launch */
    int is_host;
    int local_slot;  /* this client's own seat (a guest rebuilds when it moves) */
    /* HOST: every seated, non-spectator guest. GUEST: exactly one entry, the
     * host (player id + the seat it holds). */
    const RNetHostIcePeer *peers;
    int peer_count;
    /* NULL = the library default STUN; "" = none (loopback tests). */
    const char *stun_host;
    unsigned short stun_port;   /* 0 = 3478 */
    const char *bind_address;   /* NULL = any (test aid) */
    /* Send one lobby `signal` addressed to to_player_id. 0 = sent / queued. */
    int (*send_signal)(const char *to_player_id, int type, int flag, const char *text, void *ctx);
    /* Send a complete lobby op as JSON (path_report). 0 = sent / queued. */
    int (*send_json)(const char *json, void *ctx);
    void *ctx;
} RNetHostIceView;

typedef struct RNetHostIcePeerStatus {
    char player_id[RNET_HOST_ICE_ID_LEN];
    int  slot;
    int  state;        /* RNetIceState */
    char path[8];      /* host|srflx|prflx once connected, else "" */
    int  negotiation;  /* current negotiation id (0 = host not yet offered to) */
} RNetHostIcePeerStatus;

typedef struct RNetHostIceStatus {
    int role;                  /* 0 idle, 1 host, 2 guest */
    int peer_count;
    int completed;             /* peers whose agent is COMPLETED */
    RNetHostIcePeerStatus peer[RNET_HOST_ICE_MAX_PEERS];
    unsigned held_dropped;     /* early signals refused (hold full) */
    unsigned rejected;         /* signals refused (wrong sender / seat / type) */
    unsigned reports_sent;     /* guest: path_report ops sent */
    char last_report[8];       /* guest: "direct" | "fail" | "" */
} RNetHostIceStatus;

/* 1 when this build can run ICE agents (RNET_ENABLE_ICE). */
int rnet_host_ice_available(void);

RNetHostIce *rnet_host_ice_create(void);
void rnet_host_ice_destroy(RNetHostIce **h);

/* Every lobby pump: reconcile the agents with the view, poll them, send
 * signals and path reports. An inactive view destroys every agent. */
void rnet_host_ice_update(RNetHostIce *h, const RNetHostIceView *v);

/* 1 when `type` is in this module's lobby signal range (BASE+1..BASE+6). */
int rnet_host_ice_sig_is_ours(int wire_type);

/* One inbound lobby signal. The caller has already established that the
 * sender is seated (from_slot >= 0) and not a spectator. Returns 0 when it
 * was applied or held for a peer whose agent does not exist yet, -1 when it
 * was refused (module idle, sender is not a peer we serve, a guest hearing
 * from anyone but the host, SET_CONTROLLING, or a bad type). A signal from a
 * seat other than the one the agent serves is held, never applied: it is kept
 * only until the agent for that seat exists. */
int rnet_host_ice_push_signal(RNetHostIce *h, const char *from_player_id, int from_slot,
                              int wire_type, int flag, const char *text);

void rnet_host_ice_status(const RNetHostIce *h, RNetHostIceStatus *out);
/* Round trip in ms over the linked agent to the peer in lobby seat `slot`
 * (host: a guest's seat; guest: the host's seat), or -1 when there is no
 * linked agent or no reply yet. Excludes both ends' pump delay. */
int rnet_host_ice_peer_rtt_ms(const RNetHostIce *h, int slot);
/* Held (not yet delivered) signals for a peer; 0 when none. */
int rnet_host_ice_held_count(const RNetHostIce *h, const char *player_id);

/* Hand the agent for `player_id` to the caller iff it is COMPLETED. The module
 * forgets it (and stops polling it); the caller must give it to a session or
 * destroy it with rnet_host_ice_destroy_agent. NULL when there is no such
 * peer or it is not COMPLETED -- never a half-connected agent. */
RNetIceAgent *rnet_host_ice_take_completed(RNetHostIce *h, const char *player_id);
/* 1 when the agent for player_id exists and is COMPLETED. */
int rnet_host_ice_peer_completed(const RNetHostIce *h, const char *player_id);
/* Release an agent the caller took and will not hand to a session. */
void rnet_host_ice_destroy_agent(RNetIceAgent *agent);

#ifdef __cplusplus
}
#endif

#endif /* RECOMP_NET_HOST_ICE_H */
