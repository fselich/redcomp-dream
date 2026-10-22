#ifndef RECOMP_NET_HOST_RELAY_H
#define RECOMP_NET_HOST_RELAY_H

/*
 * Host relay: the host of an online room carries its own match instead of
 * the lobby server's UDP relay (the "SFU"). recomp-net-server WS_LOBBY.md,
 * "Host relay": the room asks with match_caps.relay = "host"; the host
 * advertises a reachable UDP endpoint with set_host_endpoint; each guest
 * probes it and reports path_report direct|fail; at start the server
 * launches transport "host" only when every guest's report is a fresh
 * "direct", otherwise its relay carries the match, so a match always
 * connects. In the match the host runs rnet_session_start_lan_hub (3+
 * seats) or accepts the one guest (2 seats); guests dial host_endpoint.
 *
 * This file is the client half, in three layers, all pump-driven from the
 * lobby pump (no threads, nothing blocks longer than one socket call):
 *
 *   RNetHostPort   -- the host side. Holds the game's UDP port while the
 *                     room waits and makes it reachable: UPnP IGD
 *                     (AddPortMapping), else NAT-PMP, else the public
 *                     mapping STUN reports (reachable only through a
 *                     forwarded port or an open NAT). Answers guests'
 *                     probes on that port from the moment it binds.
 *   RNetHostProbe  -- the guest side. Probes the advertised endpoint.
 *   RNetHostRelay  -- the orchestration a lobby client runs every pump:
 *                     given the room's view (role, bind port, the host
 *                     endpoint the server published) it opens/pumps the
 *                     port or the probe and emits the two lobby ops.
 *
 * It was retcomm-launcher's netplay_nat.cpp (C++17, POSIX, libcurl,
 * threads); this is the C11 port recomp-ui and every engine's lobby
 * client can share, on Windows too. The probe wire is the hub's:
 * "RETRO_HOSTPROBE1 <nonce>" answered by "RETRO_HOSTPROBE1_ACK <nonce>".
 *
 * Environment (test aids, documented so nobody rediscovers them):
 *   RNET_HOST_RELAY_ENDPOINT   advertise this "ip:port" without asking any
 *                              router or STUN server (loopback tests)
 *   RNET_HOST_RELAY_NO_ROUTER  "1": skip UPnP and NAT-PMP (nothing is asked
 *                              of the real router; STUN only)
 */

#include "recomp_net/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RNET_HOST_RELAY_ENDPOINT_LEN 64
#define RNET_HOST_PROBE_MAGIC "RETRO_HOSTPROBE1 "
#define RNET_HOST_PROBE_ACK_MAGIC "RETRO_HOSTPROBE1_ACK "

/* ---- the host side: RNetHostPort --------------------------------------- */

typedef struct RNetHostPort RNetHostPort;

typedef struct RNetHostPortStatus {
    int   done;                 /* discovery finished (a result or none) */
    char  endpoint[RNET_HOST_RELAY_ENDPOINT_LEN]; /* public "ip:port"; "" = none */
    char  how[12];              /* "upnp" | "nat-pmp" | "stun" | "env" | "" */
    char  stage[32];            /* what is running now, for the room */
    char  detail[200];          /* one sentence for the room */
    unsigned short local_port;  /* the UDP port bound */
    unsigned probes_answered;
} RNetHostPortStatus;

/* Bind UDP `port` (0 = the first free of 7777..7808) and start working its
 * reachability out across later pumps. stun_hostport NULL = the default.
 * try_router 0 skips UPnP and NAT-PMP. Returns 0 and *out on success, <0
 * when the port cannot be bound (the status detail says which). */
int  rnet_host_port_open(RNetHostPort **out, unsigned short port,
                         const char *stun_hostport, int try_router);
/* One non-blocking step: answer probes, advance discovery. */
void rnet_host_port_pump(RNetHostPort *hp);
void rnet_host_port_status(const RNetHostPort *hp, RNetHostPortStatus *out);
/* Close the socket so the game can bind the port. A UPnP / NAT-PMP mapping
 * this made stays (the game inherits it). */
void rnet_host_port_release(RNetHostPort **hp);
/* Remove a mapping this made (leaving the room, quitting). Bounded: at most
 * about 600 ms, best effort. Then release. */
void rnet_host_port_unmap_release(RNetHostPort **hp);

/* ---- the guest side: RNetHostProbe ------------------------------------- */

typedef struct RNetHostProbe RNetHostProbe;

/* Ephemeral UDP socket; `tries` probes about 400 ms apart (0 = 6). */
int  rnet_host_probe_open(RNetHostProbe **out, const char *endpoint, int tries);
/* 1 = the host answered, 0 = still trying, -1 = every try went unanswered. */
int  rnet_host_probe_pump(RNetHostProbe *p);
void rnet_host_probe_close(RNetHostProbe **p);

/* ---- the orchestration: RNetHostRelay ---------------------------------- */

typedef struct RNetHostRelay RNetHostRelay;

/* What the lobby client knows this pump. Strings may be NULL. */
typedef struct RNetHostRelayView {
    int active;          /* online room, seated, and the room asks for host relay */
    int is_host;
    unsigned short bind_port;   /* host: the game port to hold and advertise */
    const char *host_endpoint;  /* guest: the host's advertised endpoint (lobby_update) */
    const char *stun_hostport;  /* NULL = default */
    /* The two lobby ops, as complete JSON texts: {"op":"set_host_endpoint",...}
     * and {"op":"path_report",...}. Return 0 when sent / queued. */
    int (*send_json)(const char *json, void *ctx);
    void *ctx;
} RNetHostRelayView;

typedef struct RNetHostRelayStatus {
    int role;                   /* 0 idle, 1 host, 2 guest */
    RNetHostPortStatus port;    /* host */
    char advertised[RNET_HOST_RELAY_ENDPOINT_LEN]; /* host: last set_host_endpoint */
    int  probing;               /* guest: a probe is in flight */
    char probed[RNET_HOST_RELAY_ENDPOINT_LEN];     /* guest: endpoint under probe / reported */
    char last_report[8];        /* guest: "direct" | "fail" | "" */
    unsigned reports_sent;
} RNetHostRelayStatus;

RNetHostRelay *rnet_host_relay_create(void);
void rnet_host_relay_destroy(RNetHostRelay **hr);
/* Every lobby pump. Opens / pumps / closes the port or the probe to match
 * the view, and sends an op when something to say changed (or every 45 s
 * for a guest, since the server trusts a report for 120 s). */
void rnet_host_relay_update(RNetHostRelay *hr, const RNetHostRelayView *view);
/* Before the game binds the port (launch): close the socket, keep the mapping. */
void rnet_host_relay_release_port(RNetHostRelay *hr);
/* Guest: the server forgot this seat's path report. It clears every report on
 * any roster change (a join, a leave, a seat move) without telling the guests,
 * so the lobby client calls this when a lobby_update shows our own seat with
 * no path. A report already sent is proven again at once instead of at the
 * next refresh, which would leave the host's Start refused for up to 45 s
 * after the last player joined. No-op for a host, an idle relay, a probe in
 * flight or a guest that has not reported yet. */
void rnet_host_relay_path_cleared(RNetHostRelay *hr);
/* Leaving the room / disconnecting: unmap, close, forget. */
void rnet_host_relay_leave(RNetHostRelay *hr);
void rnet_host_relay_status(const RNetHostRelay *hr, RNetHostRelayStatus *out);

/* ---- pure encoders and parsers (unit-tested) --------------------------- */

/* The LOCATION header of an SSDP response. Returns 1 when found. */
int rnet_hr_ssdp_location(const char *response, char *out, size_t cap);
/* The text of <tag> (namespaced or not) in xml. Returns 1 when found. */
int rnet_hr_xml_text(const char *xml, const char *tag, char *out, size_t cap);
/* `ref` made absolute against `base` (scheme://host[:port]/path). */
int rnet_hr_absolute_url(const char *ref, const char *base, char *out, size_t cap);
/* The WANIPConnection (2, then 1) or WANPPPConnection:1 control URL from a
 * device description, absolute against location (URLBase when given). */
int rnet_hr_igd_control_url(const char *xml, const char *location,
                            char *control_url, size_t cap,
                            char *service_type, size_t type_cap);
/* SOAP envelope for `action` with kv[0],kv[1],... (name, value) pairs. */
int rnet_hr_soap_body(const char *service_type, const char *action,
                      const char *const *kv, int pairs, char *out, size_t cap);
/* NAT-PMP (RFC 6886) UDP mapping request / responses. */
void rnet_hr_natpmp_mapping_request(rnet_u8 out[12], unsigned short internal_port,
                                    unsigned short external_port, rnet_u32 lifetime_s);
int rnet_hr_natpmp_parse_mapping(const rnet_u8 *resp, size_t len,
                                 unsigned short *external_port, unsigned short *result_code);
int rnet_hr_natpmp_parse_address(const rnet_u8 *resp, size_t len, char *ip, size_t cap);
/* The default gateway from /proc/net/route text (Linux). */
int rnet_hr_gateway_from_route(const char *proc_net_route, char *out, size_t cap);
/* Split a raw HTTP/1.x response: status code and the body start/length. */
int rnet_hr_http_split(const char *raw, size_t len, int *status,
                       const char **body, size_t *body_len);
/* The default gateway of this machine, by the OS's route table. */
int rnet_hr_default_gateway(char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* RECOMP_NET_HOST_RELAY_H */
