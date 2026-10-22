#include "recomp_net/hash_confirm.h"

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
    RNetHashConfirm hc;
    rnet_hc_reset(&hc);

    CHECK(!rnet_hc_confirm_through(&hc, 0), "empty not confirmed");
    CHECK(rnet_hc_resolved_through(&hc) == 0u, "resolved starts 0");

    rnet_hc_note_local(&hc, 0, 0x1111u);
    rnet_hc_note_local(&hc, 1, 0x2222u);
    rnet_hc_note_local(&hc, 2, 0x3333u);
    CHECK(!rnet_hc_confirm_through(&hc, 0), "local-only not enough");

    rnet_hc_note_peer(&hc, 0, 0x1111u);
    CHECK(rnet_hc_confirm_through(&hc, 0), "tick 0 matched");
    CHECK(rnet_hc_resolved_through(&hc) == 0u, "resolved at 0");
    CHECK(!rnet_hc_confirm_through(&hc, 1), "tick 1 not yet");

    rnet_hc_note_peer(&hc, 1, 0x2222u);
    CHECK(rnet_hc_confirm_through(&hc, 1), "tick 1 matched");
    CHECK(rnet_hc_resolved_through(&hc) == 1u, "resolved at 1");

    rnet_hc_note_peer(&hc, 2, 0xDEADu);
    CHECK(rnet_hc_resolved_through(&hc) == 1u, "mismatch holds watermark");
    {
        uint32_t mt = 0, mld = 0, mpd = 0;
        CHECK(rnet_hc_peek_mismatch(&hc, &mt, &mld, &mpd), "peek mismatch");
        CHECK(mt == 2u && mld == 0x3333u && mpd == 0xDEADu, "peek mismatch vals");
    }

    rnet_hc_prime_after(&hc, 819u);
    rnet_hc_note_local(&hc, 820, 0xAAAAu);
    rnet_hc_note_peer(&hc, 820, 0xAAAAu);
    CHECK(rnet_hc_resolved_through(&hc) == 820u, "prime then match");

    rnet_hc_reset(&hc);
    rnet_hc_note_local(&hc, 0, 0x1u);
    rnet_hc_note_peer(&hc, 0, 0x1u);
    rnet_hc_note_local(&hc, 1, 0xAAAAu);
    rnet_hc_note_peer(&hc, 1, 0xBBBBu);
    rnet_hc_note_local(&hc, 1u + RNET_HC_RING, 0xCCCCu);
    rnet_hc_note_peer(&hc, 1u + RNET_HC_RING, 0xCCCCu);
    rnet_hc_note_local(&hc, 50u + RNET_HC_RING, 0xDDDDu);
    rnet_hc_note_peer(&hc, 50u + RNET_HC_RING, 0xDDDDu);
    CHECK(rnet_hc_heal_stale_gap(&hc), "heal advances over stale gap");
    CHECK(rnet_hc_resolved_through(&hc) == 50u + RNET_HC_RING, "heal tip");

    /* Quorum: local seat 1 of 4, peers 0, 2, 3. */
    rnet_hc_reset(&hc);
    rnet_hc_set_peer_mask(&hc, 0x0Du);
    rnet_hc_note_local(&hc, 0, 0x10u);
    rnet_hc_note_peer_from(&hc, 2, 0, 0x10u);
    rnet_hc_note_peer_from(&hc, 3, 0, 0x10u);
    CHECK(!rnet_hc_confirm_through(&hc, 0), "quorum: two of three peers not enough");
    {
        uint32_t pd = 0;
        CHECK(!rnet_hc_peer_digest(&hc, 0, &pd), "quorum: no peer digest yet");
    }
    rnet_hc_note_peer_from(&hc, 0, 0, 0x10u);
    CHECK(rnet_hc_confirm_through(&hc, 0), "quorum: all three agree");

    /* The R4 4-peer fork: seats 2 and 3 predicted seat 0's pad like us and
     * agree with us at tick 1; seat 0 (the slow host) does not. Under the
     * legacy shared ring the last commit decides; the quorum never confirms. */
    rnet_hc_note_local(&hc, 1, 0x20u);
    rnet_hc_note_peer_from(&hc, 2, 1, 0x20u);
    rnet_hc_note_peer_from(&hc, 3, 1, 0x20u);
    rnet_hc_note_peer_from(&hc, 0, 1, 0x99u);
    CHECK(!rnet_hc_confirm_through(&hc, 1), "quorum: one dissenting peer blocks");
    {
        uint32_t mt = 0, mld = 0, mpd = 0;
        CHECK(rnet_hc_peek_mismatch(&hc, &mt, &mld, &mpd), "quorum: dissent is a mismatch");
        CHECK(mt == 1u && mld == 0x20u && mpd == 0x99u, "quorum: reports the dissenting digest");
    }
    /* Dissent among peers is a mismatch even when the first seat matches us. */
    rnet_hc_reset(&hc);
    CHECK(rnet_hc_peer_mask(&hc) == 0x0Du, "quorum: reset keeps the mask");
    rnet_hc_note_local(&hc, 0, 0x30u);
    rnet_hc_note_peer_from(&hc, 0, 0, 0x30u);
    rnet_hc_note_peer_from(&hc, 2, 0, 0x31u);
    rnet_hc_note_peer_from(&hc, 3, 0, 0x30u);
    {
        uint32_t mt = 0, mld = 0, mpd = 0;
        CHECK(!rnet_hc_confirm_through(&hc, 0), "quorum: peers disagreeing blocks");
        CHECK(rnet_hc_peek_mismatch(&hc, &mt, &mld, &mpd) && mpd == 0x31u,
              "quorum: peek names the peer that differs from us");
    }
    /* A resimulated commit from the dissenter heals the tick. */
    rnet_hc_note_peer_from(&hc, 2, 0, 0x30u);
    CHECK(rnet_hc_confirm_through(&hc, 0), "quorum: re-commit converges");
    /* Seats outside the mask (ourselves, an empty seat) are ignored. */
    rnet_hc_note_local(&hc, 1, 0x40u);
    rnet_hc_note_peer_from(&hc, 1, 1, 0x40u);
    rnet_hc_note_peer_from(&hc, 7, 1, 0x40u);
    rnet_hc_note_peer_from(&hc, -1, 1, 0x40u);
    CHECK(!rnet_hc_confirm_through(&hc, 1), "quorum: unmasked seats do not count");
    /* prime_after keeps the mask and drops half-collected commits. */
    rnet_hc_note_peer_from(&hc, 0, 1, 0x40u);
    rnet_hc_prime_after(&hc, 0u);
    rnet_hc_note_local(&hc, 1, 0x40u);
    rnet_hc_note_peer_from(&hc, 2, 1, 0x40u);
    rnet_hc_note_peer_from(&hc, 3, 1, 0x40u);
    CHECK(!rnet_hc_confirm_through(&hc, 1), "quorum: prime drops pre-prime commits");
    rnet_hc_note_peer_from(&hc, 0, 1, 0x40u);
    CHECK(rnet_hc_confirm_through(&hc, 1), "quorum: after prime all three again");
    /* Ring reuse: a slot's old tick commits never count toward a new tick. */
    rnet_hc_note_peer_from(&hc, 0, 2u + RNET_HC_RING, 0x50u);
    rnet_hc_note_local(&hc, 2, 0x50u);
    rnet_hc_note_peer_from(&hc, 2, 2, 0x50u);
    rnet_hc_note_peer_from(&hc, 3, 2, 0x50u);
    CHECK(!rnet_hc_confirm_through(&hc, 2), "quorum: ring reuse resets seats heard");
    /* Mask 0 is the legacy single chain. */
    rnet_hc_set_peer_mask(&hc, 0u);
    rnet_hc_reset(&hc);
    rnet_hc_note_local(&hc, 0, 0x60u);
    rnet_hc_note_peer_from(&hc, 5, 0, 0x60u);
    CHECK(rnet_hc_confirm_through(&hc, 0), "mask 0: any commit is the peer's");

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ALL PASS\n");
    return 0;
}
