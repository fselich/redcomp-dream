#ifndef RECOMP_NET_RB_QUORUM_H
#define RECOMP_NET_RB_QUORUM_H

/*
 * Every participant answers: one episode handshake (BASELINE, ready-ACK,
 * POST) across N seats.
 *
 * A host that keeps one "peer baseline" / "peer POST" latch lets the first
 * reply answer for every peer. With four seats that let two guests replay
 * and resume while a third seat (the slow host in R4's soak) had not even
 * opened the episode: they resumed on different timelines and waited on
 * each other until the stall timeout.
 *
 * A quorum records each seat's reply for one epoch. It is complete once every
 * expected seat has replied, ready once every expected seat has set its ready
 * flag, and agreed only when every reply carries the same digests. A reply
 * from a seat outside the expected mask, or for another epoch, is ignored.
 * A seat's later reply replaces its earlier one (retransmits, a ready upgrade)
 * and its ready flag latches.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RNET_RB_QUORUM_SEATS 8u
#define RNET_RB_QUORUM_DIGESTS 4u

typedef struct RNetRbQuorum {
    uint32_t expect; /* bit i = seat i must reply */
    uint32_t epoch;
    uint32_t seen;
    uint32_t ready;
    uint32_t digest[RNET_RB_QUORUM_SEATS][RNET_RB_QUORUM_DIGESTS];
} RNetRbQuorum;

void rnet_rb_quorum_reset(RNetRbQuorum *q, uint32_t expect, uint32_t epoch);
/* Record seat's reply. Returns 1 when it was recorded. */
uint8_t rnet_rb_quorum_note(RNetRbQuorum *q, int seat, uint32_t epoch,
                            const uint32_t digest[RNET_RB_QUORUM_DIGESTS],
                            uint8_t ready);
uint8_t rnet_rb_quorum_complete(const RNetRbQuorum *q);
uint8_t rnet_rb_quorum_all_ready(const RNetRbQuorum *q);
/* Expected seats that have not replied (or not set ready). */
uint32_t rnet_rb_quorum_missing(const RNetRbQuorum *q);
uint32_t rnet_rb_quorum_not_ready(const RNetRbQuorum *q);
/*
 * The digests to compare against ours, once complete. If every reply agrees,
 * that common value (returns 1). Otherwise, per digest index, a reply that
 * differs from `local` where one exists, so a comparison with `local` fails
 * whenever any seat disagrees with us or with another seat (returns 0, and
 * *seat_out names the first dissenting seat). local may be NULL.
 */
uint8_t rnet_rb_quorum_view(const RNetRbQuorum *q,
                            const uint32_t local[RNET_RB_QUORUM_DIGESTS],
                            uint32_t out[RNET_RB_QUORUM_DIGESTS], int *seat_out);

/* The one seat in a one-bit mask, else -1: lets a two-seat host credit a
 * reply whose sender it cannot name. */
int rnet_rb_quorum_sole_seat(uint32_t expect);

#ifdef __cplusplus
}
#endif

#endif
