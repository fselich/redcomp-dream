/* Rider OAM presentation fixes. See ws_oam_fix.c. */
#ifndef WS_OAM_FIX_H
#define WS_OAM_FIX_H

#include "snes/ppu.h"

/* X-high/parked fixes plus the trace-only TEST_OAM injector. */
void WsOamFix(Ppu *ppu);

#endif /* WS_OAM_FIX_H */
