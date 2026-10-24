/* BG1 ring observation. See ws_ring.h.
 *
 * The trailing margin of the ring holds authentic world cells; the leading
 * margin holds cells last written a full ring (512 px) earlier. The seen
 * table is the evidence that tells them apart: a margin slot is trustworthy
 * only while it still holds the world cell and tile word the native view
 * showed. The ring formula is in
 * openspec/changes/archive/2026-10-22-uniracers-ws-offwindow/report.md.
 *
 * 2P has two independent cameras and two BG1 map bases (measured on Looper:
 * 0x0C00 on lines 0..113, 0x1C00 from line 114). Every piece of state is per
 * viewport; viewport 0 is the top half (and the whole frame in 1P), viewport 1
 * is the bottom half.
 */
#include "ws_ring.h"

#include "common_rtl.h"

#include <string.h>

typedef struct {
    unsigned scene_generation;
    int scene_valid, scroll_valid, frame_active;
    uint16_t prev_map_base, map_base;
    uint8_t prev_bgmode;
    uint16_t prev_h, prev_v, frame_raw_h, frame_raw_v;
    int32_t unwrapped_h, unwrapped_v, base_h, base_v;
    WsRingSeen seen[WS_RING_CELLS][WS_RING_CELLS];
} WsRingVp;

static WsRingVp s_vp[WS_VP_MAX];

static int WsRingVpFirst(int vp)
{
    return vp == 0 ? WS_VP_TOP_FIRST : WS_VP_BOTTOM_FIRST;
}

static WsRingVp *WsRingVpGet(int vp)
{
    if (vp < 0 || vp >= WS_VP_MAX) vp = 0;
    return &s_vp[vp];
}

int32_t WsRingFloor16(int64_t v)
{
    return (int32_t)(v >= 0 ? v / 16 : -(((-v) + 15) / 16));
}

int WsRingMod16(int64_t v)
{
    int m = (int)(v % 16);
    return m < 0 ? m + 16 : m;
}

int WsWrapDelta10(uint16_t now, uint16_t prev)
{
    int d = (int)((now - prev) & 0x3FF);
    return d >= 512 ? d - 1024 : d;
}

uint16_t WsRingMapAddr(int vp, int cx, int cy)
{
    WsRingVp *v = WsRingVpGet(vp);
    return (uint16_t)((v->map_base + ((cy & 31) << 5) + (cx & 31)) & 0x7FFF);
}

void WsRingReset(void)
{
    for (int vp = 0; vp < WS_VP_MAX; vp++) {
        memset(s_vp[vp].seen, 0, sizeof(s_vp[vp].seen));
        s_vp[vp].scroll_valid = 0;
        s_vp[vp].frame_active = 0;
    }
}

static void WsRingUpdateScroll(WsRingVp *v, uint16_t raw_h, uint16_t raw_v)
{
    if (!v->scroll_valid) {
        v->unwrapped_h = raw_h;
        v->unwrapped_v = raw_v;
        v->scroll_valid = 1;
    } else {
        v->unwrapped_h += WsWrapDelta10(raw_h, v->prev_h);
        v->unwrapped_v += WsWrapDelta10(raw_v, v->prev_v);
    }
    v->prev_h = raw_h;
    v->prev_v = raw_v;
}

/* From the viewport's first-line base plus this line's own register value, so
 * per-line scroll changes are handled without assuming one scroll per field. */
void WsRingLineScroll(const Ppu *ppu, int vp, int32_t *h, int32_t *v)
{
    WsRingVp *s = WsRingVpGet(vp);
    *h = s->base_h + WsWrapDelta10(ppu->hScroll[0], s->frame_raw_h);
    *v = s->base_v + WsWrapDelta10(ppu->vScroll[0], s->frame_raw_v);
}

/* Record every ring cell the native view shows on this line. Runs before the
 * line is drawn, so boundary cells shared with a margin are already valid. */
static void WsRingObserve(Ppu *ppu, int line, int vp)
{
    WsRingVp *s = WsRingVpGet(vp);
    int32_t h, v;
    WsRingLineScroll(ppu, vp, &h, &v);
    int32_t wy = WsRingFloor16((int64_t)v + line);
    int cy = wy & 31;
    int x = 0;
    while (x < 256) {
        int64_t sx = (int64_t)h + x;
        int32_t wx = WsRingFloor16(sx);
        int cx = wx & 31;
        WsRingSeen *seen = &s->seen[cy][cx];
        seen->wx = wx;
        seen->wy = wy;
        seen->tile = ppu->vram[WsRingMapAddr(vp, cx, cy)];
        seen->valid = 1;
        seen->frame = snes_frame_counter;
        x += 16 - WsRingMod16(sx);
    }
}

int WsRingLine(Ppu *ppu, int line, int vp)
{
    if (!ppu) return 0;
    WsRingVp *s = WsRingVpGet(vp);
    int reset = 0;
    if (line == WsRingVpFirst(vp)) {
        uint16_t map_base = PPU_bgTilemapAdr(ppu, 0);
        uint8_t mode = ppu->bgmode;
        /* NOW PLAYING (mode 3, base 0) can already satisfy InLiveRace. Do not
         * carry its seen entries or scroll unwrap into the race. */
        if (!s->scene_valid || map_base != s->prev_map_base ||
            mode != s->prev_bgmode) {
            memset(s->seen, 0, sizeof(s->seen));
            s->scroll_valid = 0;
            s->frame_active = 0;
            s->scene_generation++;
            s->scene_valid = 1;
            s->prev_map_base = map_base;
            s->prev_bgmode = mode;
            reset = 1;
        }
        WsRingUpdateScroll(s, ppu->hScroll[0], ppu->vScroll[0]);
        s->base_h = s->unwrapped_h;
        s->base_v = s->unwrapped_v;
        s->frame_raw_h = ppu->hScroll[0];
        s->frame_raw_v = ppu->vScroll[0];
        s->map_base = map_base;
        s->frame_active = 1;
    }
    if (s->frame_active) WsRingObserve(ppu, line, vp);
    return reset;
}

int WsRingFrameActive(int vp) { return WsRingVpGet(vp)->frame_active; }

void WsRingFrameEnd(void)
{
    for (int vp = 0; vp < WS_VP_MAX; vp++) s_vp[vp].frame_active = 0;
}

const WsRingSeen *WsRingSeenCell(int vp, int cy, int cx)
{
    return &WsRingVpGet(vp)->seen[cy][cx];
}

unsigned WsRingSceneGeneration(int vp)
{
    return WsRingVpGet(vp)->scene_generation;
}

int WsRingScrollValid(void) { return s_vp[0].scroll_valid; }
int32_t WsRingScrollH(void) { return s_vp[0].unwrapped_h; }
int32_t WsRingScrollV(void) { return s_vp[0].unwrapped_v; }
