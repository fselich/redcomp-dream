/*
 * Uniracers — port glue.
 *
 * This file owns the frame boundary (GameRunOneFrame), the draw orchestration
 * (GameDrawPpuFrame), the host hooks (GameAfterConfig, GameAfterRunFrame,
 * GameSessionReset) and kGameInfo. The widescreen features live in the
 * modules below:
 *
 *   ws_frame.[ch]       predicates, margin, per-frame context, env cache
 *   ws_cull.[ch]        rider cull widening (the only guest WRAM writer)
 *   ws_hud.[ch]         BG3 elastic band and clamp bands
 *   ws_oam_fix.[ch]     rider X-high/parked OAM fixes (PPU OAM copy only)
 *   ws_ring.[ch]        BG1 ring observation, margin policy and tile hook
 *   ws_source.[ch]      $7F track-table reader and source anchor
 *   ws_radar.[ch]       64x32 local radar panel
 *   ws_ghost_lane.[ch]  speculative BG1 lane for the `ghost` policy
 *   ws_debug.[ch]       lazy CSV/text log files
 *
 * Guest-state rule: ws_cull.c is the only production writer of WRAM. Every
 * other module is presentation-only. See docs/UNIRACERS_NATIVE_MAP_HANDOFF.md
 * and openspec/changes/uniracers-code-cleanup.
 */

#include "game_rtl.h"

#include "beam_frame_driver.h"
#include "snes/ppu.h"
#include "ws_cull.h"
#include "ws_frame.h"
#include "ws_ghost_lane.h"
#include "ws_hud.h"
#include "ws_oam_fix.h"
#include "ws_radar.h"
#include "ws_ring.h"

extern Ppu *g_ppu;
extern int snes_netplay_active(void);

void GameRunOneFrame(void)
{
    WsApplyCull();
    snes_beam_frame_driver_run_frame();
    /* Catch a base written during this frame before the next cull pass. */
    WsApplyCull();
}

void GameDrawPpuFrame(void)
{
    WsFrameCtx ctx;
    WsFrameCtxBuild(&ctx, g_ppu);
    if (ctx.ppu) {
        WsOamFix(ctx.ppu);
        WsRadarUpdate(ctx.ppu, &ctx);
    }
    int ow_policy = ctx.active ? WsOffwindowMode() : WS_OW_WRAP;
    int ow_mode = WsOffwindowHooked(ow_policy) ? ow_policy : 0;
    if (!ow_mode) WsOwReset();
    if (WsOffwindowHooked(WsOffwindowMode()) && ctx.wide && WsOwVerbose())
        WsRingLogLive(ctx.live);
    if (ctx.active && ow_policy == WS_OW_CLAMP) {
        /* SNESRECOMP_WS_OFFWINDOW=clamp (or the legacy TRACK_CLAMP): BG1
         * limited to the native 256 px, margins show the background. */
        PpuSetWidescreenLayerClamp(ctx.ppu, 1u << 0);
    }
    WsApplyHud(ctx.ppu, &ctx);
    if (ow_mode) {
        g_snes_beam_line_hook = &WsOffwindowLine;
        g_snes_beam_line_ctx = NULL;
        g_ppu_margin_tile_hook = &WsOffwindowTile;
        g_ppu_margin_tile_ctx = NULL;
    }
    snes_beam_frame_driver_draw_ppu_frame();
    if (ow_mode) {
        g_snes_beam_line_hook = NULL;
        g_ppu_margin_tile_hook = NULL;
    }
    WsRingFrameEnd(ow_mode);
}

void GameAfterRunFrame(const struct SnesDesktopHostFrameStats *stats)
{
    WsGhostAfterRunFrame(stats);
}

void GameAfterConfig(void)
{
    WsRadarAfterConfig();
}

void GameSessionReset(void)
{
    /* Rematch / soft-return: clear anything that must not survive a new
     * session. recomp-ai-rules/NETPLAY.md §3 — sticky state that "has always
     * been fine" is the usual desync culprit, because single-player never
     * re-enters the boot path twice in one process. */
    snes_beam_frame_driver_reset();
}

const RtlGameInfo kGameInfo = {
    .title = "uniracers",
    .initialize = NULL,
    .run_frame = &GameRunOneFrame,
    .draw_ppu_frame = &GameDrawPpuFrame,
    .save_name_prefix = "save",
    .session_reset = &GameSessionReset,
};
