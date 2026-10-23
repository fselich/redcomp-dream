/* Rider cull-window widening. See ws_cull.h.
 *
 * The object cull compares screen X against kRamCullLeft/kRamCullRight. Those
 * words are written only by the rider setup at $81:A385.. and read only by the
 * five CMP sites at $82:AD19/ADE7/AEA7/AF75/AFFE, so shifting them is a pure
 * cull change: spawn, AI, camera, collision and RNG never see them.
 */
#include "ws_cull.h"

#include "variables.h"
#include "ws_frame.h"
#include "ws_settings.h"

static WsCullState s_cull;

int WsCullIsEnabled(void)
{
    return WsSettingBool(WS_SET_CULL);
}

void WsCullGet(WsCullState *s)
{
    *s = s_cull;
}

void WsCullSet(const WsCullState *s)
{
    s_cull = *s;
}

static void WsRestoreCull(void)
{
    if (!s_cull.on) return;
    /* Only restore if the game has not replaced our widened value. */
    if (ram_read16(kRamCullLeft) == s_cull.applied_l &&
        ram_read16(kRamCullRight) == s_cull.applied_r) {
        ram_write16(kRamCullLeft, s_cull.base_l);
        ram_write16(kRamCullRight, s_cull.base_r);
    }
    s_cull.on = 0;
}

void WsApplyCull(void)
{
    if (!WsWideEnabled() || !WsInLiveRace() || !WsCullIsEnabled()) {
        WsRestoreCull();
        return;
    }

    uint16_t cur_l = ram_read16(kRamCullLeft);
    uint16_t cur_r = ram_read16(kRamCullRight);

    /* Reuse the base while our widened values are in place. A different pair
     * means the game just installed a new per-section/per-rider base. */
    if (!s_cull.on || cur_l != s_cull.applied_l || cur_r != s_cull.applied_r) {
        /* Wait for a plausible native screen window: setup writes 0 first. */
        if ((int16_t)cur_l > 0 || cur_r < 128) {
            s_cull.on = 0;
            return;
        }
        s_cull.base_l = cur_l;
        s_cull.base_r = cur_r;
    }

    /* The cull compares rel << kRamCullZoomShift (Zoom Zoo: 2, base
     * [-196,1024] = [-49,256] px x4), so the margin must be scaled the same
     * way. */
    uint16_t zoom = ram_read16(kRamCullZoomShift);
    int extra = WsExtra() << (zoom <= 6 ? zoom : 0);   /* seen: 0, 2, 4 */
    s_cull.applied_l = (uint16_t)((int16_t)s_cull.base_l - extra);
    s_cull.applied_r = (uint16_t)(s_cull.base_r + extra);
    ram_write16(kRamCullLeft, s_cull.applied_l);
    ram_write16(kRamCullRight, s_cull.applied_r);
    s_cull.on = 1;
}
