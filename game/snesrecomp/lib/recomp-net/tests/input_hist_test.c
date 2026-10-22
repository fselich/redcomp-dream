#include "recomp_net/input_hist.h"

#include <stdio.h>
#include <string.h>

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

static uint8_t hc_yes(void *ctx)
{
    (void)ctx;
    return 1;
}
static uint8_t hc_no(void *ctx)
{
    (void)ctx;
    return 0;
}

int main(void)
{
    RNetInputHist h, jog;
    RNetRbFrame f, got;
    RNetInputContractFrame pub, wire;
    RNetInputContractParams params;
    RNetInputContractHostGates gates;
    RNetInputContractDecision d;

    rnet_ih_reset(&h, 2);
    CHECK(h.slot_count == 2, "slot_count");

    memset(&f, 0, sizeof(f));
    f.tick = 10;
    f.buttons = 0xFDFFu;
    f.stick_x = 40;
    f.analog = 1u;
    f.is_valid = 1u;
    CHECK(rnet_ih_put(&h, 0, &f), "put local");
    CHECK(rnet_ih_get(&h, 0, 10, &got), "get local");
    CHECK(got.stick_x == 40 && got.analog == 1u, "get fields");

    /* The source byte is a controller type, so JogCon must survive neutral,
     * hold-last prediction, and history retrieval without bool conversion. */
    rnet_ih_reset(&jog, 1);
    f.tick = 21;
    f.analog = 2u;
    f.is_valid = 1u;
    CHECK(rnet_ih_put(&jog, 0, &f), "put JogCon row");
    CHECK(rnet_ih_invent_hold_last(&jog, 0, 22, &got), "invent JogCon hold-last");
    CHECK(got.analog == 2u, "hold-last preserves JogCon type");
    f.tick = 23; f.analog = 3u; f.rx = 0xC0u; f.ry = 0x20u;  /* NeGcon I / II */
    CHECK(rnet_ih_put(&jog, 0, &f), "put NeGcon row");
    CHECK(rnet_ih_invent_hold_last(&jog, 0, 24, &got) && got.rx == 0xC0u &&
          got.ry == 0x20u, "hold-last keeps the rest of the pad (rx/ry)");
    f.analog = 2u; f.rx = f.ry = 0u;
    CHECK(rnet_ih_get(&jog, 0, 22, &got) && got.analog == 2u,
          "history preserves JogCon type");
    f.analog = 2u;
    CHECK(rnet_ih_set_neutral(&jog, 0, &f), "set JogCon neutral");
    CHECK(rnet_ih_invent_idle(&jog, 0, 30, &got) && got.analog == 3u,
          "idle prediction keeps the last row's device (NeGcon), not neutral's");
    {
        RNetInputHist fresh;
        rnet_ih_reset(&fresh, 1);
        CHECK(rnet_ih_set_neutral(&fresh, 0, &f), "set JogCon neutral (fresh)");
        CHECK(rnet_ih_invent_idle(&fresh, 0, 30, &got) && got.analog == 2u,
              "idle prediction with no prior row uses the neutral JogCon type");
    }

    /* NeGcon (3) is just another type byte: no clamp to the 0..2 range. */
    rnet_ih_reset(&jog, 1);
    f.tick = 40;
    f.analog = 3u;
    f.stick_x = -100;
    f.stick_y = -128;
    f.is_valid = 1u;
    CHECK(rnet_ih_put(&jog, 0, &f), "put NeGcon row");
    CHECK(rnet_ih_get(&jog, 0, 40, &got) && got.analog == 3u &&
          got.stick_x == -100 && got.stick_y == -128,
          "history preserves NeGcon type and axes");
    CHECK(rnet_ih_invent_hold_last(&jog, 0, 41, &got) && got.analog == 3u,
          "hold-last preserves NeGcon type");
    CHECK(rnet_ih_set_neutral(&jog, 0, &f), "set NeGcon neutral");
    CHECK(rnet_ih_invent_idle(&jog, 0, 50, &got) && got.analog == 3u,
          "idle prediction preserves NeGcon type");

    CHECK(rnet_ih_invent_hold_last(&h, 1, 11, &got), "invent neutral");
    CHECK(got.is_predicted && got.buttons == 0xFFFFu, "invent predicted neutral");
    CHECK(got.stick_x == 0 && got.analog == 0u, "invent sticks");

    f = got;
    f.tick = 11;
    f.buttons = 0xFBFFu;
    f.stick_x = -20;
    f.analog = 1u;
    f.is_predicted = 0;
    CHECK(rnet_ih_put(&h, 1, &f), "put auth remote");
    CHECK(rnet_ih_invent_hold_last(&h, 1, 12, &got), "invent hold-last");
    CHECK(got.buttons == 0xFBFFu && got.stick_x == -20 && got.analog == 1u,
          "held");
    CHECK(got.is_predicted, "hold-last predicted");

    CHECK(rnet_ih_invent_idle(&h, 1, 13, &got), "invent idle");
    CHECK(got.buttons == 0xFFFFu && got.stick_x == 0, "idle");
    CHECK(got.analog == 1u,
          "idle keeps the seat's device type (a digital fill flipped a DualShock "
          "seat for one replayed tick and split 4-seat replays)");

    f = got;
    f.stick_x = -18;
    f.is_predicted = 0;
    CHECK(rnet_ih_promote(&h, 1, &f), "promote");
    CHECK(rnet_ih_get(&h, 1, 13, &got) && !got.is_predicted && got.stick_x == -18,
          "promoted");

    /* An active-high pad (SNES, N64) names its own neutral; without it the
     * no-prior-row fallback is the PSX active-low 0xFFFF, which on those pads
     * reads as every button held. */
    {
        RNetInputHist hi;
        RNetRbFrame n, inv;
        rnet_ih_reset(&hi, 4);
        memset(&n, 0, sizeof(n));
        n.buttons = 0x0000u;
        n.stick_x = 0;
        CHECK(rnet_ih_set_neutral(&hi, 3, &n), "set neutral seat 3");
        CHECK(rnet_ih_invent_hold_last(&hi, 3, 5, &inv) && inv.buttons == 0x0000u &&
                  inv.is_predicted,
              "invent with no prior row uses the seat's neutral");
        CHECK(rnet_ih_invent_idle(&hi, 3, 6, &inv) && inv.buttons == 0x0000u,
              "idle invent uses the seat's neutral");
        CHECK(rnet_ih_invent_hold_last(&hi, 2, 5, &inv) && inv.buttons == 0xFFFFu,
              "a seat that never set one keeps the 0xFFFF default");
        CHECK(!rnet_ih_set_neutral(&hi, RNET_INPUT_HIST_MAX_SLOTS, &n),
              "set_neutral refuses a seat past the table");
    }

    rnet_input_contract_params_init_defaults(&params);
    rnet_ih_frame_to_contract(&got, &pub);
    pub.is_predicted = 1;
    pub.stick_x = -20;
    wire = pub;
    wire.is_predicted = 0;
    wire.stick_x = -18;
    memset(&gates, 0, sizeof(gates));
    gates.hash_confirm_promote = hc_no;
    d = rnet_input_contract_stick_replace_decide(&pub, &wire, 1, &params, &gates);
    CHECK(rnet_input_contract_decision_is_rewind(d), "no hash_confirm → rewind");
    gates.hash_confirm_promote = hc_yes;
    d = rnet_input_contract_stick_replace_decide(&pub, &wire, 1, &params, &gates);
    CHECK(d == nRNetInputContractPromoteHashConfirm, "hash_confirm → promote");

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ALL PASS\n");
    return 0;
}
