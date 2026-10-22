#include "recomp_net/rb_quorum.h"

#include <stdio.h>

static int failures;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL: %s\n", msg);                                         \
            failures++;                                                        \
        } else {                                                               \
            printf("ok:   %s\n", msg);                                         \
        }                                                                      \
    } while (0)

int main(void)
{
    RNetRbQuorum q;
    uint32_t a[4] = {0xA, 0xB, 0xC, 0}, b[4] = {0xA, 0xB, 0xD, 0}, out[4];
    int seat = 0;

    /* Four seats, we are seat 1: seats 0, 2, 3 must all answer. */
    rnet_rb_quorum_reset(&q, 0x0Du, 7u);
    CHECK(rnet_rb_quorum_note(&q, 2, 7u, a, 0), "seat 2 recorded");
    CHECK(!rnet_rb_quorum_complete(&q), "one of three is not complete");
    CHECK(!rnet_rb_quorum_note(&q, 1, 7u, a, 1), "own seat ignored");
    CHECK(!rnet_rb_quorum_note(&q, 3, 6u, a, 1), "other epoch ignored");
    CHECK(!rnet_rb_quorum_note(&q, 9, 7u, a, 1), "out-of-range seat ignored");
    CHECK(rnet_rb_quorum_missing(&q) == 0x09u, "missing names seats 0 and 3");
    rnet_rb_quorum_note(&q, 3, 7u, a, 1);
    CHECK(!rnet_rb_quorum_complete(&q), "the slow seat 0 still missing");
    rnet_rb_quorum_note(&q, 0, 7u, a, 0);
    CHECK(rnet_rb_quorum_complete(&q), "all three answered");
    CHECK(rnet_rb_quorum_view(&q, a, out, &seat) && out[2] == 0xC && seat < 0,
          "agreeing view is the common digest");
    CHECK(!rnet_rb_quorum_all_ready(&q) && rnet_rb_quorum_not_ready(&q) == 0x05u,
          "ready waits on seats 0 and 2");
    rnet_rb_quorum_note(&q, 0, 7u, a, 1);
    rnet_rb_quorum_note(&q, 2, 7u, a, 1);
    CHECK(rnet_rb_quorum_all_ready(&q), "every seat ready");
    rnet_rb_quorum_note(&q, 2, 7u, a, 0);
    CHECK(rnet_rb_quorum_all_ready(&q), "ready latches across a retransmit");

    /* One dissenting seat fails the comparison even if two match us. */
    rnet_rb_quorum_note(&q, 3, 7u, b, 1);
    CHECK(!rnet_rb_quorum_view(&q, a, out, &seat) && seat == 3, "dissent named");
    CHECK(out[2] == 0xD && out[0] == 0xA, "dissenting field differs from ours");
    /* Peers agree with each other but not with us: plain mismatch. */
    rnet_rb_quorum_note(&q, 0, 7u, b, 1);
    rnet_rb_quorum_note(&q, 2, 7u, b, 1);
    CHECK(rnet_rb_quorum_view(&q, a, out, &seat) && out[2] == 0xD,
          "unanimous peers that differ from us");
    /* We are the odd one only against one seat: still fails. */
    rnet_rb_quorum_note(&q, 0, 7u, a, 1);
    CHECK(!rnet_rb_quorum_view(&q, b, out, NULL) && out[2] == 0xC,
          "local matching two of three still fails");

    /* Three seats, we are the host (0). */
    rnet_rb_quorum_reset(&q, 0x06u, 9u);
    rnet_rb_quorum_note(&q, 1, 9u, a, 1);
    CHECK(!rnet_rb_quorum_complete(&q), "3 seats: one follower is not enough");
    rnet_rb_quorum_note(&q, 2, 9u, a, 1);
    CHECK(rnet_rb_quorum_complete(&q) && rnet_rb_quorum_all_ready(&q),
          "3 seats: both followers");
    /* reset drops the old epoch's replies */
    rnet_rb_quorum_reset(&q, 0x06u, 10u);
    CHECK(!rnet_rb_quorum_complete(&q) && q.seen == 0u, "reset clears replies");

    CHECK(rnet_rb_quorum_sole_seat(0x02u) == 1, "sole seat of two-seat mask");
    CHECK(rnet_rb_quorum_sole_seat(0x06u) == -1, "no sole seat with two peers");

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ALL PASS\n");
    return 0;
}
