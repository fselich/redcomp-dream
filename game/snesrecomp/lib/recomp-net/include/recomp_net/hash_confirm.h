#ifndef RECOMP_NET_HASH_CONFIRM_H
#define RECOMP_NET_HASH_CONFIRM_H

/*
 * Local digest ring + peer FRAME_COMMIT matching → resolved_through watermark.
 *
 * rnet_hc_confirm_through(T) is 1 iff every tick in (prev_resolved, T] has a
 * local digest that matched a peer FRAME_COMMIT (contiguous from the prior
 * watermark). Bind to RNetInputContractHostGates.hash_confirm_promote /
 * RNetRollbackVTable.hash_confirm_through.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RNET_HC_RING 128u
/* Seats a quorum chain can wait on (bit i of the peer mask = seat i). */
#define RNET_HC_MAX_PEERS 8u

typedef struct RNetHashConfirm {
    uint32_t local_tick[RNET_HC_RING];
    uint32_t local_digest[RNET_HC_RING];
    uint8_t  local_valid[RNET_HC_RING];
    uint32_t peer_tick[RNET_HC_RING];
    uint32_t peer_digest[RNET_HC_RING];
    uint8_t  peer_valid[RNET_HC_RING];
    uint32_t resolved_through; /* inclusive; 0 = none yet (tick 0 may match) */
    uint8_t  resolved_valid;   /* 0 until first match advances watermark */
    /* Quorum (rnet_hc_set_peer_mask). With a mask, a tick's peer digest is
     * known only once EVERY masked seat has committed it, and it matches only
     * when they all agree. Kept across reset/prime_after. */
    uint32_t peer_mask;
    uint32_t quorum_tick[RNET_HC_RING];
    uint8_t  quorum_seen[RNET_HC_RING];     /* seats heard for quorum_tick */
    uint8_t  quorum_conflict[RNET_HC_RING]; /* seats disagree at peer_tick */
    uint32_t quorum_digest[RNET_HC_RING][RNET_HC_MAX_PEERS];
} RNetHashConfirm;

void rnet_hc_reset(RNetHashConfirm *hc);

/* Clear the ring and set resolved_through = last_ok so the next compared tick
 * is last_ok+1. Used at Replay entry to drop live invent FRAME_COMMITs. */
void rnet_hc_prime_after(RNetHashConfirm *hc, uint32_t last_ok);

void rnet_hc_note_local(RNetHashConfirm *hc, uint32_t tick, uint32_t digest);
void rnet_hc_note_peer(RNetHashConfirm *hc, uint32_t tick, uint32_t digest);

/*
 * More than two seats: confirm against every peer, not the last one heard.
 *
 * One peer ring shared by three peers' commits confirms a tick as soon as ANY
 * peer matches it. Peers that made the same wrong prediction for a fourth
 * seat agree with each other, confirm each other, and then treat the late
 * real input as cosmetic -- the fourth peer is left on its own timeline with
 * nobody rolling back (R4 4-peer soak: guests predicted the slow host's pad,
 * hash-confirmed among themselves, promoted the real pad into history only,
 * and the host forked for the rest of the race).
 *
 * set_peer_mask(bit i = seat i, never the local seat; 0 = one peer, the
 * legacy chain) makes the chain a quorum: note_peer_from(seat, ...) records
 * each seat's commit, the tick's peer digest exists once every masked seat
 * has committed it, and it matches only when all of them agree with the
 * local digest. Disagreement among the peers is a mismatch even when one of
 * them matches us (peek_mismatch then reports a peer digest that differs).
 * A seat outside the mask is ignored. Set the mask before the first note;
 * reset and prime_after keep it.
 */
void rnet_hc_set_peer_mask(RNetHashConfirm *hc, uint32_t peer_mask);
uint32_t rnet_hc_peer_mask(const RNetHashConfirm *hc);
void rnet_hc_note_peer_from(RNetHashConfirm *hc, int seat, uint32_t tick,
                            uint32_t digest);

uint32_t rnet_hc_resolved_through(const RNetHashConfirm *hc);
uint8_t  rnet_hc_confirm_through(const RNetHashConfirm *hc, uint32_t tick);

uint8_t rnet_hc_local_digest(const RNetHashConfirm *hc, uint32_t tick,
                            uint32_t *digest_out);
uint8_t rnet_hc_peer_digest(const RNetHashConfirm *hc, uint32_t tick,
                           uint32_t *digest_out);

uint8_t rnet_hc_peek_mismatch(const RNetHashConfirm *hc, uint32_t *tick_out,
                             uint32_t *local_out, uint32_t *peer_out);

/* Heal a stuck watermark when the next tick aged out of the ring and is no
 * longer a live mismatch. Returns 1 if the watermark moved. */
uint8_t rnet_hc_heal_stale_gap(RNetHashConfirm *hc);

#ifdef __cplusplus
}
#endif

#endif /* RECOMP_NET_HASH_CONFIRM_H */
