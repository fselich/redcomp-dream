#include "recomp_net/hash_confirm.h"

#include <string.h>

static uint32_t slot_of(uint32_t tick)
{
    return tick % RNET_HC_RING;
}

void rnet_hc_reset(RNetHashConfirm *hc)
{
    uint32_t mask;
    if (!hc)
        return;
    mask = hc->peer_mask;
    memset(hc, 0, sizeof(*hc));
    hc->peer_mask = mask;
}

void rnet_hc_prime_after(RNetHashConfirm *hc, uint32_t last_ok)
{
    if (!hc)
        return;
    rnet_hc_reset(hc);
    hc->resolved_through = last_ok;
    hc->resolved_valid = 1u;
}

static void try_advance(RNetHashConfirm *hc);

void rnet_hc_note_local(RNetHashConfirm *hc, uint32_t tick, uint32_t digest)
{
    uint32_t i;
    if (!hc)
        return;
    i = slot_of(tick);
    hc->local_tick[i] = tick;
    hc->local_digest[i] = digest;
    hc->local_valid[i] = 1u;
    try_advance(hc);
}

static int local_at(const RNetHashConfirm *hc, uint32_t tick, uint32_t *dig)
{
    uint32_t i = slot_of(tick);
    if (!hc->local_valid[i] || hc->local_tick[i] != tick)
        return 0;
    if (dig)
        *dig = hc->local_digest[i];
    return 1;
}

static int peer_at(const RNetHashConfirm *hc, uint32_t tick, uint32_t *dig)
{
    uint32_t i = slot_of(tick);
    if (!hc->peer_valid[i] || hc->peer_tick[i] != tick)
        return 0;
    if (dig)
        *dig = hc->peer_digest[i];
    return 1;
}

static void try_advance(RNetHashConfirm *hc)
{
    for (;;) {
        uint32_t next;
        uint32_t ld = 0, pd = 0;
        if (!hc->resolved_valid)
            next = 0u;
        else {
            if (hc->resolved_through == 0xffffffffu)
                return;
            next = hc->resolved_through + 1u;
        }
        if (!local_at(hc, next, &ld) || !peer_at(hc, next, &pd))
            return;
        if (ld != pd || hc->quorum_conflict[slot_of(next)])
            return;
        hc->resolved_through = next;
        hc->resolved_valid = 1u;
    }
}

void rnet_hc_note_peer(RNetHashConfirm *hc, uint32_t tick, uint32_t digest)
{
    uint32_t i;
    if (!hc)
        return;
    i = slot_of(tick);
    hc->peer_tick[i] = tick;
    hc->peer_digest[i] = digest;
    hc->peer_valid[i] = 1u;
    hc->quorum_conflict[i] = 0u;
    try_advance(hc);
}

void rnet_hc_set_peer_mask(RNetHashConfirm *hc, uint32_t peer_mask)
{
    if (!hc)
        return;
    hc->peer_mask = peer_mask & ((1u << RNET_HC_MAX_PEERS) - 1u);
}

uint32_t rnet_hc_peer_mask(const RNetHashConfirm *hc)
{
    return hc ? hc->peer_mask : 0u;
}

void rnet_hc_note_peer_from(RNetHashConfirm *hc, int seat, uint32_t tick,
                            uint32_t digest)
{
    uint32_t i, s, first = 0u;
    uint8_t have_first = 0u, conflict = 0u;
    if (!hc)
        return;
    if (hc->peer_mask == 0u) {
        rnet_hc_note_peer(hc, tick, digest);
        return;
    }
    if (seat < 0 || (uint32_t)seat >= RNET_HC_MAX_PEERS ||
        !(hc->peer_mask & (1u << seat)))
        return;
    i = slot_of(tick);
    if (hc->quorum_tick[i] != tick || hc->quorum_seen[i] == 0u) {
        /* A new tick in this ring slot: forget the old one's commits. */
        hc->quorum_tick[i] = tick;
        hc->quorum_seen[i] = 0u;
        if (hc->peer_tick[i] != tick)
            hc->peer_valid[i] = 0u;
    }
    hc->quorum_digest[i][seat] = digest;
    hc->quorum_seen[i] = (uint8_t)(hc->quorum_seen[i] | (1u << seat));
    if ((hc->quorum_seen[i] & hc->peer_mask) != hc->peer_mask)
        return; /* someone has not committed this tick yet */
    for (s = 0; s < RNET_HC_MAX_PEERS; ++s) {
        if (!(hc->peer_mask & (1u << s)))
            continue;
        if (!have_first) {
            first = hc->quorum_digest[i][s];
            have_first = 1u;
        } else if (hc->quorum_digest[i][s] != first) {
            conflict = 1u;
        }
    }
    hc->peer_tick[i] = tick;
    hc->peer_digest[i] = first;
    hc->peer_valid[i] = 1u;
    hc->quorum_conflict[i] = conflict;
    try_advance(hc);
}

/* The peer digest to report for ring slot i: on a quorum conflict, one that
 * differs from the local digest (some seat must). */
static uint32_t reported_peer(const RNetHashConfirm *hc, uint32_t i,
                              uint32_t local)
{
    uint32_t s;
    if (!hc->quorum_conflict[i])
        return hc->peer_digest[i];
    for (s = 0; s < RNET_HC_MAX_PEERS; ++s)
        if ((hc->peer_mask & (1u << s)) && hc->quorum_digest[i][s] != local)
            return hc->quorum_digest[i][s];
    return hc->peer_digest[i];
}

uint32_t rnet_hc_resolved_through(const RNetHashConfirm *hc)
{
    if (!hc || !hc->resolved_valid)
        return 0u;
    return hc->resolved_through;
}

uint8_t rnet_hc_confirm_through(const RNetHashConfirm *hc, uint32_t tick)
{
    if (!hc || !hc->resolved_valid)
        return 0u;
    return (tick <= hc->resolved_through) ? 1u : 0u;
}

uint8_t rnet_hc_local_digest(const RNetHashConfirm *hc, uint32_t tick,
                            uint32_t *digest_out)
{
    uint32_t d = 0;
    if (!hc || !local_at(hc, tick, &d))
        return 0u;
    if (digest_out)
        *digest_out = d;
    return 1u;
}

uint8_t rnet_hc_peer_digest(const RNetHashConfirm *hc, uint32_t tick,
                           uint32_t *digest_out)
{
    uint32_t d = 0;
    if (!hc || !peer_at(hc, tick, &d))
        return 0u;
    if (digest_out)
        *digest_out = d;
    return 1u;
}

uint8_t rnet_hc_peek_mismatch(const RNetHashConfirm *hc, uint32_t *tick_out,
                             uint32_t *local_out, uint32_t *peer_out)
{
    uint32_t next;
    uint32_t ld = 0, pd = 0;
    if (!hc)
        return 0u;
    if (!hc->resolved_valid)
        next = 0u;
    else {
        if (hc->resolved_through == 0xffffffffu)
            return 0u;
        next = hc->resolved_through + 1u;
    }
    if (!local_at(hc, next, &ld) || !peer_at(hc, next, &pd))
        return 0u;
    if (ld == pd && !hc->quorum_conflict[slot_of(next)])
        return 0u;
    pd = reported_peer(hc, slot_of(next), ld);
    if (tick_out)
        *tick_out = next;
    if (local_out)
        *local_out = ld;
    if (peer_out)
        *peer_out = pd;
    return 1u;
}

uint8_t rnet_hc_heal_stale_gap(RNetHashConfirm *hc)
{
    uint32_t next;
    uint32_t best = 0u;
    uint8_t have_best = 0u;
    uint32_t i;
    uint32_t ld = 0, pd = 0;
    if (!hc || !hc->resolved_valid)
        return 0u;
    if (hc->resolved_through == 0xffffffffu)
        return 0u;
    next = hc->resolved_through + 1u;
    if (local_at(hc, next, &ld) && peer_at(hc, next, &pd))
        return 0u;
    if (local_at(hc, next, NULL) || peer_at(hc, next, NULL))
        return 0u;
    for (i = 0; i < RNET_HC_RING; i++) {
        uint32_t t;
        if (!hc->local_valid[i] || !hc->peer_valid[i])
            continue;
        if (hc->local_tick[i] != hc->peer_tick[i])
            continue;
        t = hc->local_tick[i];
        if (t <= hc->resolved_through)
            continue;
        if (hc->local_digest[i] != hc->peer_digest[i] || hc->quorum_conflict[i])
            return 0u;
        if (!have_best || t > best) {
            best = t;
            have_best = 1u;
        }
    }
    if (!have_best || best <= hc->resolved_through)
        return 0u;
    hc->resolved_through = best;
    return 1u;
}
