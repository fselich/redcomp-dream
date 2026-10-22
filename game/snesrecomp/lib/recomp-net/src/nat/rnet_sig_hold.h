#ifndef RNET_SIG_HOLD_H
#define RNET_SIG_HOLD_H

/*
 * Per-peer hold for ICE signals that arrive before the agent that consumes
 * them exists. One bucket per sending player id, so a stranger's signals can
 * neither evict nor be replayed into another peer's negotiation (the old
 * single-sender buffer reset itself whenever a second sender spoke).
 *
 * Internal: shared by the lobby client (mod transfer) and RNetHostIce.
 * Pure bookkeeping, no I/O; buckets allocate lazily.
 */

#include "recomp_net/ice.h"
#include "recomp_net/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RNET_SIG_HOLD_PEERS 8
#define RNET_SIG_HOLD_PER_PEER 24
#define RNET_SIG_HOLD_ID_LEN 64

typedef struct RNetSigHoldEntry
{
    RNetSignal sig;
    int slot; /* the sender's seat when the signal arrived (-1 = unchecked) */
} RNetSigHoldEntry;

typedef struct RNetSigHoldBucket
{
    char player_id[RNET_SIG_HOLD_ID_LEN];
    RNetSigHoldEntry *e;
    int n;
    rnet_u64 born_ms;
} RNetSigHoldBucket;

typedef struct RNetSigHold
{
    RNetSigHoldBucket b[RNET_SIG_HOLD_PEERS];
    unsigned dropped; /* signals refused because a bucket / the table was full */
} RNetSigHold;

/* 0 held; -1 refused (bucket full -- the newest is dropped, keeping the offer
 * -- or no free bucket for a new peer). */
int rnet_sig_hold_push(RNetSigHold *h, const char *player_id, int slot, const RNetSignal *sig,
                       rnet_u64 now_ms);
int rnet_sig_hold_count(const RNetSigHold *h, const char *player_id);
const RNetSigHoldEntry *rnet_sig_hold_get(const RNetSigHold *h, const char *player_id, int index);
/* Forget one peer's bucket / everything / buckets older than max_age_ms. */
void rnet_sig_hold_drop(RNetSigHold *h, const char *player_id);
void rnet_sig_hold_clear(RNetSigHold *h);
void rnet_sig_hold_expire(RNetSigHold *h, rnet_u64 now_ms, rnet_u64 max_age_ms);

#ifdef __cplusplus
}
#endif

#endif /* RNET_SIG_HOLD_H */
