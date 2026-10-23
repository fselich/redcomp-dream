/* Speculative ghost lane (SNESRECOMP_WS_OFFWINDOW=ghost).
 *
 * Runs a copy of the machine ahead of the real one, harvests BG1 ring cells
 * into a world-keyed table and serves them in the real margin. Presentation-
 * only: the real machine is snapshotted and restored around every step.
 */
#include "ws_ghost_lane.h"

#include "beam_frame_driver.h"
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes_runahead.h"
#include "variables.h"
#include "widescreen.h"
#include "ws_cull.h"
#include "ws_debug.h"
#include "ws_frame.h"
#include "ws_ring.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern Ppu *g_ppu;
extern int snes_netplay_active(void);

/* ── Ghost lane (SNESRECOMP_WS_OFFWINDOW=ghost) ───────────────────────────
 *
 * A speculative machine runs ahead of the real one. Its BG1 ring cells are
 * harvested into s_gh with the same world-key + tile-word rule as Option A,
 * and served only when the real margin needs exactly that world cell. The
 * level map is static per world cell, so a cell the game writes in a
 * speculative future is still correct for that cell.
 *
 * The lane does not touch the real machine: save the rollback blob and the
 * host render state, run the ghost, save its blob, load the real blob and
 * restore the host state before the real field is drawn. */
#define WS_GHOST_SENTINEL (INT_MIN / 2)

typedef struct {
    int32_t wx, wy;
    uint16_t tile;
    uint8_t valid;
} WsGhostCell;

static WsGhostCell s_gh[WS_OW_CELLS][WS_OW_CELLS];
static int s_gh_valid;

static int s_gh_frame_active;
static int s_gh_scene_ok;
static int32_t s_gh_unwrapped_h, s_gh_unwrapped_v;
static uint16_t s_gh_prev_h, s_gh_prev_v;
static int s_gh_scroll_valid;
static int32_t s_gh_base_h, s_gh_base_v;
static uint16_t s_gh_frame_raw_h, s_gh_frame_raw_v;
static uint16_t s_gh_map_base;

static uint8_t *s_gh_blob, *s_re_blob;
static size_t s_gh_cap, s_re_cap, s_gh_len, s_re_len;
static int s_ghost_valid;
static int s_ghost_lead;
static int s_ghost_input_valid;
static uint32_t s_ghost_input;
static unsigned s_ghost_scene_gen;
static int s_ghost_frames_since_resync;
static uint32_t s_ghost_pc;
static bool s_ghost_wai;
static WsCullState s_ghost_cull;
static void *s_raster_save;
static size_t s_raster_save_size;
static int s_ghost_in_hook;
static int s_ghost_announced;
static int s_ghost_warned;
static uint32_t s_gh_chr_hash;
static int s_gh_chr_has;
static int s_gh_chr_stable;
static int s_gh_chr_ready;
#define WS_GHOST_CHR_WARMUP 30
static Ppu s_ghost_ppu_save;

static int WsGhostAhead(void)
{
    return WsEnvInt("SNESRECOMP_WS_GHOST_AHEAD", 4, 0, 8);
}

static int WsGhostMaxSteps(void)
{
    return WsEnvInt("SNESRECOMP_WS_GHOST_MAXSTEPS", 2, 1, 8);
}

static int WsGhostResync(void)
{
    return WsEnvInt("SNESRECOMP_WS_GHOST_RESYNC", 600, 1, 3600);
}

static int WsGhostEligible(void)
{
    if (WsOffwindowMode() != WS_OW_GHOST) return 0;
    if (!WsWideEnabled() || !WsInLiveRace()) return 0;
    if (snes_runahead_frames() > 0) return 0;
    if (snes_netplay_active()) return 0;
    return 1;
}

static int WsGhostEnsureBuffers(void)
{
    size_t bound = RtlRollbackSnapshotBound();
    if (!bound) return 0;

    if (!s_gh_blob || s_gh_cap < bound) {
        uint8_t *p = (uint8_t *)realloc(s_gh_blob, bound);
        if (!p) return 0;
        s_gh_blob = p;
        s_gh_cap = bound;
        s_ghost_valid = 0;
    }
    if (!s_re_blob || s_re_cap < bound) {
        uint8_t *p = (uint8_t *)realloc(s_re_blob, bound);
        if (!p) return 0;
        s_re_blob = p;
        s_re_cap = bound;
    }
    if (!s_raster_save) {
        size_t n = ppu_rasterStateSize();
        if (!n) return 0;
        s_raster_save = malloc(n);
        if (!s_raster_save) return 0;
        s_raster_save_size = n;
    }
    return 1;
}

static void WsGhostSeedScroll(void)
{
    s_gh_scroll_valid = WsRingScrollSeed(
        &s_gh_unwrapped_h, &s_gh_unwrapped_v, &s_gh_prev_h, &s_gh_prev_v);
}

/* BG1 character data is streamed at race/scene entry. A ghost map word is
 * only safe to draw while the real lane's character region has been stable
 * for a short window; otherwise it could point at a character the real VRAM
 * has not received yet. The hash is cheap and also feeds the optional trace. */
static void WsGhostChrCheck(void)
{
    if (!g_ppu) return;
    FILE *f = WsLogOpen("SNESRECOMP_WS_GHOST_CHR_LOG",
                        "# frame,hash,nonzero");
    int base = PPU_bgTileAdr(g_ppu, 0);
    uint32_t h = 2166136261u;
    int nonzero = 0;
    for (int i = 0; i < 0x4000; i++) {
        uint16_t w = g_ppu->vram[(base + i) & 0x7FFF];
        if (w) nonzero++;
        h ^= w & 0xFF;
        h *= 16777619u;
        h ^= w >> 8;
        h *= 16777619u;
    }
    if (!s_gh_chr_has || h != s_gh_chr_hash) {
        s_gh_chr_hash = h;
        s_gh_chr_has = 1;
        s_gh_chr_stable = 0;
        s_gh_chr_ready = 0;
        if (f) {
            fprintf(f, "%d,0x%08x,%d\n", snes_frame_counter, h, nonzero);
            fflush(f);
        }
    } else if (s_gh_chr_stable < WS_GHOST_CHR_WARMUP) {
        s_gh_chr_stable++;
        if (s_gh_chr_stable >= WS_GHOST_CHR_WARMUP) s_gh_chr_ready = 1;
    }
}

static void WsGhostSeenSink(void *context, int cx, int cy, int32_t wx,
                            int32_t wy, uint16_t tile)
{
    (void)context;
    WsGhostCell *g = &s_gh[cy][cx];
    g->wx = wx;
    g->wy = wy;
    g->tile = tile;
    g->valid = 1;
}

static void WsGhostLine(Ppu *ppu, unsigned line, void *context)
{
    (void)context;
    if (!ppu || !s_gh_frame_active || !s_gh_scene_ok) return;

    if ((int)line == WS_OW_LINE_FIRST) {
        uint16_t base = PPU_bgTilemapAdr(ppu, 0);
        uint8_t mode = ppu->bgmode;
        /* The real lane's scene is the last one it drew. A ghost frame in a
         * transition is not useful; drop the harvest until the real draw
         * catches up. */
        if (!WsRingSceneValid() || base != WsRingMapBase() ||
            mode != WsRingPrevBgMode() || !s_gh_scroll_valid) {
            s_gh_scene_ok = 0;
            s_gh_valid = 0;
            return;
        }
        s_gh_map_base = base;
        int dh = WsWrapDelta10(ppu->hScroll[0], s_gh_prev_h);
        int dv = WsWrapDelta10(ppu->vScroll[0], s_gh_prev_v);
        s_gh_unwrapped_h += dh;
        s_gh_unwrapped_v += dv;
        s_gh_prev_h = ppu->hScroll[0];
        s_gh_prev_v = ppu->vScroll[0];
        s_gh_base_h = s_gh_unwrapped_h;
        s_gh_base_v = s_gh_unwrapped_v;
        s_gh_frame_raw_h = ppu->hScroll[0];
        s_gh_frame_raw_v = ppu->vScroll[0];
    }
    if (!s_gh_scene_ok) return;
    /* HUD bands repoint BG1's map base. Never harvest those lines. */
    if (PPU_bgTilemapAdr(ppu, 0) != s_gh_map_base) return;

    int dh = WsWrapDelta10(ppu->hScroll[0], s_gh_frame_raw_h);
    int dv = WsWrapDelta10(ppu->vScroll[0], s_gh_frame_raw_v);
    int32_t h = s_gh_base_h + dh, v = s_gh_base_v + dv;
    WsRingWalkLine(ppu, s_gh_map_base, h, v, (int)line, &WsGhostSeenSink, NULL);
}

static void WsGhostDraw(void)
{
    SnesBeamLineHook *old_line = g_snes_beam_line_hook;
    void *old_line_ctx = g_snes_beam_line_ctx;
    PpuMarginTileHook *old_margin = g_ppu_margin_tile_hook;
    void *old_margin_ctx = g_ppu_margin_tile_ctx;
    int old_counter = snes_frame_counter;

    snes_frame_counter = WS_GHOST_SENTINEL;
    g_snes_beam_line_hook = &WsGhostLine;
    g_snes_beam_line_ctx = NULL;
    g_ppu_margin_tile_hook = NULL;
    g_ppu_margin_tile_ctx = NULL;
    s_gh_frame_active = 1;
    s_gh_scene_ok = 1;
    snes_beam_frame_driver_draw_ppu_frame();
    s_gh_frame_active = 0;

    g_snes_beam_line_hook = old_line;
    g_snes_beam_line_ctx = old_line_ctx;
    g_ppu_margin_tile_hook = old_margin;
    g_ppu_margin_tile_ctx = old_margin_ctx;
    snes_frame_counter = old_counter;
    s_gh_valid = s_gh_scene_ok && s_gh_chr_ready;
}

void WsGhostAfterRunFrame(const struct SnesDesktopHostFrameStats *stats)
{
    (void)stats;
    if (s_ghost_in_hook) return;

    if (!WsGhostEligible()) {
        s_ghost_valid = 0;
        s_gh_valid = 0;
        s_ghost_input_valid = 0;
        return;
    }
    WsGhostChrCheck();
    if (!WsGhostEnsureBuffers()) return;

    s_ghost_in_hook = 1;

    uint32_t real_pc;
    bool real_wai;
    snes_beam_frame_driver_get_exec(&real_pc, &real_wai);
    WsCullState real_cull;
    WsCullGet(&real_cull);
    uint32_t audio_cursor = RtlAudioProducerCursor();
    bool saved_spec = RtlSpeculativeFrame();
    bool saved_render = g_snes->disableRender;
    SnesBeamLineHook *saved_line = g_snes_beam_line_hook;
    void *saved_line_ctx = g_snes_beam_line_ctx;
    PpuMarginTileHook *saved_margin = g_ppu_margin_tile_hook;
    void *saved_margin_ctx = g_ppu_margin_tile_ctx;

    ppu_rasterStateSave(s_raster_save, s_raster_save_size);
    if (g_ppu) memcpy(&s_ghost_ppu_save, g_ppu, sizeof(Ppu));
    s_re_len = RtlRollbackSaveToMemory(s_re_blob, s_re_cap);
    if (!s_re_len) {
        if (!s_ghost_warned) {
            fprintf(stderr, "[ws-ghost] real snapshot failed; lane idle\n");
            s_ghost_warned = 1;
        }
        s_ghost_in_hook = 0;
        return;
    }

    uint32_t inputs = (uint32_t)g_snes->input1_currentState |
                      ((uint32_t)g_snes->input2_currentState << 12);
    int resync = 0;
    if (!s_ghost_valid || !s_ghost_input_valid || inputs != s_ghost_input ||
        WsRingSceneGeneration() != s_ghost_scene_gen ||
        s_ghost_frames_since_resync >= WsGhostResync())
        resync = 1;

    if (!resync) {
        if (!RtlRollbackLoadFromMemory(s_gh_blob, s_gh_len)) {
            /* The machine may be partially changed; put the real state back
             * before doing anything else. */
            resync = 1;
            if (!RtlRollbackLoadFromMemory(s_re_blob, s_re_len)) {
                if (!s_ghost_warned) {
                    fprintf(stderr, "[ws-ghost] real rollback failed; lane "
                                    "disabled\n");
                    s_ghost_warned = 1;
                }
                s_ghost_valid = 0;
                s_gh_valid = 0;
                s_ghost_in_hook = 0;
                return;
            }
            snes_beam_frame_driver_set_exec(real_pc, real_wai);
            WsCullSet(&real_cull);
        } else {
            snes_beam_frame_driver_set_exec(s_ghost_pc, s_ghost_wai);
            WsCullSet(&s_ghost_cull);
            s_ghost_lead -= 1;
        }
    }

    if (resync) {
        s_ghost_valid = 0;
        s_ghost_lead = 0;
        s_ghost_frames_since_resync = 0;
        s_ghost_scene_gen = WsRingSceneGeneration();
        memset(s_gh, 0, sizeof(s_gh));
        s_gh_valid = 0;
        s_ghost_pc = real_pc;
        s_ghost_wai = real_wai;
        s_ghost_cull = real_cull;
        WsGhostSeedScroll();
    }
    s_ghost_input = inputs;
    s_ghost_input_valid = 1;

    int ahead = WsGhostAhead();
    int maxsteps = WsGhostMaxSteps();
    int steps = 0;
    if (ahead > 0) {
        steps = s_ghost_lead < ahead ? ahead - s_ghost_lead : 1;
        if (steps > maxsteps) steps = maxsteps;
        if (steps < 0) steps = 0;
    } else {
        s_gh_valid = 0;
        s_ghost_valid = 0;
    }

    if (steps > 0) {
        RtlSetSpeculativeFrame(true);
        for (int i = 0; i < steps; i++) {
            snes_frame_counter = WS_GHOST_SENTINEL;
            RtlRunFrame(inputs);
            WsGhostDraw();
            s_ghost_lead++;
        }
        RtlSetSpeculativeFrame(false);

        s_gh_len = RtlRollbackSaveToMemory(s_gh_blob, s_gh_cap);
        s_ghost_valid = s_gh_len != 0;
        if (s_ghost_valid) {
            snes_beam_frame_driver_get_exec(&s_ghost_pc, &s_ghost_wai);
            WsCullGet(&s_ghost_cull);
        }
    }
    s_ghost_frames_since_resync++;

    RtlAudioRewindProducer(audio_cursor);
    if (!RtlRollbackLoadFromMemory(s_re_blob, s_re_len)) {
        if (!s_ghost_warned) {
            fprintf(stderr, "[ws-ghost] real rollback load FAILED; state may "
                            "have advanced\n");
            s_ghost_warned = 1;
        }
    }
    snes_beam_frame_driver_set_exec(real_pc, real_wai);
    WsCullSet(&real_cull);
    if (g_ppu) memcpy(g_ppu, &s_ghost_ppu_save, sizeof(Ppu));
    ppu_rasterStateLoad(s_raster_save, s_raster_save_size);
    RtlSetSpeculativeFrame(saved_spec);
    g_snes->disableRender = saved_render;
    g_snes_beam_line_hook = saved_line;
    g_snes_beam_line_ctx = saved_line_ctx;
    g_ppu_margin_tile_hook = saved_margin;
    g_ppu_margin_tile_ctx = saved_margin_ctx;

    if (!s_ghost_announced) {
        s_ghost_announced = 1;
        fprintf(stderr, "[ws-ghost] active: ahead=%d maxsteps=%d resync=%d "
                        "snapshot=%zu bytes\n", ahead, maxsteps,
                WsGhostResync(), s_re_len);
    }
    s_ghost_in_hook = 0;
}


int WsGhostLookup(int cx, int cy, int32_t wx, int32_t wy, uint16_t *tile)
{
    if (!s_gh_valid) return 0;
    const WsGhostCell *g = &s_gh[cy][cx];
    if (!g->valid || g->wx != wx || g->wy != wy) return 0;
    *tile = g->tile;
    return 1;
}
