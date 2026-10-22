#include "nat/rnet_sig_hold.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RNetSigHoldBucket *find(const RNetSigHold *h, const char *pid)
{
    int i;
    if (h == NULL || pid == NULL || pid[0] == '\0')
        return NULL;
    for (i = 0; i < RNET_SIG_HOLD_PEERS; ++i)
        if (h->b[i].e != NULL && strcmp(h->b[i].player_id, pid) == 0)
            return (RNetSigHoldBucket *)&h->b[i];
    return NULL;
}

static void free_bucket(RNetSigHoldBucket *b)
{
    free(b->e);
    memset(b, 0, sizeof(*b));
}

int rnet_sig_hold_push(RNetSigHold *h, const char *pid, int slot, const RNetSignal *sig,
                       rnet_u64 now_ms)
{
    RNetSigHoldBucket *b;
    int i;
    if (h == NULL || sig == NULL || pid == NULL || pid[0] == '\0')
        return -1;
    b = find(h, pid);
    if (b == NULL)
    {
        for (i = 0; i < RNET_SIG_HOLD_PEERS && b == NULL; ++i)
            if (h->b[i].e == NULL)
                b = &h->b[i];
        if (b == NULL)
        {
            h->dropped++;
            return -1;
        }
        b->e = (RNetSigHoldEntry *)calloc(RNET_SIG_HOLD_PER_PEER, sizeof(RNetSigHoldEntry));
        if (b->e == NULL)
        {
            h->dropped++;
            return -1;
        }
        snprintf(b->player_id, sizeof(b->player_id), "%s", pid);
        b->n = 0;
        b->born_ms = now_ms;
    }
    if (b->n >= RNET_SIG_HOLD_PER_PEER)
    {
        h->dropped++;
        return -1;
    }
    b->e[b->n].sig = *sig;
    b->e[b->n].slot = slot;
    b->n++;
    return 0;
}

int rnet_sig_hold_count(const RNetSigHold *h, const char *pid)
{
    const RNetSigHoldBucket *b = find(h, pid);
    return b ? b->n : 0;
}

const RNetSigHoldEntry *rnet_sig_hold_get(const RNetSigHold *h, const char *pid, int index)
{
    const RNetSigHoldBucket *b = find(h, pid);
    if (b == NULL || index < 0 || index >= b->n)
        return NULL;
    return &b->e[index];
}

void rnet_sig_hold_drop(RNetSigHold *h, const char *pid)
{
    RNetSigHoldBucket *b = find(h, pid);
    if (b != NULL)
        free_bucket(b);
}

void rnet_sig_hold_clear(RNetSigHold *h)
{
    int i;
    if (h == NULL)
        return;
    for (i = 0; i < RNET_SIG_HOLD_PEERS; ++i)
        free_bucket(&h->b[i]);
}

void rnet_sig_hold_expire(RNetSigHold *h, rnet_u64 now_ms, rnet_u64 max_age_ms)
{
    int i;
    if (h == NULL)
        return;
    for (i = 0; i < RNET_SIG_HOLD_PEERS; ++i)
        if (h->b[i].e != NULL && now_ms >= h->b[i].born_ms &&
            now_ms - h->b[i].born_ms > max_age_ms)
            free_bucket(&h->b[i]);
}
