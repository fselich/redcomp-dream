/* Widescreen HUD (BG3) bands.
 *
 * Owns the elastic top band (RACE / lap counter / timer) and the authentic
 * clamp for the lower prompt/arrow/lap-time rows. Presentation-only: writes
 * PPU configuration, never guest memory.
 */
#ifndef WS_HUD_H
#define WS_HUD_H

#include "snes/ppu.h"
#include "ws_frame.h"

void WsApplyHud(Ppu *ppu, const WsFrameCtx *ctx);

#endif /* WS_HUD_H */
