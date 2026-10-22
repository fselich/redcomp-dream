#include "recomp_net/input_hist.h"

#include <string.h>

void rnet_ih_reset(RNetInputHist *h, int slot_count)
{
    if (!h)
        return;
    memset(h, 0, sizeof(*h));
    if (slot_count < 1)
        slot_count = 1;
    if (slot_count > RNET_INPUT_HIST_MAX_SLOTS)
        slot_count = RNET_INPUT_HIST_MAX_SLOTS;
    h->slot_count = slot_count;
    {
        int i;
        for (i = 0; i < RNET_INPUT_HIST_MAX_SLOTS; ++i)
            h->neutral[i].buttons = 0xFFFFu;
    }
}

int rnet_ih_set_neutral(RNetInputHist *h, int slot, const RNetRbFrame *neutral)
{
    if (!h || !neutral || slot < 0 || slot >= RNET_INPUT_HIST_MAX_SLOTS)
        return 0;
    memset(&h->neutral[slot], 0, sizeof(h->neutral[slot]));
    h->neutral[slot].buttons = neutral->buttons;
    h->neutral[slot].stick_x = neutral->stick_x;
    h->neutral[slot].stick_y = neutral->stick_y;
    h->neutral[slot].analog = neutral->analog;
    h->neutral[slot].rx = neutral->rx;
    h->neutral[slot].ry = neutral->ry;
    return 1;
}

void rnet_ih_frame_to_contract(const RNetRbFrame *frame, RNetInputContractFrame *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!frame)
        return;
    out->tick = frame->tick;
    out->buttons = frame->buttons;
    out->stick_x = frame->stick_x;
    out->stick_y = frame->stick_y;
    out->is_predicted = frame->is_predicted ? 1u : 0u;
}

int rnet_ih_put(RNetInputHist *h, int slot, const RNetRbFrame *frame)
{
    RNetRbFrame *dst;
    if (!h || !frame || !frame->is_valid)
        return 0;
    if (slot < 0 || slot >= h->slot_count)
        return 0;
    dst = &h->rows[slot][frame->tick % RNET_INPUT_HIST_DEPTH];
    *dst = *frame;
    dst->is_valid = 1u;
    return 1;
}

int rnet_ih_get(const RNetInputHist *h, int slot, uint32_t tick, RNetRbFrame *out)
{
    const RNetRbFrame *src;
    if (!h || !out)
        return 0;
    if (slot < 0 || slot >= h->slot_count)
        return 0;
    src = &h->rows[slot][tick % RNET_INPUT_HIST_DEPTH];
    if (!src->is_valid || src->tick != tick)
        return 0;
    *out = *src;
    return 1;
}

int rnet_ih_invent_hold_last(RNetInputHist *h, int slot, uint32_t tick, RNetRbFrame *out)
{
    RNetRbFrame invented;
    RNetRbFrame prev;
    uint32_t look;

    if (!h || slot < 0 || slot >= h->slot_count)
        return 0;

    memset(&invented, 0, sizeof(invented));
    invented.tick = tick;
    invented.buttons = h->neutral[slot].buttons;
    invented.stick_x = h->neutral[slot].stick_x;
    invented.stick_y = h->neutral[slot].stick_y;
    invented.analog = h->neutral[slot].analog;
    invented.rx = h->neutral[slot].rx;
    invented.ry = h->neutral[slot].ry;
    invented.is_predicted = 1u;
    invented.is_valid = 1u;

    for (look = 1; look < RNET_INPUT_HIST_DEPTH; look++) {
        if (tick < look)
            break;
        if (rnet_ih_get(h, slot, tick - look, &prev)) {
            invented.buttons = prev.buttons;
            invented.stick_x = prev.stick_x;
            invented.stick_y = prev.stick_y;
            invented.analog = prev.analog;
            invented.rx = prev.rx;
            invented.ry = prev.ry;
            break;
        }
    }

    if (!rnet_ih_put(h, slot, &invented))
        return 0;
    h->invent_count++;
    if (out)
        *out = invented;
    return 1;
}

int rnet_ih_invent_idle(RNetInputHist *h, int slot, uint32_t tick, RNetRbFrame *out)
{
    RNetRbFrame invented;
    RNetRbFrame prev;
    uint32_t look;

    if (!h || slot < 0 || slot >= h->slot_count)
        return 0;

    memset(&invented, 0, sizeof(invented));
    invented.tick = tick;
    invented.buttons = h->neutral[slot].buttons;
    invented.stick_x = h->neutral[slot].stick_x;
    invented.stick_y = h->neutral[slot].stick_y;
    invented.analog = h->neutral[slot].analog;
    invented.rx = h->neutral[slot].rx;
    invented.ry = h->neutral[slot].ry;
    invented.is_predicted = 1u;
    invented.is_valid = 1u;

    /* Idle releases the buttons and centres the axes; it does not swap the
     * controller. The device type (analog) comes from the seat's last row: a
     * neutral "digital" fill switched an analog / NeGcon pad to digital for
     * one replayed tick, the emulated pad answered a shorter poll, and the
     * replay ran a few guest cycles short of the live tick (4-seat R4
     * rollback: seats that sealed the gap themselves split from the seat
     * that had its real row). */
    for (look = 1; look < RNET_INPUT_HIST_DEPTH; look++) {
        if (tick < look)
            break;
        if (rnet_ih_get(h, slot, tick - look, &prev)) {
            invented.analog = prev.analog;
            break;
        }
    }

    if (!rnet_ih_put(h, slot, &invented))
        return 0;
    h->invent_count++;
    if (out)
        *out = invented;
    return 1;
}

int rnet_ih_promote(RNetInputHist *h, int slot, const RNetRbFrame *wire)
{
    RNetRbFrame auth;
    if (!h || !wire || !wire->is_valid)
        return 0;
    auth = *wire;
    auth.is_predicted = 0u;
    auth.is_valid = 1u;
    if (!rnet_ih_put(h, slot, &auth))
        return 0;
    h->promote_count++;
    return 1;
}
