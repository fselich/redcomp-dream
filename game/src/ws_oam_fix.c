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
    int tile, attr;
} WsOamSlot;

static WsOamSlot WsOamSlotRead(const Ppu *ppu, int slot)
{
    WsOamSlot o;
    o.lo = ppu->oam[slot * 2] & 0xFF;
    o.y = ppu->oam[slot * 2] >> 8;
    o.tile = ppu->oam[slot * 2 + 1] & 0xFF;
    o.attr = ppu->oam[slot * 2 + 1] >> 8;
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

static void WsOamSlotClearHigh(Ppu *ppu, int slot, const WsOamSlot *o)
{
    ppu->highOam[slot >> 2] &= (uint8_t)~(1u << o->bit);
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

static void Ws2pOamActionLog(int slot, int half, const WsOamSlot *o,
                             int rel, int rel_y, int e, const char *action)
{
    FILE *f = WsLogOpen("SNESRECOMP_WS_2P_OAM_LOG",
        "# frame,slot,half,lo,y,hi,tile,attr,rel,rel_y,E,action");
    if (!f) return;
    fprintf(f, "%d,%d,%d,%d,%d,%d,0x%02x,0x%02x,%d,%d,%d,%s\n",
            snes_frame_counter, slot, half, o->lo, o->y, o->hi, o->tile,
            o->attr, rel, rel_y, e, action);
    fflush(f);
}

typedef struct {
    int slot, half, is_p1, yoff;
    int sent_lo, sent_y, sent_hi;
    uint16_t rx, ry, cx, cy;
} Ws2pRiderSlot;

static const Ws2pRiderSlot k2pRiderSlots[4] = {
    /* slot, half, P1?, +$70, sentinel, rider X/Y, camera X/Y */
    { 96, 1, 0, 0x70, 0x30, 0x30, 1, kRamRider2X, kRamRider2Y, kRamCam2X, kRamCam2Y },
    { 97, 1, 1, 0x70, 0x30, 0x30, 1, kRamRider1X, kRamRider1Y, kRamCam2X, kRamCam2Y },
    { 98, 0, 1, 0x00, 0x70, 0x70, 0, kRamRider1X, kRamRider1Y, kRamCamX,  kRamCamY  },
    { 99, 0, 0, 0x00, 0x70, 0x70, 0, kRamRider2X, kRamRider2Y, kRamCamX,  kRamCamY  },
};

/* The staged tile/attribute patterns measured in task 1.4. Sentinels are
 * checked before this, so the 0x26/0x28 variants are the parked graphics. */
static int Ws2pLooksLikeRider(const WsOamSlot *o, int is_p1)
{
    if (is_p1)
        return o->tile == 0x00 && (o->attr == 0x66 || o->attr == 0x26);
    return o->tile == 0x88 && (o->attr == 0x68 || o->attr == 0x28);
}

static void WsFixRiderOamXHigh2P(Ppu *ppu)
{
    const int e = WsExtra();
    if (e <= 0) return;

    for (int i = 0; i < 4; i++) {
        const Ws2pRiderSlot *s = &k2pRiderSlots[i];
        const WsOamSlot o = WsOamSlotRead(ppu, s->slot);
        const int rel = (int16_t)(ram_read16(s->rx) - ram_read16(s->cx));
        const int rel_y = (int16_t)(ram_read16(s->ry) - ram_read16(s->cy));

        /* 1. The half's parking sentinel. Its Y cannot belong to a staged
         * real rider: the vertical window is [-41,112), so a top entry never
         * has Y=0x70 and a bottom entry has Y=relY+0x70 >= 0x47, never 0x30. */
        if (o.lo == s->sent_lo && o.y == s->sent_y && o.hi == s->sent_hi) {
            WsOamSlotHide(ppu, s->slot, &o);
            Ws2pOamActionLog(s->slot, s->half, &o, rel, rel_y, e, "hide");
            continue;
        }

        if (!Ws2pLooksLikeRider(&o, s->is_p1)) {
            Ws2pOamActionLog(s->slot, s->half, &o, rel, rel_y, e, "skip");
            continue;
        }

        /* 2. The high bit is normally the game's negative-X encoding. If the
         * WRAM position is positive and clearly prefers the low byte, the
         * bit was set for a positive rider (measured on real bottom riders in
         * task 1.4); clear it. */
        if (o.hi) {
            const int high_value = (o.lo < e) ? (o.lo + 256) : (o.lo - 256);
            if (rel > 0 && abs(o.lo - rel) < abs(high_value - rel)) {
                WsOamSlotClearHigh(ppu, s->slot, &o);
                Ws2pOamActionLog(s->slot, s->half, &o, rel, rel_y, e,
                                 "xhigh-clear");
            } else {
                Ws2pOamActionLog(s->slot, s->half, &o, rel, rel_y, e,
                                 rel > 0 ? "hi-positive" : "keep");
            }
            continue;
        }

        /* 3. A low byte at or beyond the margin already decodes natively. */
        if (o.lo >= e) {
            Ws2pOamActionLog(s->slot, s->half, &o, rel, rel_y, e, "keep");
            continue;
        }

        /* 4. Positive X in [256, 256+E) lost its bit 8 in the low byte. Use
         * the same one-frame slack as the 1P fix. */
        if (rel >= 256 - WS_OAM_XHIGH_SLACK &&
            rel < 256 + e + WS_OAM_XHIGH_SLACK) {
            const int d_lo = abs(o.lo - rel);
            const int d_hi = abs((o.lo + 256) - rel);
            if (d_hi < d_lo) {
                WsOamSlotSetHigh(ppu, s->slot, &o);
                Ws2pOamActionLog(s->slot, s->half, &o, rel, rel_y, e, "xhigh");
                continue;
            }
        }

        Ws2pOamActionLog(s->slot, s->half, &o, rel, rel_y, e, "keep");
    }
    (void)k2pRiderSlots[0].yoff;
}

/* Per-line companion to the frame-level 2P fix. The framework's draw does not
 * replay OAMDATA, and the game's single end-of-frame high table has the
 * bottom entries small (the top entries are big). The bottom half needs its
 * own rider entries with the big size bit and the high bit that matches the
 * true X; write that into the PPU high table before each line. No guest
 * memory is touched. */
void WsOamLineFix(Ppu *ppu, int line)
{
    if (!ppu || g_ram[kRamTwoPlayer] == 0) return;
    if (!WsInLiveRace() || !WsCullIsEnabled()) return;
    if (!WsOamXHighEnabled()) return;
    int vp = WsViewportOfLine(1, line);
    if (vp < 0) return;
    int e = WsExtra();

    int slots[2];
    if (vp == 0) { slots[0] = 98; slots[1] = 99; }
    else         { slots[0] = 96; slots[1] = 97; }

    for (int k = 0; k < 2; k++) {
        const int slot = slots[k];
        const Ws2pRiderSlot *s = NULL;
        for (int i = 0; i < 4; i++)
            if (k2pRiderSlots[i].slot == slot) s = &k2pRiderSlots[i];
        if (!s) continue;
        WsOamSlot o = WsOamSlotRead(ppu, slot);
        if (o.y == 0xE0) continue;
        if (o.lo == s->sent_lo && o.y == s->sent_y) {
            /* Native 4:3 has no frame-level sentinel pass; hide it here too. */
            WsOamSlotHide(ppu, slot, &o);
            continue;
        }
        if (!Ws2pLooksLikeRider(&o, s->is_p1)) continue;

        const int rel = (int16_t)(ram_read16(s->rx) - ram_read16(s->cx));
        const int hi = (rel < 0 || (rel >= 256 && rel < 256 + e)) ? 1 : 0;
        if (hi) WsOamSlotSetHigh(ppu, slot, &o);
        else WsOamSlotClearHigh(ppu, slot, &o);
        /* The bottom entries are staged small; the rider graphic needs the
         * big size bit in both halves. */
        ppu->highOam[slot >> 2] |= (uint8_t)(1u << (o.bit + 1));
    }
}

static void WsFixRiderOamXHigh(Ppu *ppu)
{
    if (!ppu) return;
    if (!WsWideEnabled() || !WsInLiveRace()) return;
    if (!WsCullIsEnabled() || WsExtra() <= 0) return;
    if (!WsOamXHighEnabled()) return;
    if (g_ram[kRamTwoPlayer] != 0) {
        WsFixRiderOamXHigh2P(ppu);
        return;
    }

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


/* 2P OAM/staging diagnostic. The durable per-slot action log is
 * SNESRECOMP_WS_2P_OAM_LOG; this raw staging trace is kept for measurement. */
static void Ws2pOamLog(Ppu *ppu)
{
    if (!ppu || g_ram[kRamTwoPlayer] == 0) return;
    FILE *f = WsLogOpen("SNESRECOMP_WS_2P_OAM_STAGE_LOG",
        "# frame,hi24,oam192,oam193,oam194,oam195,oam196,oam197,oam198,oam199,"
        "stage1501,stage1599,stage20a2,stage20a3,"
        "r1x,r1y,r2x,r2y,c1x,c1y,c2x,c2y,"
        "r1t_x,r1t_y,r2t_x,r2t_y,r2b_x,r2b_y,r1b_x,r1b_y");
    if (!f) return;
    char stage[33];
    for (int i = 0; i < 16; i++)
        snprintf(stage + i * 2, 3, "%02x", g_ram[0x1501 + i]);
    stage[32] = 0;
    int16_t r1x = (int16_t)ram_read16(kRamRider1X);
    int16_t r1y = (int16_t)ram_read16(kRamRider1Y);
    int16_t r2x = (int16_t)ram_read16(kRamRider2X);
    int16_t r2y = (int16_t)ram_read16(kRamRider2Y);
    int16_t c1x = (int16_t)ram_read16(kRamCamX);
    int16_t c1y = (int16_t)ram_read16(kRamCamY);
    int16_t c2x = (int16_t)ram_read16(kRamCam2X);
    int16_t c2y = (int16_t)ram_read16(kRamCam2Y);
    fprintf(f, "%d,0x%02x,%04x,%04x,%04x,%04x,%04x,%04x,%04x,%04x,%s,"
               "0x%02x,0x%02x,0x%02x,"
               "%d,%d,%d,%d,%d,%d,%d,%d,"
               "%d,%d,%d,%d,%d,%d,%d,%d\n",
            snes_frame_counter, ppu->highOam[24],
            ppu->oam[192], ppu->oam[193], ppu->oam[194], ppu->oam[195],
            ppu->oam[196], ppu->oam[197], ppu->oam[198], ppu->oam[199],
            stage, g_ram[0x1599], g_ram[0x20A2], g_ram[0x20A3],
            r1x, r1y, r2x, r2y, c1x, c1y, c2x, c2y,
            (int16_t)(r1x - c1x), (int16_t)(r1y - c1y),
            (int16_t)(r2x - c1x), (int16_t)(r2y - c1y),
            (int16_t)(r2x - c2x), (int16_t)(r2y - c2y),
            (int16_t)(r1x - c2x), (int16_t)(r1y - c2y));
    fflush(f);
}

void WsOamFix(Ppu *ppu)
{
    Ws2pOamLog(ppu);
    WsInjectTestOam(ppu);
    WsFixRiderOamXHigh(ppu);
}
