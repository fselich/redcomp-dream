#ifndef RECOMP_NET_ICE_XFER_H
#define RECOMP_NET_ICE_XFER_H

#include "recomp_net/ice.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reliable blob transfer over a dedicated ICE/UDP agent (libjuice).
 * Signaling stays on the host-supplied emit callback (lobby WS). Application
 * bytes never go through that callback — only SDP/candidates.
 *
 * Blobs are queued FIFO. A zero-length blob is a valid end marker.
 */
typedef struct RNetIceXfer RNetIceXfer;

typedef void (*RNetIceXferSignalEmitFn)(const RNetSignal *msg, void *user);

int rnet_ice_xfer_open(RNetIceXfer **out, const RNetIceConfig *ice,
                       RNetIceXferSignalEmitFn emit, void *user);
void rnet_ice_xfer_close(RNetIceXfer **xfer);

void rnet_ice_xfer_push_signal(RNetIceXfer *xfer, const RNetSignal *msg);
void rnet_ice_xfer_pump(RNetIceXfer *xfer);

RNetIceState rnet_ice_xfer_state(const RNetIceXfer *xfer);

/* Takes ownership of data (free()). len 0 is allowed (end marker). */
int rnet_ice_xfer_queue_blob(RNetIceXfer *xfer, uint8_t *data, size_t len);

/* 1 when no queued/in-flight outbound blob remains. */
int rnet_ice_xfer_send_idle(const RNetIceXfer *xfer);

/* Completed inbound blob. Caller frees *data (NULL when len is 0). */
int rnet_ice_xfer_take_blob(RNetIceXfer *xfer, uint8_t **data, size_t *len);

/* Which candidate pair carries this transfer: host|srflx|prflx|relay|unknown
 * (or "none" in a build without ICE). Meaningful only once the state reaches
 * CONNECTED; before that it is "unknown".
 *
 * Callers use this to price a transfer. A relayed path runs on someone else's
 * bandwidth -- the TURN server's -- so a caller may reasonably allow any size
 * over a direct pair and cap what it will push through a relay. That policy
 * belongs to the caller; this only reports the fact. */
void rnet_ice_xfer_path(const RNetIceXfer *xfer, char *out, size_t cap);

/* -1 idle/connecting, -2 fail, 0..100 of the current inbound or outbound blob. */
int rnet_ice_xfer_progress(const RNetIceXfer *xfer);
int rnet_ice_xfer_failed(const RNetIceXfer *xfer, char *err, size_t err_cap);
void rnet_ice_xfer_fail(RNetIceXfer *xfer, const char *err);

#ifdef __cplusplus
}
#endif

#endif /* RECOMP_NET_ICE_XFER_H */
