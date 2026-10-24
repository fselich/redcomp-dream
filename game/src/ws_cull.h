/* Rider cull-window widening.
 *
 * This is the ONLY module that writes guest WRAM in normal play: it shifts
 * kRamCullLeft/Right by the widescreen margin and restores the game's base
 * when widescreen is off or the race ends. Everything else in the port is
 * presentation-only. See
 * openspec/changes/archive/2026-10-20-uniracers-ws-objects.
 */
#ifndef WS_CULL_H
#define WS_CULL_H

#include <stdint.h>

int WsCullIsEnabled(void);
int WsCullPairLooksLive(uint16_t left, uint16_t right);
int WsCullLooksLive(void);
void WsApplyCull(void);

#endif /* WS_CULL_H */
