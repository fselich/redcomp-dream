/* BG1 ring observation. See ws_ring.h.
 *
 * The trailing margin of the ring holds authentic world cells; the leading
 * margin holds cells last written a full ring (512 px) earlier. The seen
 * table is the evidence that tells them apart: a margin slot is trustworthy
 * only while it still holds the world cell and tile word the native view
 * showed. The ring formula is in
 * openspec/changes/archive/2026-10-22-uniracers-ws-offwindow/report.md.
 */
#include "ws_ring.h"

#include "common_rtl.h"

#include <string.h>

static WsRingSeen s_seen[WS_RING_CELLS][WS_RING_CELLS];
static unsigned s_scene_generation;
static int s_scene_valid;
static uint16_t s_prev_map_base;
static uint8_t s_prev_bgmode;

static int32_t s_unwrapped_h, s_unwrapped_v;
static uint16_t s_prev_h, s_prev_v;
static int s_scroll_valid;

static int s_frame_active;
static int32_t s_base_h, s_base_v;
static uint16_t s_frame_raw_h, s_frame_raw_v;
static uint16_t s_map_base;

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

uint16_t WsRingMapAddr(int cx, int cy)
{
    return (uint16_t)((s_map_base + ((cy & 31) << 5) + (cx & 31)) & 0x7FFF);
}

void WsRingReset(void)
{
    memset(s_seen, 0, sizeof(s_seen));
    s_scroll_valid = 0;
    s_frame_active = 0;
}

static void WsRingUpdateScroll(uint16_t raw_h, uint16_t raw_v)
{
    if (!s_scroll_valid) {
        s_unwrapped_h = raw_h;
        s_unwrapped_v = raw_v;
        s_scroll_valid = 1;
    } else {
        s_unwrapped_h += WsWrapDelta10(raw_h, s_prev_h);
        s_unwrapped_v += WsWrapDelta10(raw_v, s_prev_v);
    }
    s_prev_h = raw_h;
    s_prev_v = raw_v;
}

/* From the frame's line-1 base plus this line's own register value, so
 * per-line scroll changes are handled without assuming one scroll per field. */
void WsRingLineScroll(const Ppu *ppu, int32_t *h, int32_t *v)
{
    *h = s_base_h + WsWrapDelta10(ppu->hScroll[0], s_frame_raw_h);
    *v = s_base_v + WsWrapDelta10(ppu->vScroll[0], s_frame_raw_v);
}

/* Record every ring cell the native view shows on this line. Runs before the
 * line is drawn, so boundary cells shared with a margin are already valid. */
static void WsRingObserve(const Ppu *ppu, int line)
{
    int32_t h, v;
    WsRingLineScroll(ppu, &h, &v);
    int32_t wy = WsRingFloor16((int64_t)v + line);
    int cy = wy & 31;
    int x = 0;
    while (x < 256) {
        int64_t sx = (int64_t)h + x;
        int32_t wx = WsRingFloor16(sx);
        int cx = wx & 31;
        WsRingSeen *s = &s_seen[cy][cx];
        s->wx = wx;
        s->wy = wy;
        s->tile = ppu->vram[WsRingMapAddr(cx, cy)];
        s->valid = 1;
        s->frame = snes_frame_counter;
        x += 16 - WsRingMod16(sx);
    }
}

int WsRingLine(Ppu *ppu, int line)
{
    int reset = 0;
    if (line == WS_RING_LINE_FIRST) {
        uint16_t map_base = PPU_bgTilemapAdr(ppu, 0);
        uint8_t mode = ppu->bgmode;
        /* NOW PLAYING (mode 3, base 0) can already satisfy InLiveRace. Do not
         * carry its seen entries or scroll unwrap into the race. */
        if (!s_scene_valid || map_base != s_prev_map_base ||
            mode != s_prev_bgmode) {
            WsRingReset();
            s_scene_generation++;
            s_scene_valid = 1;
            s_prev_map_base = map_base;
            s_prev_bgmode = mode;
            reset = 1;
        }
        WsRingUpdateScroll(ppu->hScroll[0], ppu->vScroll[0]);
        s_base_h = s_unwrapped_h;
        s_base_v = s_unwrapped_v;
        s_frame_raw_h = ppu->hScroll[0];
        s_frame_raw_v = ppu->vScroll[0];
        s_map_base = map_base;
        s_frame_active = 1;
    }
    if (s_frame_active) WsRingObserve(ppu, line);
    return reset;
}

int WsRingFrameActive(void) { return s_frame_active; }
void WsRingFrameEnd(void) { s_frame_active = 0; }

const WsRingSeen *WsRingSeenCell(int cy, int cx)
{
    return &s_seen[cy][cx];
}

unsigned WsRingSceneGeneration(void) { return s_scene_generation; }
int WsRingScrollValid(void) { return s_scroll_valid; }
int32_t WsRingScrollH(void) { return s_unwrapped_h; }
int32_t WsRingScrollV(void) { return s_unwrapped_v; }
