/* BG1 ring observation, margin policy and tile hook.
 *
 * Owns the 32x32 seen table (world cell + tile word), the 10-bit scroll
 * unwrap, the per-line hook and the margin policy (`wrap`, `ring`, `source`,
 * `ghost`, `clamp`). Presentation-only; reads guest memory and VRAM.
 */
#ifndef WS_RING_H
#define WS_RING_H

#include <stdint.h>

#include "snes/ppu.h"

enum { WS_OW_CELLS = 32, WS_OW_LINE_FIRST = 1, WS_OW_LINE_LAST = 224 };

typedef struct {
    int32_t wx, wy;
    uint16_t tile;
    uint8_t valid;
    int frame;      /* snes_frame_counter of the last native observation */
} WsOwSeen;

enum {
    WS_OW_WRAP = 0,
    WS_OW_RING = 1,
    WS_OW_SOURCE = 2,
    WS_OW_GHOST = 3,
    WS_OW_CLAMP = 4,
};

int WsOffwindowMode(void);
int WsOffwindowHooked(int mode);
int WsOwVerbose(void);
void WsOwReset(void);
void WsOffwindowLine(Ppu *ppu, unsigned line, void *context);
uint16_t WsOffwindowTile(Ppu *ppu, unsigned layer, int screen_x, int line,
                         uint16_t tile, uint16_t sc, void *context);

/* Shared ring math. */
int32_t WsRingFloor16(int64_t v);
int WsRingMod16(int64_t v);
int WsWrapDelta10(uint16_t now, uint16_t prev);

typedef void (*WsRingLineSink)(void *context, int cx, int cy, int32_t wx,
                               int32_t wy, uint16_t tile);
void WsRingWalkLine(Ppu *ppu, uint16_t map_base, int32_t h, int32_t v,
                    int line, WsRingLineSink sink, void *context);

/* Cross-module reads. */
const WsOwSeen *WsRingSeenCell(int cy, int cx);
unsigned WsRingSceneGeneration(void);
int WsRingSceneValid(void);
uint16_t WsRingMapBase(void);
uint8_t WsRingPrevBgMode(void);
int WsRingScrollValid(void);
int32_t WsRingScrollH(void);
int32_t WsRingScrollV(void);
int WsRingScrollSeed(int32_t *h, int32_t *v, uint16_t *ph, uint16_t *pv);
void WsRingFrameEnd(int ow_mode);
void WsRingLogLive(int live);

#endif /* WS_RING_H */
