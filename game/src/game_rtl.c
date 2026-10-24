/*
 * Uniracers — port glue.
 *
 * This file owns the frame boundary (GameRunOneFrame), the draw orchestration
 * (GameDrawPpuFrame), the host hooks (GameAfterConfig, GameAfterRunFrame,
 * GameSessionReset) and kGameInfo. The widescreen features live in the
 * modules below:
 *
 *   ws_frame.[ch]       predicates, margin, per-frame context, env cache
 *   ws_settings.[ch]    one model for every user-facing option
 *   ws_menu.[ch]        in-game options menu (F10 / Select+L)
 *   ws_cull.[ch]        rider cull widening (the only guest WRAM writer)
 *   ws_hud.[ch]         BG3 elastic band and clamp bands
 *   ws_oam_fix.[ch]     rider X-high/parked OAM fixes (PPU OAM copy only)
 *   ws_margin.[ch]      BG1 margin policy (source/clamp) and its hooks
 *   ws_ring.[ch]        BG1 ring observation (seen table, scroll unwrap)
 *   ws_source.[ch]      $7F track-table reader and source anchor
 *   ws_radar.[ch]       64x32 local radar panel
 *   ws_debug.[ch]       lazy CSV/text log files
 *   uniracers_save.[ch] SRAM checksums and the opt-in UnlockAllTours
 *
 * Guest-state rule: ws_cull.c is the only production writer of WRAM, and
 * uniracers_save.c the only writer of SRAM (one byte, opt-in). Every other
 * module is presentation-only. See
 * openspec/changes/archive/2026-10-23-uniracers-code-cleanup.
 */

#include "game_rtl.h"

#include "beam_frame_driver.h"
#include "snes/ppu.h"
#include "uniracers_save.h"
#include "ws_cull.h"
#include "ws_frame.h"
#include "ws_hud.h"
#include "ws_margin.h"
#include "ws_oam_fix.h"
#include "ws_radar.h"
#include "ws_settings.h"

extern Ppu *g_ppu;

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
    WsMarginFrameBegin(&ctx);
    WsApplyHud(ctx.ppu, &ctx);
    snes_beam_frame_driver_draw_ppu_frame();
    WsMarginFrameEnd();
}

void GameAfterRunFrame(const struct SnesDesktopHostFrameStats *stats)
{
    (void)stats;
    SaveUnlockTick();
}

void GameAfterConfig(void)
{
    WsSettingsLoad();
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
