/* Widescreen BG1 margin policy.
 *
 * Owns what the BG1 margins (screen X outside 0..255) show in a live race:
 *   source  ring-verified cells, then the $7F track table (default)
 *   clamp   BG1 limited to the native 256 px; margins show the backdrop
 * It installs the per-line and margin-tile hooks around the draw, applies the
 * clamp, and resets the ring when the policy changes. Presentation-only.
 */
#ifndef WS_MARGIN_H
#define WS_MARGIN_H

#include "ws_frame.h"

enum {
    WS_MARGIN_NONE = 0,     /* not a live widescreen race */
    WS_MARGIN_SOURCE = 2,
    WS_MARGIN_CLAMP = 4,
};

/* The configured policy, read every call so the menu applies live. */
int WsMarginPolicy(void);
/* Before the PPU draw: clamp BG1 or install the margin hooks. */
void WsMarginFrameBegin(const WsFrameCtx *ctx);
/* After the draw: remove the hooks and advance the source anchor. */
void WsMarginFrameEnd(void);
/* Drop the ring state (scene change or a policy change). */
void WsMarginReset(void);

#endif /* WS_MARGIN_H */
