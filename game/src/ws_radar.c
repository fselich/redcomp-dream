/* Local radar panel.
 *
 * Draws a 64x32 RGB15 panel in the bottom-right margin from the $7F track
 * table and the live rider/camera positions. Presentation-only: no guest
 * writes. See openspec/changes/archive/2026-10-22-uniracers-ws-radar.
 */
#include "ws_radar.h"

#include "common_rtl.h"
#include "variables.h"
#include "ws_debug.h"
#include "ws_frame.h"
#include "ws_ring.h"
#include "ws_settings.h"
#include "ws_source.h"

#include <stdio.h>
#include <string.h>

extern int snes_netplay_active(void);

/* ── Local radar ─────────────────────────────────────────────────────────────
 *
 * A 64x32 RGB15 panel in the bottom-right margin. Each panel pixel covers a
 * 2x2 block of the 128x64-cell window around the player, read from the decoded
 * $7F track table through the same source anchor as the margins. The player
 * and rival dots come from the live WRAM rider/camera positions. The panel is
 * a PpuExtraObject: presentation-only, no guest memory writes. */
#define WS_RADAR_PANEL_W 64
#define WS_RADAR_PANEL_H 32
#define WS_RADAR_PANEL_WINDOW_X 128
#define WS_RADAR_PANEL_WINDOW_Y 64
#define WS_RADAR_OPAQUE 0x8000u
#define WS_RADAR_BG (WS_RADAR_OPAQUE | 0x0842u)
#define WS_RADAR_EDGE (WS_RADAR_OPAQUE | 0x5294u)
#define WS_RADAR_TRACK (WS_RADAR_OPAQUE | 0x03E0u)
#define WS_RADAR_PLAYER (WS_RADAR_OPAQUE | 0x7FFFu)
#define WS_RADAR_RIVAL (WS_RADAR_OPAQUE | 0x001Fu)

static uint16_t s_radar_px[WS_RADAR_PANEL_W * WS_RADAR_PANEL_H];
static PpuExtraObject s_radar_obj;

static int WsRadarHalf(int v)
{
    return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

static int WsRadarRiderCell(uint16_t xaddr, uint16_t yaddr,
                            int32_t *tx, int32_t *ty)
{
    if (!WsRingScrollValid() || !WsSourceValid()) return 0;
    int16_t rx = (int16_t)ram_read16(xaddr);
    int16_t ry = (int16_t)ram_read16(yaddr);
    int16_t cx = (int16_t)ram_read16(kRamCamX);
    int16_t cy = (int16_t)ram_read16(kRamCamY);
    int16_t sx = (int16_t)(rx - cx);
    int16_t sy = (int16_t)(ry - cy);
    int32_t wx = WsRingFloor16((int64_t)WsRingScrollH() + sx);
    int32_t wy = WsRingFloor16((int64_t)WsRingScrollV() + sy);
    *tx = wx + WsSourceAnchorX(0);
    *ty = wy + WsSourceAnchorY(0);
    return 1;
}

void WsRadarUpdate(Ppu *ppu, const WsFrameCtx *ctx)
{
    if (!ppu) return;

    int eligible = WsSettingBool(WS_SET_MINIMAP) && ctx && ctx->active &&
                   !ctx->two_player && ctx->extra > 0 &&
                   WsSourceValid() && !snes_netplay_active();
    if (!eligible) {
        PpuSetExtraObjects(ppu, NULL, 0);
        return;
    }

    int32_t ptx, pty, rtx, rty;
    if (!WsRadarRiderCell(kRamRider1X, kRamRider1Y, &ptx, &pty) ||
        !WsRadarRiderCell(kRamRider2X, kRamRider2Y, &rtx, &rty)) {
        PpuSetExtraObjects(ppu, NULL, 0);
        return;
    }

    int half_x = WS_RADAR_PANEL_WINDOW_X / 2;
    int half_y = WS_RADAR_PANEL_WINDOW_Y / 2;
    for (int py = 0; py < WS_RADAR_PANEL_H; py++) {
        for (int px = 0; px < WS_RADAR_PANEL_W; px++) {
            uint16_t c = WS_RADAR_BG;
            if (px == 0 || px == WS_RADAR_PANEL_W - 1 ||
                py == 0 || py == WS_RADAR_PANEL_H - 1) {
                c = WS_RADAR_EDGE;
            } else {
                int tx = ptx - half_x + px * 2;
                int ty = pty - half_y + py * 2;
                int track = 0;
                for (int dy = 0; dy < 2 && !track; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        uint16_t w;
                        if (WsSourceCell(tx + dx, ty + dy, &w) &&
                            (w & 0x03FF)) {
                            track = 1;
                            break;
                        }
                    }
                }
                c = track ? WS_RADAR_TRACK : WS_RADAR_BG;
            }
            s_radar_px[py * WS_RADAR_PANEL_W + px] = c;
        }
    }

    int rx = WS_RADAR_PANEL_W / 2 + WsRadarHalf(rtx - ptx);
    int ry = WS_RADAR_PANEL_H / 2 + WsRadarHalf(rty - pty);
    if (rx < 1) rx = 1;
    if (rx > WS_RADAR_PANEL_W - 2) rx = WS_RADAR_PANEL_W - 2;
    if (ry < 1) ry = 1;
    if (ry > WS_RADAR_PANEL_H - 2) ry = WS_RADAR_PANEL_H - 2;
    s_radar_px[ry * WS_RADAR_PANEL_W + rx] = WS_RADAR_RIVAL;
    s_radar_px[(WS_RADAR_PANEL_H / 2) * WS_RADAR_PANEL_W + WS_RADAR_PANEL_W / 2] =
        WS_RADAR_PLAYER;

    FILE *radar_log = WsLogOpen("SNESRECOMP_WS_RADAR_LOG",
                                "# frame,ptx,pty,rtx,rty,ax,ay,pword,rword");
    if (radar_log) {
        uint16_t pword = 0, rword = 0;
        WsSourceCell(ptx, pty, &pword);
        WsSourceCell(rtx, rty, &rword);
        fprintf(radar_log, "%d,%d,%d,%d,%d,%d,%d,%04x,%04x\n",
                snes_frame_counter, ptx, pty, rtx, rty,
                WsSourceAnchorX(0), WsSourceAnchorY(0), pword, rword);
        fflush(radar_log);
    }

    s_radar_obj.x = 256 + (int)ppu->extraRightCur - WS_RADAR_PANEL_W - 2;
    s_radar_obj.y = 224 - WS_RADAR_PANEL_H - 2;
    s_radar_obj.width = WS_RADAR_PANEL_W;
    s_radar_obj.height = WS_RADAR_PANEL_H;
    s_radar_obj.stride = WS_RADAR_PANEL_W;
    s_radar_obj.priority = 3;
    s_radar_obj.math = 1;
    s_radar_obj.oamSlot = 127;
    s_radar_obj.order = 1;
    s_radar_obj.pixels = s_radar_px;
    PpuSetExtraObjects(ppu, &s_radar_obj, 1);
}

