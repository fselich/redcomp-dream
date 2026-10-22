#ifndef RNET_WS_H
#define RNET_WS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal RFC6455 helpers (text frames, no extensions). */

int rnet_ws_accept_key(const char *client_key, char out_b64[32]);

/*
 * Encode one text frame (FIN, opcode 1, masked when client_mask) into out.
 * Returns the frame length, or -1 when text is NULL, the payload is 64 KiB or
 * more (no 64-bit lengths), or out_cap is too small.
 */
int rnet_ws_frame_text(const char *text, int client_mask, uint8_t *out, size_t out_cap);

/*
 * Write one text frame and return 0, or -1. BLOCKING SOCKETS ONLY: on a
 * non-blocking socket a would-block after part of the frame has gone returns
 * -1 with that part already on the wire, and the next frame written after it
 * corrupts the stream (snesrecomp#104). A non-blocking writer uses RNetWsTx.
 */
int rnet_ws_write_text(int fd, const char *text, int client_mask);

/*
 * Frame-atomic outbound buffer for a NON-BLOCKING socket.
 *
 * Frames are appended whole (or refused whole) and flushed in order; a
 * would-block keeps the unsent tail, including the rest of a half-sent frame,
 * for the next flush. The stream on the wire is therefore always exactly the
 * sequence of queued frames. The connection stays up across would-block; the
 * owner disconnects only on a hard socket error (flush < 0) or when the
 * backlog would pass `cap` (queue < 0) -- a peer that has stopped reading.
 *
 * A zeroed RNetWsTx is valid and empty with the default cap. Free with
 * rnet_ws_tx_free (also resets it to that state).
 */
#define RNET_WS_TX_CAP_DEFAULT (256u * 1024u)

typedef struct RNetWsTx
{
    uint8_t *buf;
    size_t   len;    /* bytes held, including already-sent ones before off */
    size_t   off;    /* first unsent byte */
    size_t   frame_at; /* start of the frame that holds off (== off between frames) */
    size_t   alloc;
    size_t   cap;    /* max unsent bytes; 0 = RNET_WS_TX_CAP_DEFAULT */
    /* Counters, for logs and tests. */
    uint64_t frames_queued;
    uint64_t would_blocks;   /* flushes that stopped on would-block */
    uint64_t split_frames;   /* ... of which stopped inside a frame */
    size_t   high_water;     /* largest unsent backlog seen */
} RNetWsTx;

/* 0 queued; -1 the frame cannot be encoded (see rnet_ws_frame_text); -2 it
 * would take the unsent backlog past cap; -3 out of memory. Nothing is
 * queued on failure. Does not write to the socket. */
int rnet_ws_tx_queue_text(RNetWsTx *tx, const char *text, int client_mask);

/* Send as much of the backlog as the socket takes. Returns the unsent bytes
 * left (0 = drained, > 0 = would-block, try again on the next pump), or -1 on
 * a hard error (the connection is broken; the backlog is kept for
 * inspection). Never raises SIGPIPE. */
long rnet_ws_tx_flush(RNetWsTx *tx, int fd);

size_t rnet_ws_tx_pending(const RNetWsTx *tx);
void   rnet_ws_tx_free(RNetWsTx *tx);

/* There is no frame reader here: inbound frames are parsed from a buffer by
 * the lobby client (drain_ws_pending in rnet_lobby_client.c), which survives
 * a frame split across non-blocking reads. */

#ifdef __cplusplus
}
#endif

#endif
