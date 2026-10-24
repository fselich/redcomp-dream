/* Widescreen BG1 margin policy. See ws_margin.h.
 *
 * With `source`, every margin sample of BG1 goes through WsMarginTile: a
 * ring slot still holding the world cell the native view showed is drawn as
 * is; otherwise the $7F track table supplies the word when the source anchor
 * agrees; otherwise the sample is masked with a fully transparent character.
 * The tile hook runs inside the PPU's BG renderer, so it never writes guest
 * VRAM, and the map-address check leaves the HUD bands (which repoint BG1's
 * map base) alone.
 */
#include "ws_margin.h"

#include "beam_frame_driver.h"
#include "common_rtl.h"
#include "variables.h"
#include "ws_debug.h"
#include "ws_ring.h"
#include "ws_settings.h"
#include "ws_source.h"

#include <stdio.h>
#include <string.h>

static int s_frame_policy;          /* policy hooked for the current draw */
static uint8_t s_logged[WS_RING_CELLS][WS_RING_CELLS];
static uint16_t s_mask_word = 0xFFFF;
static int s_announced;
static int s_live_prev = -1;

/* SNESRECOMP_WS_OFFWINDOW, else SNESRECOMP_WS_TRACK_CLAMP (clamp), else
 * [Widescreen] Margin, else source; the settings model maps the values of
 * removed policies. */
int WsMarginPolicy(void)
{
    const char *m = WsSettingValue(WS_SET_MARGIN);
    return WsCiEq(m, "clamp") ? WS_MARGIN_CLAMP : WS_MARGIN_SOURCE;
}

static int WsMarginVerbose(void)
{
    static int v = -1;
    if (v < 0) {
        const char *a = WsEnvStr("SNESRECOMP_WS_OFFWINDOW_LOG");
        const char *b = WsEnvStr("SNESRECOMP_WS_SOURCE_LOG");
        v = (a && *a) || (b && *b);
    }
    return v;
}

void WsMarginReset(void)
{
    WsRingReset();
}

static void WsMarginLogVisit(int cx, int cy, int32_t wx, int32_t wy,
                             uint16_t real, uint16_t served, int masked)
{
    FILE *f = WsLogOpen("SNESRECOMP_WS_OFFWINDOW_LOG",
                        "# frame,cell_x,cell_y,world_x,world_y,real,mask,masked");
    if (!f) return;
    fprintf(f, "%d,%d,%d,%d,%d,0x%04x,0x%04x,%d\n",
            snes_frame_counter, cx, cy, wx, wy, real, served, masked);
    fflush(f);
}

static void WsMarginLogLive(int live)
{
    if (live == s_live_prev) return;
    s_live_prev = live;
    fprintf(stderr, "[ws-ow] %s at frame %d (b4=%u ee9=%u 300=%u)\n",
            live ? "ACTIVE" : "inactive", snes_frame_counter,
            g_ram[kRamRaceMode], g_ram[kRamPauseFinish], g_ram[kRamScreenMode]);
}

/* Find a 16x16 character whose four 8x8 sub-characters are all zero words:
 * pixel 0 in every position, so a map word pointing at it is transparent for
 * any palette/priority. Returns the character index or -1. */
static int WsMarginFindBlankChar(Ppu *ppu)
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

/* Per-line host hook (beam_frame_driver), after the journal and HDMA for the
 * line and before it is drawn. */
static void WsMarginLine(Ppu *ppu, unsigned line, void *context)
{
    (void)context;
    if (!ppu) return;
    if ((int)line == WS_RING_LINE_FIRST) {
        uint16_t map_base = PPU_bgTilemapAdr(ppu, 0);
        uint16_t raw_h = ppu->hScroll[0], raw_v = ppu->vScroll[0];
        int reset = WsRingLine(ppu, (int)line);
        if (reset && WsMarginVerbose())
            fprintf(stderr, "[ws-ow] scene reset: base=%04x mode=%02x "
                            "scroll=%u,%u\n", map_base, ppu->bgmode,
                    raw_h, raw_v);
        memset(s_logged, 0, sizeof(s_logged));
        s_mask_word = 0xFFFF;
        int blank = WsMarginFindBlankChar(ppu);
        if (blank >= 0) s_mask_word = (uint16_t)(blank & 0x3FF);
        if (!s_announced && WsMarginVerbose()) {
            s_announced = 1;
            fprintf(stderr, "[ws-ow] active: mode=%d map_base=%04x "
                            "scroll=%u,%u extra=%d/%d\n",
                    WsMarginPolicy(), map_base, raw_h, raw_v,
                    ppu->extraLeftCur, ppu->extraRightCur);
        }
        return;
    }
    WsRingLine(ppu, (int)line);
}

/* Renderer hook: the tile word for a margin sample of BG1. */
static uint16_t WsMarginTile(Ppu *ppu, unsigned layer, int screen_x,
                             int line, uint16_t tile, uint16_t sc,
                             void *context)
{
    (void)context;
    if (!WsRingFrameActive() || layer != 0) return tile;
    if (screen_x >= 0 && screen_x < 256) return tile;

    int32_t h, v;
    WsRingLineScroll(ppu, &h, &v);
    int32_t wx = WsRingFloor16((int64_t)h + screen_x);
    int32_t wy = WsRingFloor16((int64_t)v + line);
    int cx = wx & 31, cy = wy & 31;
    if ((sc & 0x7FFF) != WsRingMapAddr(cx, cy)) return tile;

    const WsRingSeen *s = WsRingSeenCell(cy, cx);
    if (s->valid && s->wx == wx && s->wy == wy && s->tile == tile)
        return tile;

    if (WsSourceValid()) {
        uint16_t w;
        if (WsSourceAgree(wx, wy, &w)) {
            if (!s_logged[cy][cx]) {
                s_logged[cy][cx] = 1;
                WsMarginLogVisit(cx, cy, wx, wy, tile, w, 0);
            }
            return w;
        }
    }

    int masked = s_mask_word != 0xFFFF;
    if (!s_logged[cy][cx]) {
        s_logged[cy][cx] = 1;
        WsMarginLogVisit(cx, cy, wx, wy, tile, masked ? s_mask_word : tile,
                         masked);
    }
    return masked ? s_mask_word : tile;
}

void WsMarginFrameBegin(const WsFrameCtx *ctx)
{
    int policy = ctx->active ? WsMarginPolicy() : WS_MARGIN_NONE;
    s_frame_policy = policy == WS_MARGIN_SOURCE ? policy : WS_MARGIN_NONE;
    if (s_frame_policy == WS_MARGIN_NONE) WsMarginReset();
    if (WsMarginPolicy() == WS_MARGIN_SOURCE && ctx->wide && WsMarginVerbose())
        WsMarginLogLive(ctx->live);
    if (policy == WS_MARGIN_CLAMP)
        PpuSetWidescreenLayerClamp(ctx->ppu, 1u << 0);
    if (s_frame_policy == WS_MARGIN_SOURCE) {
        g_snes_beam_line_hook = &WsMarginLine;
        g_snes_beam_line_ctx = NULL;
        g_ppu_margin_tile_hook = &WsMarginTile;
        g_ppu_margin_tile_ctx = NULL;
    }
}

void WsMarginFrameEnd(void)
{
    if (s_frame_policy == WS_MARGIN_SOURCE) {
        g_snes_beam_line_hook = NULL;
        g_ppu_margin_tile_hook = NULL;
        if (WsRingFrameActive()) WsSourceFrame();
        WsRingFrameEnd();
    } else {
        WsSourceInvalidate();
    }
}
