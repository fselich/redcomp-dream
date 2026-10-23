/* Local radar panel.
 *
 * Owns the 64x32 RGB15 panel and reads [Video] MiniMap from the settings
 * model each frame. Presentation-only: it adds a PpuExtraObject and never
 * writes guest memory.
 */
#ifndef WS_RADAR_H
#define WS_RADAR_H

#include "snes/ppu.h"
#include "ws_frame.h"

void WsRadarUpdate(Ppu *ppu, const WsFrameCtx *ctx);

#endif /* WS_RADAR_H */
