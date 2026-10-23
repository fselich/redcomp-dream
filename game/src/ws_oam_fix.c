/* Rider OAM presentation fixes.
 *
 * Owns the X-high-bit fix for riders in the widened right cull, the parked
 * sentinel and the parked 1P 2P pair, plus the trace-only TEST_OAM injector.
 * Writes only the PPU's uploaded OAM copy and high table, never guest memory.
 */
#include "ws_oam_fix.h"

#include "common_rtl.h"

#include "variables.h"
#include "ws_cull.h"
#include "ws_debug.h"
#include "ws_frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Debug-only P8 probe: override one OAM slot for the presentation pass.
 * Format: slot:x:y:tile:attr:big. It runs after the game logic and before
 * the rasterizer, so it cannot change simulation; unset in normal runs.
 * Compiled only into trace builds; release builds get the stub below. */
#if SNESRECOMP_TRACE
static void WsInjectTestOam(Ppu *ppu)
{
    const char *spec = WsEnvStr("SNESRECOMP_WS_TEST_OAM");
    if (!spec || !*spec || !ppu) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", spec);
    uint8_t hints[16] = {0};
    int any_hint = 0;
    for (char *p = strtok(buf, ";"); p; p = strtok(NULL, ";")) {
        int slot, x, y, tile, attr, big;
        if (sscanf(p, "%d:%d:%d:%d:%d:%d",
                   &slot, &x, &y, &tile, &attr, &big) != 6)
            continue;
        if (slot < 0 || slot > 127) continue;
        ppu->oam[slot * 2] = (uint16_t)(((y & 0xFF) << 8) | (x & 0xFF));
        ppu->oam[slot * 2 + 1] = (uint16_t)(((attr & 0xFF) << 8) | (tile & 0xFF));
        uint8_t *hb = &ppu->highOam[slot >> 2];
        int sh = (slot & 3) * 2;
        *hb = (uint8_t)((*hb & ~(3u << sh)) |
                      ((((x >> 8) & 1u) << sh)) |
                      (((big & 1u) << (sh + 1))));
        hints[slot >> 3] |= (uint8_t)(1u << (slot & 7));
        any_hint = 1;
    }
    const char *hint = WsEnvStr("SNESRECOMP_WS_TEST_OAM_LEFT_HINT");
    if (any_hint && hint && *hint && strcmp(hint, "0") != 0)
        PpuWsSetOamLeftHints(ppu, hints);
}
#else
static void WsInjectTestOam(Ppu *ppu) { (void)ppu; }
#endif

/* The rider staging code sets the OAM X high bit only for negative X. The
 * native right cull is 256, so a positive X can never reach the high bit; our
 * widened cull admits [256, 256 + E) and those uploads would decode as X-256,
 * drawing a phantom on the left. Resolve the ambiguity from the rider's true
 * WRAM X and set the missing bit in the PPU's uploaded high table only. Never
 * write guest memory. */
#define WS_OAM_XHIGH_SLACK 32

static int WsOamXHighEnabled(void)
{
    return WsEnvFlag("SNESRECOMP_WS_OAM_XHIGH", 1);
}

static void WsOamXHighLog(int slot, int lo, int want)
{
    FILE *f = WsLogOpen("SNESRECOMP_WS_OAM_XHIGH_LOG", "frame,slot,lo,want");
    if (!f) return;
    fprintf(f, "%d,%d,%d,%d\n", snes_frame_counter, slot, lo, want);
    fflush(f);
}

/* Diagnostic for the parked-rider sentinel. Scans every OAM slot, not just
 * the two rider slots, because the game also parks slots 96/97. The rel/rel_y
 * columns are only meaningful for slots 98/99. */
static void WsParkLog(int slot, int lo, int y, int hi, int rel, int rel_y,
                      int e, const char *action)
{
    FILE *f = WsLogOpen("SNESRECOMP_WS_PARK_LOG",
                        "frame,slot,lo,y,hi,rel,rel_y,E,action");
    if (!f) return;
    fprintf(f, "%d,%d,%d,%d,%d,%d,%d,%d,%s\n", snes_frame_counter, slot, lo, y,
            hi, rel, rel_y, e, action);
    fflush(f);
}

typedef struct {
    int lo, y, hi, bit;
} WsOamSlot;

static WsOamSlot WsOamSlotRead(const Ppu *ppu, int slot)
{
    WsOamSlot o;
    o.lo = ppu->oam[slot * 2] & 0xFF;
    o.y = ppu->oam[slot * 2] >> 8;
    o.bit = (slot & 3) * 2;
    o.hi = (ppu->highOam[slot >> 2] >> o.bit) & 1;
    return o;
}

static void WsOamSlotHide(Ppu *ppu, int slot, const WsOamSlot *o)
{
    ppu->oam[slot * 2] = (uint16_t)((0xE0 << 8) | o->lo);
    ppu->highOam[slot >> 2] &= (uint8_t)~(2u << o->bit);
}

static void WsOamSlotSetHigh(Ppu *ppu, int slot, const WsOamSlot *o)
{
    ppu->highOam[slot >> 2] |= (uint8_t)(1u << o->bit);
}

static void WsParkCandidates(Ppu *ppu, int e, int cam, int cam_y)
{
    for (int slot = 0; slot < 128; slot++) {
        const WsOamSlot o = WsOamSlotRead(ppu, slot);
        const int lo = o.lo, y = o.y, hi = o.hi;
        /* A high-bit X below E is the same 9-bit value as a negative X in
         * [-256, -128): 4:3 wraps it off-screen, but 256 + lo < 256 + E makes
         * the widened renderer place it in the right margin. Log every such
         * slot so the parked 2P pair (96/97) is visible in the trace. */
        if (hi && lo < e) WsParkLog(slot, lo, y, hi, -32768, -32768, e, "right");
        if (!(hi && lo == 0x70 && y == 0x70)) continue;
        int rel = -32768, rel_y = -32768;
        if (slot == 98) {
            rel = (int16_t)(ram_read16(kRamRider1X) - (uint16_t)cam);
            rel_y = (int16_t)(ram_read16(kRamRider1Y) - (uint16_t)cam_y);
        } else if (slot == 99) {
            rel = (int16_t)(ram_read16(kRamRider2X) - (uint16_t)cam);
            rel_y = (int16_t)(ram_read16(kRamRider2Y) - (uint16_t)cam_y);
        }
        WsParkLog(slot, lo, y, hi, rel, rel_y, e, "candidate");
    }
}

static void WsFixRiderOamXHigh(Ppu *ppu)
{
    if (!ppu) return;
    if (!WsWideEnabled() || !WsInLiveRace()) return;
    if (g_ram[kRamTwoPlayer] != 0) return;            /* 2P split screen */
    if (!WsCullIsEnabled() || WsExtra() <= 0) return;
    if (!WsOamXHighEnabled()) return;

    const int e = WsExtra();
    const int cam = (int16_t)ram_read16(kRamCamX);
    const int cam_y = (int16_t)ram_read16(kRamCamY);
    static const uint16_t wx_addr[2] = { kRamRider1X, kRamRider2X }; /* player, rival */
    static const uint16_t wy_addr[2] = { kRamRider1Y, kRamRider2Y };
    WsParkCandidates(ppu, e, cam, cam_y);
    /* Slots 96/97 are the parked 2P pair in a 1P race. Their X high bit is
     * set (the stored X is negative), so 4:3 wraps them off-screen left; at
     * E > ~96/101 the raw 9-bit value instead falls inside the widened right
     * window and shows as a sliver of the unused rider. They have no
     * legitimate 1P position, so hide them in the PPU copy. */
    for (int slot = 96; slot <= 97; slot++) {
        const WsOamSlot o = WsOamSlotRead(ppu, slot);
        if (!o.hi) continue;
        WsOamSlotHide(ppu, slot, &o);
        WsParkLog(slot, o.lo, o.y, o.hi, -32768, -32768, e, "hide-pair");
    }
    for (int i = 0; i < 2; i++) {
        const int slot = 98 + i;
        const WsOamSlot o = WsOamSlotRead(ppu, slot);
        const int lo = o.lo, y = o.y, hi = o.hi;
        if (hi && lo == 0x70 && y == 0x70) {
            /* A culled rider is parked at X=$170, Y=$70, large. That raw X
             * is 368. At the 512-px cap (E=128) PpuDecodeOamX wraps only at
             * 256 + E = 384, so 368 lands at +368 and its edge shows at the
             * right; below E=112 the same value wraps to -144 and can show on
             * the left. Neither is a real position, so always move the slot
             * below the field as a small sprite instead of consulting WRAM:
             * even a rider genuinely at -144,112 is outside the capped view. */
            int rel = (int16_t)(ram_read16(wx_addr[i]) - (uint16_t)cam);
            int rel_y = (int16_t)(ram_read16(wy_addr[i]) - (uint16_t)cam_y);
            WsOamSlotHide(ppu, slot, &o);
            WsOamXHighLog(slot, -1, rel);
            WsParkLog(slot, lo, y, hi, rel, rel_y, e, "hide");
            continue;
        }
        if (hi) continue;
        if (lo >= e) continue;                 /* only the aliased [0,E) range */
        int16_t rel = (int16_t)(ram_read16(wx_addr[i]) - (uint16_t)cam);
        /* The OAM X is the unshifted rel: the game does TAX before the
         * $03ED shift loop, which only feeds the cull compare. */
        int32_t want = rel;
        if (want < 256 - WS_OAM_XHIGH_SLACK ||
            want >= 256 + e + WS_OAM_XHIGH_SLACK)
            continue;
        /* Tolerate the up-to-one-frame lag of the WRAM position behind the
         * uploaded OAM value: only the clear winner gets the bit. */
        int d_lo = abs(lo - (int)want);
        int d_hi = abs((lo + 256) - (int)want);
        if (d_hi >= d_lo) continue;
        WsOamSlotSetHigh(ppu, slot, &o);
        WsOamXHighLog(slot, lo, (int)want);
    }
}


void WsOamFix(Ppu *ppu)
{
    WsInjectTestOam(ppu);
    WsFixRiderOamXHigh(ppu);
}
