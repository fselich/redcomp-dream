/* Local radar panel.
 *
 * Owns the 64x32 RGB15 panel and the [Video] MiniMap read. Presentation-only:
 * it adds a PpuExtraObject and never writes guest memory.
 */
#ifndef WS_RADAR_H
#define WS_RADAR_H

#include "snes/ppu.h"
#include "ws_frame.h"

void WsRadarAfterConfig(void);
void WsRadarUpdate(Ppu *ppu, const WsFrameCtx *ctx);

#endif /* WS_RADAR_H */
