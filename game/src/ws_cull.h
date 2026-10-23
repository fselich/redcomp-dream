/* Rider cull-window widening.
 *
 * This is the ONLY module that writes guest WRAM in normal play: it shifts
 * kRamCullLeft/Right by the widescreen margin and restores the game's base
 * when widescreen is off or the race ends. Everything else in the port is
 * presentation-only. See openspec/changes/uniracers-ws-objects.
 */
#ifndef WS_CULL_H
#define WS_CULL_H

#include "types.h"

typedef struct {
    int on;
    uint16_t base_l, base_r, applied_l, applied_r;
} WsCullState;

int WsCullIsEnabled(void);
void WsApplyCull(void);
void WsCullGet(WsCullState *s);
void WsCullSet(const WsCullState *s);

#endif /* WS_CULL_H */
