/* Rider OAM presentation fixes. See ws_oam_fix.c. */
#ifndef WS_OAM_FIX_H
#define WS_OAM_FIX_H

#include "snes/ppu.h"

/* X-high/parked fixes plus the trace-only TEST_OAM injector. */
void WsOamFix(Ppu *ppu);

/* Per-line 2P high-table fix: keep the bottom half's own rider big and at its
 * true X. Called by the beam line hook before the line is drawn. */
void WsOamLineFix(Ppu *ppu, int line);

#endif /* WS_OAM_FIX_H */
