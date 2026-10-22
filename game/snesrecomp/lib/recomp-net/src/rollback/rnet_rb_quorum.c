#include "recomp_net/rb_quorum.h"

#include <string.h>

void rnet_rb_quorum_reset(RNetRbQuorum *q, uint32_t expect, uint32_t epoch)
{
    if (!q)
        return;
    memset(q, 0, sizeof(*q));
    q->expect = expect & ((1u << RNET_RB_QUORUM_SEATS) - 1u);
    q->epoch = epoch;
}

uint8_t rnet_rb_quorum_note(RNetRbQuorum *q, int seat, uint32_t epoch,
                            const uint32_t digest[RNET_RB_QUORUM_DIGESTS],
                            uint8_t ready)
{
    uint32_t bit, i;
    if (!q || epoch != q->epoch || seat < 0 ||
        (uint32_t)seat >= RNET_RB_QUORUM_SEATS)
        return 0u;
    bit = 1u << seat;
    if (!(q->expect & bit))
        return 0u;
    for (i = 0; i < RNET_RB_QUORUM_DIGESTS; ++i)
        q->digest[seat][i] = digest ? digest[i] : 0u;
    q->seen |= bit;
    if (ready)
        q->ready |= bit;
    return 1u;
}

uint8_t rnet_rb_quorum_complete(const RNetRbQuorum *q)
{
    return (q && (q->seen & q->expect) == q->expect) ? 1u : 0u;
}

uint8_t rnet_rb_quorum_all_ready(const RNetRbQuorum *q)
{
    return (q && (q->ready & q->expect) == q->expect) ? 1u : 0u;
}

uint32_t rnet_rb_quorum_missing(const RNetRbQuorum *q)
{
    return q ? (q->expect & ~q->seen) : 0u;
}

uint32_t rnet_rb_quorum_not_ready(const RNetRbQuorum *q)
{
    return q ? (q->expect & ~q->ready) : 0u;
}

uint8_t rnet_rb_quorum_view(const RNetRbQuorum *q,
                            const uint32_t local[RNET_RB_QUORUM_DIGESTS],
                            uint32_t out[RNET_RB_QUORUM_DIGESTS], int *seat_out)
{
    uint32_t i, s;
    int first = -1, dissent = -1;
    if (seat_out)
        *seat_out = -1;
    if (!q || !out)
        return 0u;
    for (s = 0; s < RNET_RB_QUORUM_SEATS; ++s)
        if ((q->expect & q->seen) & (1u << s)) {
            first = (int)s;
            break;
        }
    if (first < 0) {
        memset(out, 0, sizeof(uint32_t) * RNET_RB_QUORUM_DIGESTS);
        return 0u;
    }
    for (i = 0; i < RNET_RB_QUORUM_DIGESTS; ++i) {
        uint32_t v = q->digest[first][i];
        uint8_t agree = 1u;
        for (s = 0; s < RNET_RB_QUORUM_SEATS; ++s) {
            if (!((q->expect & q->seen) & (1u << s)))
                continue;
            if (q->digest[s][i] != q->digest[first][i]) {
                agree = 0u;
                if (dissent < 0)
                    dissent = (int)s;
            }
            if (local && q->digest[s][i] != local[i])
                v = q->digest[s][i];
        }
        /* Agreeing replies: the common value, whether or not it is ours.
         * Disagreeing ones: some seat differs from ours (v), so the host's
         * comparison fails as it must. */
        out[i] = agree ? q->digest[first][i] : v;
    }
    if (seat_out)
        *seat_out = dissent;
    return dissent < 0 ? 1u : 0u;
}

int rnet_rb_quorum_sole_seat(uint32_t expect)
{
    int s;
    if (!expect || (expect & (expect - 1u)))
        return -1;
    for (s = 0; s < (int)RNET_RB_QUORUM_SEATS; ++s)
        if (expect & (1u << s))
            return s;
    return -1;
}
