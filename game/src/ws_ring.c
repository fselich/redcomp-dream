/* BG1 ring observation and margin policy. See ws_ring.h. */
#include "ws_ring.h"

#include "common_rtl.h"
#include "variables.h"
#include "ws_debug.h"
#include "ws_frame.h"
#include "ws_settings.h"
#include "ws_source.h"
#include "ws_ghost_lane.h"

#include <stdio.h>
#include <string.h>

/* Test-only prototype: per-cell verified BG1 ring margins.
 *
 * The ring already holds authentic world cells on the trailing margin. The
 * leading margin holds cells last written a full ring (512 px) earlier. Track
 * a 32x32 "seen" table from the native view (world cell + tile word) and let
 * the renderer substitute a transparent entry for every margin cell that does
 * not match it. This runs inside PpuDrawBackgroundBig, so it never writes
 * guest VRAM: a host-side bracket would also hit the HUD, which uses a
 * different map base in its scanline bands. See
 * openspec/changes/uniracers-ws-leading-edge (Option A). */
static WsOwSeen s_ow_seen[WS_OW_CELLS][WS_OW_CELLS];
static uint8_t s_ow_logged[WS_OW_CELLS][WS_OW_CELLS];
static unsigned s_ow_scene_generation;

static int32_t s_ow_unwrapped_h, s_ow_unwrapped_v;
static uint16_t s_ow_prev_h, s_ow_prev_v;
static int s_ow_scroll_valid;

static int s_ow_frame_active;
static int32_t s_ow_base_h, s_ow_base_v;
static uint16_t s_ow_frame_raw_h, s_ow_frame_raw_v;
static uint16_t s_ow_map_base;
static uint16_t s_ow_prev_map_base;
static uint8_t s_ow_prev_bgmode;
static int s_ow_scene_valid;
static uint16_t s_ow_mask_word = 0xFFFF;
static int s_ow_announced;
static int s_ow_live_prev = -1;

/* 16:9 BG1 margin policy (openspec/changes/uniracers-ws-source-default):
 *   0 wrap   raw 512-px ring in the margins (pre-fix look; `wrap`/`off`/`0`)
 *   1 ring   verified ring cells, mask otherwise
 *   2 source ring-verified, then the $7F track table (DEFAULT)
 *   3 ghost  ring + speculative lane (parked: too slow; environment only)
 *   4 clamp  global BG1 clamp to the native 256 px
 * The policy comes from the settings model: SNESRECOMP_WS_OFFWINDOW, else the
 * legacy SNESRECOMP_WS_TRACK_CLAMP (clamp), else [Widescreen] Margin, else
 * source. It is read every call so the menu can change it live. */
int WsOffwindowMode(void)
{
    const char *m = WsSettingValue(WS_SET_MARGIN);
    if (WsCiEq(m, "ring")) return WS_OW_RING;
    if (WsCiEq(m, "ghost")) return WS_OW_GHOST;
    if (WsCiEq(m, "clamp")) return WS_OW_CLAMP;
    if (WsCiEq(m, "wrap")) return WS_OW_WRAP;
    return WS_OW_SOURCE;
}

/* Margin hooks run for the modes that substitute margin cells. */
int WsOffwindowHooked(int mode)
{
    return mode == WS_OW_RING || mode == WS_OW_SOURCE || mode == WS_OW_GHOST;
}

int WsOwVerbose(void)
{
    static int v = -1;
    if (v < 0) {
        const char *a = WsEnvStr("SNESRECOMP_WS_OFFWINDOW_LOG");
        const char *b = WsEnvStr("SNESRECOMP_WS_SOURCE_LOG");
        v = (a && *a) || (b && *b);
    }
    return v;
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

static uint16_t WsOwMapAddrBase(int cx, int cy)
{
    return (uint16_t)((s_ow_map_base + ((cy & 31) << 5) + (cx & 31)) & 0x7FFF);
}

void WsOwReset(void)
{
    memset(s_ow_seen, 0, sizeof(s_ow_seen));
    s_ow_scroll_valid = 0;
    s_ow_frame_active = 0;
}

static void WsOwLogVisit(int cx, int cy, int32_t wx, int32_t wy,
                         uint16_t real, uint16_t served, int masked)
{
    FILE *f = WsLogOpen("SNESRECOMP_WS_OFFWINDOW_LOG",
                        "# frame,cell_x,cell_y,world_x,world_y,real,mask,masked");
    if (!f) return;
    fprintf(f, "%d,%d,%d,%d,%d,0x%04x,0x%04x,%d\n",
            snes_frame_counter, cx, cy, wx, wy, real, served, masked);
    fflush(f);
}

/* Find a 16x16 character whose four 8x8 sub-characters are all zero words:
 * pixel 0 in every position, so a map word pointing at it is transparent for
 * any palette/priority. Returns the character index or -1. */
static int WsOwFindBlankChar(Ppu *ppu)
{
    int tileadr = PPU_bgTileAdr(ppu, 0);
    for (int c = 0; c <= 1023 - 17; c++) {
        int ok = 1;
        for (int q = 0; q < 4 && ok; q++) {
            int ch = c + (q & 1) + ((q >> 1) << 4);
            int off = (tileadr + ch * 16) & 0x7FFF;
            for (int i = 0; i < 16; i++) {
                if (ppu->vram[(off + i) & 0x7FFF]) { ok = 0; break; }
            }
        }
        if (ok) return c;
    }
    return -1;
}

static void WsOwUpdateScroll(uint16_t raw_h, uint16_t raw_v)
{
    if (!s_ow_scroll_valid) {
        s_ow_unwrapped_h = raw_h;
        s_ow_unwrapped_v = raw_v;
        s_ow_scroll_valid = 1;
    } else {
        s_ow_unwrapped_h += WsWrapDelta10(raw_h, s_ow_prev_h);
        s_ow_unwrapped_v += WsWrapDelta10(raw_v, s_ow_prev_v);
    }
    s_ow_prev_h = raw_h;
    s_ow_prev_v = raw_v;
}

/* Absolute scroll for the line currently being rasterized, from the frame's
 * line-1 base plus that line's own register value. Handles per-line changes
 * without assuming the whole field shares one scroll. */
static void WsOwLineScroll(Ppu *ppu, int32_t *h, int32_t *v)
{
    *h = s_ow_base_h + WsWrapDelta10(ppu->hScroll[0], s_ow_frame_raw_h);
    *v = s_ow_base_v + WsWrapDelta10(ppu->vScroll[0], s_ow_frame_raw_v);
}

/* Record every ring cell the native view shows on this line. Called before the
 * line is drawn, so boundary cells shared with a margin are already valid. */
static void WsOwSeenSink(void *context, int cx, int cy, int32_t wx, int32_t wy,
                         uint16_t tile)
{
    (void)context;
    WsOwSeen *s = &s_ow_seen[cy][cx];
    s->wx = wx;
    s->wy = wy;
    s->tile = tile;
    s->valid = 1;
    s->frame = snes_frame_counter;
}

static void WsOwObserveLine(Ppu *ppu, int line)
{
    int32_t h, v;
    WsOwLineScroll(ppu, &h, &v);
    WsRingWalkLine(ppu, s_ow_map_base, h, v, line, &WsOwSeenSink, NULL);
}

/* Per-line host hook (beam_frame_driver) run after the journal and HDMA for
 * this line, before it is drawn. */
void WsOffwindowLine(Ppu *ppu, unsigned line, void *context)
{
    (void)context;
    if (!ppu) return;
    if ((int)line == WS_OW_LINE_FIRST) {
        uint16_t map_base = PPU_bgTilemapAdr(ppu, 0);
        uint8_t mode = ppu->bgmode;
        /* NOW PLAYING (mode 3, base 0) can already satisfy InLiveRace. Do not
         * carry its seen entries or scroll unwrap into the race. */
        if (!s_ow_scene_valid || map_base != s_ow_prev_map_base ||
            mode != s_ow_prev_bgmode) {
            WsOwReset();
            s_ow_scene_generation++;
            s_ow_scene_valid = 1;
            s_ow_prev_map_base = map_base;
            s_ow_prev_bgmode = mode;
            if (WsOwVerbose())
                fprintf(stderr, "[ws-ow] scene reset: base=%04x mode=%02x "
                                "scroll=%u,%u\n", map_base, mode,
                        ppu->hScroll[0], ppu->vScroll[0]);
        }
        memset(s_ow_logged, 0, sizeof(s_ow_logged));
        s_ow_mask_word = 0xFFFF;
        int blank = WsOwFindBlankChar(ppu);
        if (blank >= 0) s_ow_mask_word = (uint16_t)(blank & 0x3FF);
        WsOwUpdateScroll(ppu->hScroll[0], ppu->vScroll[0]);
        s_ow_base_h = s_ow_unwrapped_h;
        s_ow_base_v = s_ow_unwrapped_v;
        s_ow_frame_raw_h = ppu->hScroll[0];
        s_ow_frame_raw_v = ppu->vScroll[0];
        s_ow_map_base = map_base;
        s_ow_frame_active = 1;
        if (!s_ow_announced && WsOwVerbose()) {
            s_ow_announced = 1;
            fprintf(stderr, "[ws-ow] active: mode=%d map_base=%04x "
                            "scroll=%u,%u extra=%d/%d\n",
                    WsOffwindowMode(), s_ow_map_base, s_ow_frame_raw_h,
                    s_ow_frame_raw_v, ppu->extraLeftCur, ppu->extraRightCur);
        }
    }
    if (!s_ow_frame_active) return;
    WsOwObserveLine(ppu, (int)line);
}

/* Renderer hook: decide the tile word for a margin sample of BG1. The map
 * address is checked against the race BG1 ring so HUD bands that repoint the
 * layer's map base are left alone. */
uint16_t WsOffwindowTile(Ppu *ppu, unsigned layer, int screen_x,
                         int line, uint16_t tile, uint16_t sc,
                         void *context)
{
    (void)context;
    if (!s_ow_frame_active || layer != 0) return tile;
    if (screen_x >= 0 && screen_x < 256) return tile;

    int32_t h, v;
    WsOwLineScroll(ppu, &h, &v);
    int32_t wx = WsRingFloor16((int64_t)h + screen_x);
    int32_t wy = WsRingFloor16((int64_t)v + line);
    int cx = wx & 31, cy = wy & 31;
    if ((sc & 0x7FFF) != WsOwMapAddrBase(cx, cy)) return tile;

    WsOwSeen *s = &s_ow_seen[cy][cx];
    if (s->valid && s->wx == wx && s->wy == wy && s->tile == tile)
        return tile;

    if (WsOffwindowMode() == WS_OW_SOURCE && WsSourceValid()) {
        uint16_t w;
        if (WsSourceAgree(wx, wy, &w)) {
            WsSourceNoteServed();
            if (!s_ow_logged[cy][cx]) {
                s_ow_logged[cy][cx] = 1;
                WsOwLogVisit(cx, cy, wx, wy, tile, w, 0);
            }
            return w;
        }
    }

    if (WsOffwindowMode() == WS_OW_GHOST) {
        uint16_t g;
        if (WsGhostLookup(cx, cy, wx, wy, &g)) {
            if (!s_ow_logged[cy][cx]) {
                s_ow_logged[cy][cx] = 1;
                WsOwLogVisit(cx, cy, wx, wy, tile, g, 0);
            }
            return g;
        }
    }

    int masked = s_ow_mask_word != 0xFFFF;
    if (!s_ow_logged[cy][cx]) {
        s_ow_logged[cy][cx] = 1;
        WsOwLogVisit(cx, cy, wx, wy, tile, masked ? s_ow_mask_word : tile,
                     masked);
    }
    return masked ? s_ow_mask_word : tile;
}



int WsWrapDelta10(uint16_t now, uint16_t prev)
{
    int d = (int)((now - prev) & 0x3FF);
    return d >= 512 ? d - 1024 : d;
}

const WsOwSeen *WsRingSeenCell(int cy, int cx)
{
    return &s_ow_seen[cy][cx];
}

unsigned WsRingSceneGeneration(void) { return s_ow_scene_generation; }
int WsRingSceneValid(void) { return s_ow_scene_valid; }
uint16_t WsRingMapBase(void) { return s_ow_map_base; }
uint8_t WsRingPrevBgMode(void) { return s_ow_prev_bgmode; }
int WsRingScrollValid(void) { return s_ow_scroll_valid; }
int32_t WsRingScrollH(void) { return s_ow_unwrapped_h; }
int32_t WsRingScrollV(void) { return s_ow_unwrapped_v; }

int WsRingScrollSeed(int32_t *h, int32_t *v, uint16_t *ph, uint16_t *pv)
{
    if (!s_ow_scroll_valid) return 0;
    *h = s_ow_unwrapped_h;
    *v = s_ow_unwrapped_v;
    *ph = s_ow_prev_h;
    *pv = s_ow_prev_v;
    return 1;
}

void WsRingFrameEnd(int ow_mode)
{
    if (ow_mode) {
        if (ow_mode == WS_OW_SOURCE && s_ow_frame_active)
            WsSourceFrame();
        s_ow_frame_active = 0;
    } else {
        WsSourceInvalidate();
    }
}

void WsRingLogLive(int live)
{
    if (live == s_ow_live_prev) return;
    s_ow_live_prev = live;
    fprintf(stderr, "[ws-ow] %s at frame %d (b4=%u ee9=%u 300=%u)\n",
            live ? "ACTIVE" : "inactive", snes_frame_counter,
            g_ram[kRamRaceMode], g_ram[kRamPauseFinish], g_ram[kRamScreenMode]);
}

void WsRingWalkLine(Ppu *ppu, uint16_t map_base, int32_t h, int32_t v,
                    int line, WsRingLineSink sink, void *context)
{
    if (!ppu || !sink) return;
    int32_t wy = WsRingFloor16((int64_t)v + line);
    int cy = wy & 31;
    int x = 0;
    while (x < 256) {
        int64_t sx = (int64_t)h + x;
        int32_t wx = WsRingFloor16(sx);
        int cx = wx & 31;
        uint16_t tile = ppu->vram[(map_base + ((cy & 31) << 5) +
                                   (cx & 31)) & 0x7FFF];
        sink(context, cx, cy, wx, wy, tile);
        x += 16 - WsRingMod16(sx);
    }
}
