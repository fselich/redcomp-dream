/* BG1 ring observation.
 *
 * The race BG1 map is a 32x32 ring of 16x16 cells (512 px each way) that the
 * game rewrites as the camera moves. This module records, per ring slot, the
 * world cell and tile word the native 256 px view showed (the "seen" table),
 * and unwraps the 10-bit scroll registers into an absolute world scroll. It
 * holds no policy: ws_margin.[ch] decides what the margins show.
 *
 * 2P split screen has two independent cameras and two BG1 maps, so every
 * piece of this state exists per viewport. 1P uses viewport 0 only.
 * Presentation-only; reads guest VRAM and PPU registers.
 */
#ifndef WS_RING_H
#define WS_RING_H

#include <stdint.h>

#include "snes/ppu.h"
#include "ws_frame.h"

enum { WS_RING_CELLS = 32, WS_RING_LINE_FIRST = WS_VP_TOP_FIRST };

typedef struct {
    int32_t wx, wy;
    uint16_t tile;
    uint8_t valid;
    int frame;      /* snes_frame_counter of the last native observation */
} WsRingSeen;

/* Forget both viewports' seen tables and scroll unwraps. */
void WsRingReset(void);
/* Per-line observation, before the line is drawn. On the viewport's first
 * visible line it starts the frame (and resets that viewport's scene when its
 * BG1 map base or BG mode changed); every line records the cells the native
 * view shows. vp is 0 in 1P, 0/1 in 2P. Returns 1 when this call reset the
 * viewport's scene. */
int WsRingLine(Ppu *ppu, int line, int vp);
/* 1 between the viewport's first observed line and WsRingFrameEnd(). */
int WsRingFrameActive(int vp);
void WsRingFrameEnd(void);

/* Absolute scroll of the line being rasterized for a viewport. */
void WsRingLineScroll(const Ppu *ppu, int vp, int32_t *h, int32_t *v);
/* VRAM word address of ring slot (cx, cy) in the viewport's BG1 map. */
uint16_t WsRingMapAddr(int vp, int cx, int cy);

const WsRingSeen *WsRingSeenCell(int vp, int cy, int cx);
unsigned WsRingSceneGeneration(int vp);

/* Viewport-0 wrappers, kept for the radar. */
int WsRingScrollValid(void);
int32_t WsRingScrollH(void);
int32_t WsRingScrollV(void);

/* Shared ring math. */
int32_t WsRingFloor16(int64_t v);
int WsRingMod16(int64_t v);
int WsWrapDelta10(uint16_t now, uint16_t prev);

#endif /* WS_RING_H */
