/* Widescreen HUD bands. See ws_hud.h. */
#include "ws_hud.h"

#include "ws_frame.h"
#include "ws_settings.h"

static int WsHudEnabled(void)
{
    return WsSettingBool(WS_SET_HUD);
}

void WsApplyHud(Ppu *ppu, const WsFrameCtx *ctx)
{
    if (!ppu) return;

    if (!ctx || !ctx->active || !WsHudEnabled()) {
        PpuSetWidescreenLayerElasticBandSlot(ppu, 0, 2, 0, 0, NULL, 0);
        PpuSetWidescreenBg3Widen(ppu, 0);
        if (ctx && ctx->active)
            PpuSetWidescreenLayerClamp(ppu, 1u << 2);
        return;
    }

    /* BG3 is per-band: top groups are elastic, the lower prompt/arrow/
     * lap-time rows keep the authentic clamp. The layer-wide clamp would win
     * over the elastic band, so it must not be set here. Line 31 is the
     * bottom row of the lap counter/timer glyphs (Zoom Zoo's "/" leaves a
     * stray pixel there when it is clamped), so the elastic band ends at 32
     * and the lower clamp starts there. */
    PpuSetWidescreenLayerClampBand(ppu, 2, 32, 224);
    /* BG3 needs its own widen gate before the elastic band is consulted. */
    PpuSetWidescreenBg3Widen(ppu, 17);
    /* Segments follow the margin E (43 at 16:9: -39..8 and 236..299). */
    const int e = ctx->extra;
    const PpuWsElasticSeg segs[2] = {
        { 17, 64, (int16_t)(4 - e), (int16_t)(51 - e) },     /* left group + 4 px inset, 1:1 */
        { 193, 256, (int16_t)(193 + e), (int16_t)(256 + e) }, /* timer + right inset, 1:1 */
    };
    PpuSetWidescreenLayerElasticBandSlot(ppu, 0, 2, 17, 32, segs, 2);
}
