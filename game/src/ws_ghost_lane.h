/* Speculative ghost lane (policy `ghost`).
 *
 * Owns the run-ahead snapshot/restore and the world-keyed BG1 cell table.
 * It restores the real machine before the real field is drawn; simulation
 * output is unchanged.
 */
#ifndef WS_GHOST_LANE_H
#define WS_GHOST_LANE_H

#include <stdint.h>

struct SnesDesktopHostFrameStats;

int WsGhostLookup(int cx, int cy, int32_t wx, int32_t wy, uint16_t *tile);
void WsGhostAfterRunFrame(const struct SnesDesktopHostFrameStats *stats);

#endif /* WS_GHOST_LANE_H */
