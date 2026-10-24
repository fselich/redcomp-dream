/* Widescreen HUD bands. See ws_hud.h. */
#include "ws_hud.h"

#include "ws_frame.h"
#include "ws_settings.h"

static int WsHudEnabled(void)
{
    return WsSettingBool(WS_SET_HUD);
}

static void WsHudClearBands(Ppu *ppu, int count)
{
    for (int slot = 0; slot < count; slot++)
        PpuSetWidescreenLayerElasticBandSlot(ppu, (uint8_t)slot, 2, 0, 0,
                                             NULL, 0);
}

void WsApplyHud(Ppu *ppu, const WsFrameCtx *ctx)
{
    if (!ppu) return;

    if (!ctx || !ctx->active || !WsHudEnabled()) {
        WsHudClearBands(ppu, 3);
        PpuSetWidescreenLayerClampBand(ppu, 2, 0, 224);
        PpuSetWidescreenBg3Widen(ppu, 0);
        return;
    }

    if (ctx->two_player) {
        /* Each half has its own RACE/timer pair and player name. Three bands
         * cover the top half, the divider and the bottom half; the columns no
         * segment covers stay empty, which clamps the rest of BG3 without a
         * clamp band (a clamp band would disable the elastic bands). */
        const int e = ctx->extra;
        const PpuWsElasticSeg half[3] = {
            { 17, 64, (int16_t)(4 - e), (int16_t)(51 - e) },
            { 64, 192, 64, 192 },
            { 192, 256, (int16_t)(192 + e), (int16_t)(256 + e) },
        };
        const PpuWsElasticSeg divider[1] = {
            { 0, 256, (int16_t)(-e), (int16_t)(256 + e) },
        };
        PpuSetWidescreenLayerClampBand(ppu, 2, 0, 0);
        PpuSetWidescreenBg3Widen(ppu, 10);
        PpuSetWidescreenLayerElasticBandSlot(ppu, 0, 2,
                                             WS_VP_TOP_FIRST + 9,
                                             WS_VP_DIVIDER_FIRST,
                                             half, 3);
        PpuSetWidescreenLayerElasticBandSlot(ppu, 1, 2,
                                             WS_VP_DIVIDER_FIRST,
                                             WS_VP_BOTTOM_FIRST,
                                             divider, 1);
        PpuSetWidescreenLayerElasticBandSlot(ppu, 2, 2,
                                             WS_VP_BOTTOM_FIRST,
                                             WS_VP_END, half, 3);
        return;
    }

    /* 1P: top groups are elastic, the lower prompt/arrow/lap-time rows keep
     * the authentic clamp. The layer-wide clamp would win over the elastic
     * band, so it must not be set here. Line 31 is the bottom row of the lap
     * counter/timer glyphs (Zoom Zoo's "/" leaves a stray pixel there when it
     * is clamped), so the elastic band ends at 32 and the lower clamp starts
     * there. Clear slots 1 and 2 so a 2P band cannot survive into 1P. */
    WsHudClearBands(ppu, 3);
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
