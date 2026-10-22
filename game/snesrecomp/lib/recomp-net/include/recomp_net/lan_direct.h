#ifndef RNET_LAN_DIRECT_H
#define RNET_LAN_DIRECT_H

#include "recomp_net/lan_lobby.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UDP waiting-room seat claim for LAN/Direct IP across machines.
 *
 * Host binds the advertised game UDP port while the lobby is open, answers
 * JOIN_REQ, and later notifies START. Guest Join Direct sends JOIN_REQ to the
 * typed host IP:port (public IP + port-forward, or LAN IP). Same-machine play
 * can still use the file registry (rnet_lan_lobby); this path is for remote.
 *
 * Wire format is a small text datagram (RNETDJ1). Not encrypted. */

enum {
    RNET_LAN_DIRECT_PENDING = 1, /* join_poll: no answer yet, keep polling */
    RNET_LAN_DIRECT_OK = 0,
    RNET_LAN_DIRECT_ERR_IO = -1,
    RNET_LAN_DIRECT_ERR_FULL = -2,
    RNET_LAN_DIRECT_ERR_PASSWORD = -3,
    RNET_LAN_DIRECT_ERR_IDENTITY = -4,
    RNET_LAN_DIRECT_ERR_STARTED = -5,
    RNET_LAN_DIRECT_ERR_TIMEOUT = -6,
    RNET_LAN_DIRECT_ERR_ARGUMENT = -7
};

typedef struct RNetLanDirectHost RNetLanDirectHost;
typedef struct RNetLanDirectGuest RNetLanDirectGuest;

/* ---- lobby chat ----------------------------------------------------------
 *
 * The HOST is the authority, exactly as the lobby server is online. A guest
 * sends its line to the host and appends nothing locally; the host stamps the
 * sender's seat name on it, keeps it, and echoes it to the guest. So both
 * peers read the same lines in the same order, and neither can show a line
 * the other never got.
 *
 * Received lines queue inside the host/guest handle and are drained with
 * take_chat. A queue rather than a single slot: two lines can land between
 * pumps, and dropping the second would silently lose what somebody said. */
#define RNET_LAN_CHAT_ID_LEN   64
#define RNET_LAN_CHAT_NAME_LEN 64
#define RNET_LAN_CHAT_TEXT_LEN 256
#define RNET_LAN_CHAT_QUEUE    16

typedef struct RNetLanChatLine {
    char player_id[RNET_LAN_CHAT_ID_LEN];
    char from[RNET_LAN_CHAT_NAME_LEN];
    char text[RNET_LAN_CHAT_TEXT_LEN];
} RNetLanChatLine;

/* ---- host (waiting room) ------------------------------------------------- */

/* Bind UDP on bind_hostport (usually 0.0.0.0:<port>). Copies room fields.
 * Returns 0 on success. */
int rnet_lan_direct_host_open(RNetLanDirectHost **out, const char *bind_hostport,
                              const RNetLanLobby *room);

void rnet_lan_direct_host_close(RNetLanDirectHost **host);

/* Non-blocking: process JOIN_REQ / LEAVE / PING/PONG. Updates *room.
 * Returns 1 if seat state changed. When a PONG arrives, *out_rtt_ms (if
 * non-NULL) is set to the measured round-trip milliseconds. */
int rnet_lan_direct_host_pump(RNetLanDirectHost *host, RNetLanLobby *room,
                              int *out_rtt_ms);

/* Send a latency probe to the seated guest (no-op if none). */
int rnet_lan_direct_host_ping(RNetLanDirectHost *host);

/* Notify seated guest that the match is starting. */
int rnet_lan_direct_host_notify_start(RNetLanDirectHost *host,
                                      const RNetLanLobby *room);

/* Host: push updated match caps (D / rollback / P / opaque session variant)
 * while a guest is seated. START repeats these authoritative values. */
int rnet_lan_direct_host_notify_caps(RNetLanDirectHost *host,
                                     const RNetLanLobby *room);

/* Tell guest they were kicked / host left. */
int rnet_lan_direct_host_notify_kick(RNetLanDirectHost *host);
int rnet_lan_direct_host_notify_close(RNetLanDirectHost *host);

/* Host says something. Queued locally AND sent to the seated guest, so the
 * caller reads its own line back through take_chat like any other -- one
 * source for what was said, in one order. */
int rnet_lan_direct_host_send_chat(RNetLanDirectHost *host,
                                   const char *player_id, const char *from,
                                   const char *text);

/* Drain one received line, oldest first. 1 = filled, 0 = queue empty. */
int rnet_lan_direct_host_take_chat(RNetLanDirectHost *host,
                                   RNetLanChatLine *out);

/* Seat swap. The two-seat room has one possible trade -- host and guest --
 * so a request carries no seat numbers. The seated guest asks (SWAPREQ), the
 * host's caller reads it through take_swap_request (1 once per ask), decides,
 * and answers with send_swap_result; the guest's caller reads that through
 * take_swap_result (1 once per answer, *accept filled). Moving the seats
 * themselves is the caller's job (the room file), as for every other change. */
int rnet_lan_direct_host_take_swap_request(RNetLanDirectHost *host);

/* Push the seat table (host_slot, names, started) to the seated guest. Call
 * on every room change: without it a guest learns a seat swap only at start. */
int rnet_lan_direct_host_notify_room(RNetLanDirectHost *host,
                                     const RNetLanLobby *room);
int rnet_lan_direct_host_send_swap_result(RNetLanDirectHost *host, int accept);

/* ---- guest --------------------------------------------------------------- */

/* Blocking join to host_hostport. timeout_ms <= 0 → 2000.
 * On OK, *out_room is filled and *out_guest owns a socket for START/KICK.
 * join_begin + join_poll until it answers, waiting on the socket between. */
int rnet_lan_direct_guest_join(const char *host_hostport,
                               const char *expected_game,
                               const char *expected_version,
                               const char *password, const char *player_name,
                               const char *guest_bind_hostport, int timeout_ms,
                               RNetLanLobby *out_room,
                               RNetLanDirectGuest **out_guest);

/* The same join without blocking, for a caller that has a frame to keep
 * drawing -- a guest re-seating itself after a soft return, when the host may
 * not be listening yet.
 *
 * join_begin opens and binds the socket and sends the first JOIN_REQ; on OK
 * *out_guest is a handle that is NOT seated yet. join_poll reads the answer
 * and re-sends JOIN_REQ every 400 ms while there is none:
 *   RNET_LAN_DIRECT_OK       seated; *out_room filled (as the blocking join)
 *   RNET_LAN_DIRECT_PENDING  no answer yet -- poll again
 *   < 0                      refused (JOIN_NAK: FULL / PASSWORD / IDENTITY /
 *                            STARTED) or I/O; the answer sticks
 * There is no deadline inside: the caller owns how long to keep asking, and
 * closes the handle (rnet_lan_direct_guest_close) when it gives up or is
 * refused. Until seated, guest_pump reports nothing and the handle sends no
 * chat, pings or LEAVE the host would act on. */
int rnet_lan_direct_guest_join_begin(const char *host_hostport,
                                     const char *expected_game,
                                     const char *expected_version,
                                     const char *password,
                                     const char *player_name,
                                     const char *guest_bind_hostport,
                                     RNetLanDirectGuest **out_guest);
int rnet_lan_direct_guest_join_poll(RNetLanDirectGuest *guest,
                                    RNetLanLobby *out_room);

void rnet_lan_direct_guest_close(RNetLanDirectGuest **guest);

/* Non-blocking: 1=START, 2=KICK/CLOSE, 3=RTT updated (*out_rtt_ms), 0=none,
 * <0 error. */
int rnet_lan_direct_guest_pump(RNetLanDirectGuest *guest, RNetLanLobby *room,
                               int *out_rtt_ms);

/* Send a latency probe to the host. */
int rnet_lan_direct_guest_ping(RNetLanDirectGuest *guest);

/* Guest says something. Sent to the host and NOT queued locally: the host is
 * the authority and echoes it back, which is what keeps both peers' logs in
 * the same order. `from` is the host's to fill in from the seat table -- a
 * guest naming itself could name anybody. */
int rnet_lan_direct_guest_send_chat(RNetLanDirectGuest *guest,
                                    const char *player_id, const char *text);

/* Drain one received line, oldest first. 1 = filled, 0 = queue empty. */
int rnet_lan_direct_guest_take_chat(RNetLanDirectGuest *guest,
                                    RNetLanChatLine *out);

/* Guest asks the host to trade seats / reads the host's answer (see the
 * host side above). */
int rnet_lan_direct_guest_send_swap_request(RNetLanDirectGuest *guest);
int rnet_lan_direct_guest_take_swap_result(RNetLanDirectGuest *guest, int *accept);

/* Guest leaves waiting room (best-effort notify). */
int rnet_lan_direct_guest_leave(RNetLanDirectGuest *guest);

#ifdef __cplusplus
}
#endif

#endif /* RNET_LAN_DIRECT_H */
