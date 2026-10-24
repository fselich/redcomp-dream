/* $7F track-table reader and source anchor.
 *
 * Reads the unpacked Uniracers track table from WRAM bank $7F, searches the
 * world-to-table anchor from verified native cells and serves margin words.
 * Presentation-only: reads guest memory, writes nothing.
 *
 * 2P split screen has two independent cameras and two BG1 maps, so the anchor
 * set, scene generation, race start and seen-table reads are per viewport.
 * The track grid header and the piece-library word index are shared.
 */
#include "ws_source.h"

#include "common_rtl.h"
#include "variables.h"
#include "ws_debug.h"
#include "ws_frame.h"
#include "ws_ring.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ── Track-table source (margin policy `source`) ──────────────────────────
 *
 * The whole track is in WRAM bank $7F from load to finish (RNC1-unpacked by
 * $81:B8F1): a u16 piece-id grid at $7F:000F (cols x rows, kRam7fTrackGridBytes bytes) and
 * a library of 4x4-cell pieces at $7F:800F. See docs/formats/TRACK_FORMAT.md.
 * Margin cells that the ring cannot verify are read from that table at
 * world + anchor. The anchor (table cell - world key, constant per race) is
 * searched from the verified native cells and re-checked every frame; while
 * it is invalid only ring-verified cells are drawn and the rest is masked.
 * Ring cells that read 0x0000 are never used as evidence: at race start the
 * game leaves some ring cells unwritten (they show 0x0000 natively for a few
 * frames), so they say nothing about the table. Nor are words that occur
 * nowhere in the piece library (the race-start overlay draws such words into
 * the ring). The search uses every verified cell still in the seen table, not
 * only this frame's: the map is static, so older observations stay true and
 * their wider footprint resolves repetitive geometry (Switcher). The
 * self-check uses this frame's cells only. Repeated sections can leave a few
 * equally good anchors; up to WS_SRC_MAX_ANCHORS are kept and a margin cell is
 * served only where all of them give the same word (the self-check prunes
 * them as the camera moves on). Presentation-only: reads g_ram, writes
 * nothing. See openspec/changes/archive/2026-10-22-uniracers-ws-source. */
#define WS_SRC_MIN_SAMPLES 8
#define WS_SRC_MAX_SAMPLES 256
#define WS_SRC_CHECK_SAMPLES 32
#define WS_SRC_MAX_CANDIDATES 4096
#define WS_SRC_MAX_PIECES 2048
#define WS_SRC_MAX_ANCHORS 8
#define WS_SRC_SEED_WAIT 10
#define WS_SRC_SELF_GRACE 30

typedef struct {
    int32_t wx, wy;
    uint16_t tile;
} WsSrcSample;

typedef struct {
    int valid;                                  /* number of live anchors */
    int32_t ax[WS_SRC_MAX_ANCHORS], ay[WS_SRC_MAX_ANCHORS];
    unsigned generation;
    int race_frame0;                            /* first live frame of this scene */
    int seed_fail;                              /* failed searches since samples */
    int seed_world_valid;                       /* start world X was captured */
    int32_t seed_world_x;                        /* rider world X at the start */
    int self_fail[WS_SRC_MAX_ANCHORS];           /* consecutive failed self-checks */
} WsSrcVp;

static WsSrcVp s_src_vp[WS_VP_MAX];
static int s_src_cols, s_src_rows;             /* grid size in cells */
static uint8_t s_src_word_present[0x10000 / 8]; /* words in the piece library */
static uint16_t s_src_present_end;              /* library end it was built for */

static void WsSrcLog(int vp, const char *fmt, ...)
{
    FILE *f = WsLogOpen("SNESRECOMP_WS_SOURCE_LOG", NULL);
    if (!f) return;
    if (vp != 0) fprintf(f, "vp=%d ", vp);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fflush(f);
}

/* Grid dimensions from the unpacked header: $7F:000D = cols/4, $7F:000E =
 * rows/4 (0 means 256). The id grid is always kRam7fTrackGridBytes bytes. */
static int WsSrcHeader(void)
{
    int cb = (g_ram[kRam7fBase + kRam7fTrackColsBlock] ? g_ram[kRam7fBase + kRam7fTrackColsBlock] : 256);
    int rb = (g_ram[kRam7fBase + kRam7fTrackRowsBlock] ? g_ram[kRam7fBase + kRam7fTrackRowsBlock] : 256);
    int cols = cb * 4, rows = rb * 4;               /* blocks */
    if (cols * rows * 2 != kRam7fTrackGridBytes) return 0;
    s_src_cols = cols * 4;
    s_src_rows = rows * 4;
    /* Library end ($7F:000B-000C, rewritten by the game at load). Rebuild the
     * word-presence set when it changes (new track). */
    uint16_t end = ram7f_read16(kRam7fTrackLibraryEnd);
    if (end != s_src_present_end) {
        s_src_present_end = end;
        memset(s_src_word_present, 0, sizeof(s_src_word_present));
        uint32_t lim = end > kRam7fTrackLibrary ? end : 0xFFFE;
        for (uint32_t off = kRam7fTrackLibrary; off + 1 < lim && off + 1 <= 0xFFFF; off += 2) {
            uint16_t w = ram7f_read16(off);
            s_src_word_present[w >> 3] |= (uint8_t)(1u << (w & 7));
        }
    }
    return 1;
}

static int WsSrcWordUsable(uint16_t w)
{
    return w && (s_src_word_present[w >> 3] & (1u << (w & 7)));
}

/* Table word for table cell (tx, ty). The grid is circular in X: on a
 * tour-2 track the race crosses the table's right edge and the good anchor
 * re-appeared at ax +- cols (95, -161, 351, -673 with cols = 256), so X wraps
 * modulo the grid width. Outside the grid in Y the game shows 0x0000.
 * Returns 0 only for an id pointing past the bank. */
static int WsSrcCell(int32_t tx, int32_t ty, uint16_t *out)
{
    if (s_src_cols > 0) {
        tx %= s_src_cols;
        if (tx < 0) tx += s_src_cols;
    }
    if (ty < 0 || ty >= s_src_rows) {
        *out = 0;
        return 1;
    }
    uint32_t id = ram7f_read16(kRam7fTrackGrid + 2u * ((uint32_t)(ty >> 2) *
                                             (uint32_t)(s_src_cols >> 2) +
                                             (uint32_t)(tx >> 2)));
    uint32_t off = kRam7fTrackLibrary + id * 32u + (uint32_t)(ty & 3) * 8u +
                   (uint32_t)(tx & 3) * 2u;
    if (off + 1 > 0xFFFF) return 0;
    *out = ram7f_read16(off);
    return 1;
}

/* Verified native cells of one viewport usable as evidence: this frame's
 * only, or every cell still held in the seen table (all_frames). */
static int WsSrcSamples(int vp, WsSrcSample *out, int max, int all_frames)
{
    int n = 0;
    for (int cy = 0; cy < WS_RING_CELLS && n < max; cy++) {
        for (int cx = 0; cx < WS_RING_CELLS && n < max; cx++) {
            const WsRingSeen *s = WsRingSeenCell(vp, cy, cx);
            if (!s->valid || !WsSrcWordUsable(s->tile))
                continue;
            if (!all_frames && s->frame != snes_frame_counter)
                continue;
            out[n].wx = s->wx;
            out[n].wy = s->wy;
            out[n].tile = s->tile;
            n++;
        }
    }
    return n;
}

static int WsSrcAgreement(const WsSrcSample *smp, int n, int32_t ax,
                          int32_t ay, int max_miss)
{
    int ok = 0, miss = 0;
    for (int i = 0; i < n; i++) {
        uint16_t w;
        if (WsSrcCell(smp[i].wx + ax, smp[i].wy + ay, &w) && w == smp[i].tile)
            ok++;
        else if (++miss > max_miss)
            return -1;
    }
    return ok;
}

/* Seed the anchor from the track header start pair and the first verified
 * samples. The header stores the start X as a big-endian table cell; the
 * rider/camera/scroll give the world X. Y is not in the header (the rider
 * starts a few cells after the line), so it is chosen by table agreement over
 * the samples. The self-check in WsSrcFrame still validates the seed. */
static int WsSrcTrySeed(int vp, const WsSrcSample *smp, int n)
{
    WsSrcVp *v = &s_src_vp[vp];
    if (n <= 0 || s_src_cols <= 0 || !v->seed_world_valid) return 0;
    uint16_t hx = (uint16_t)((g_ram[kRam7fBase + 0x0002] << 8) |
                             g_ram[kRam7fBase + 0x0003]);
    uint16_t hy = (uint16_t)((g_ram[kRam7fBase + 0x0004] << 8) |
                             g_ram[kRam7fBase + 0x0005]);
    if (hx == 0 && hy == 0) return 0;
    if (hx >= s_src_cols) return 0;

    int32_t ax = (int32_t)hx - v->seed_world_x;

    struct { int ay, count; } cand[WS_SRC_MAX_SAMPLES * 2];
    int nc = 0;
    for (int i = 0; i < n; i++) {
        int32_t tx = smp[i].wx + ax;
        for (int ty = 0; ty < s_src_rows; ty++) {
            uint16_t w;
            if (!WsSrcCell(tx, ty, &w) || w != smp[i].tile) continue;
            int ay = ty - smp[i].wy;
            int j = 0;
            for (; j < nc; j++)
                if (cand[j].ay == ay) break;
            if (j == nc) {
                if (nc >= (int)(sizeof(cand) / sizeof(cand[0]))) continue;
                cand[nc].ay = ay;
                cand[nc].count = 0;
                nc++;
            }
            cand[j].count++;
        }
    }
    int best = -1;
    for (int i = 0; i < nc; i++)
        if (best < 0 || cand[i].count > cand[best].count) best = i;
    WsSrcLog(vp, "%d seed try ax=%d n=%d nc=%d best=%d count=%d\n",
             snes_frame_counter, ax, n, nc, best,
             best >= 0 ? cand[best].count : 0);
    if (best < 0 || cand[best].count * 20 < n * 19) return 0;

    v->valid = 1;
    v->ax[0] = ax;
    v->ay[0] = cand[best].ay;
    v->self_fail[0] = 0;
    v->seed_fail = 0;
    WsSrcLog(vp, "%d anchor ok seed ax=%d ay=%d anchors=1 grid=%dx%d "
             "samples=%d header=%d,%d\n", snes_frame_counter, ax, v->ay[0],
             s_src_cols, s_src_rows, n, hx, hy);
    return 1;
}

/* Find the unique anchor matching >= 95 % of this viewport's verified cells.
 * Seeds from the rarest sampled word; candidate positions come from a
 * per-piece index of where that word occurs. */
static void WsSrcSearch(int vp)
{
    WsSrcVp *v = &s_src_vp[vp];
    static WsSrcSample smp[WS_SRC_MAX_SAMPLES];
    static uint16_t piece_mask[WS_SRC_MAX_PIECES];
    if (!WsSrcHeader()) return;
    int n = WsSrcSamples(vp, smp, WS_SRC_MAX_SAMPLES, 1);
    if (n < WS_SRC_MIN_SAMPLES) {
        if (snes_frame_counter % 30 == 0)
            WsSrcLog(vp, "%d anchor wait samples=%d\n", snes_frame_counter, n);
        return;
    }
    if (v->seed_fail >= WS_SRC_SEED_WAIT) {
        static WsSrcSample smp_now[WS_SRC_MAX_SAMPLES];
        int n_now = WsSrcSamples(vp, smp_now, WS_SRC_MAX_SAMPLES, 0);
        if (WsSrcTrySeed(vp, smp_now, n_now)) return;
    }
    int max_miss = n / 20;                       /* >= 95 % agreement */

    /* Try seeds from rarest to commonest word (at most four distinct). */
    int tried = 0;
    uint16_t tried_words[4];
    clock_t t0 = clock();
    while (tried < 4) {
        int seed = -1, seed_count = 0x7FFFFFFF;
        for (int i = 0; i < n; i++) {
            int skip = 0;
            for (int k = 0; k < tried; k++)
                if (tried_words[k] == smp[i].tile) skip = 1;
            if (skip) continue;
            int c = 0;
            for (int j = 0; j < n; j++) c += smp[j].tile == smp[i].tile;
            if (c < seed_count) { seed_count = c; seed = i; }
        }
        if (seed < 0) break;
        uint16_t word = smp[seed].tile;
        tried_words[tried++] = word;

        /* Which positions inside each piece hold the seed word. */
        uint32_t pieces = (0x10000u - kRam7fTrackLibrary) / 32u;
        if (pieces > WS_SRC_MAX_PIECES) pieces = WS_SRC_MAX_PIECES;
        for (uint32_t id = 0; id < pieces; id++) {
            uint16_t m = 0;
            for (int k = 0; k < 16; k++)
                if (ram7f_read16(kRam7fTrackLibrary + id * 32u + (uint32_t)k * 2u) == word)
                    m |= (uint16_t)(1u << k);
            piece_mask[id] = m;
        }
        int bcols = s_src_cols >> 2, brows = s_src_rows >> 2;
        int candidates = 0, survivors = 0;
        int32_t sur_ax[WS_SRC_MAX_ANCHORS], sur_ay[WS_SRC_MAX_ANCHORS];
        for (int by = 0; by < brows && candidates <= WS_SRC_MAX_CANDIDATES; by++) {
            for (int bx = 0; bx < bcols; bx++) {
                uint32_t id = ram7f_read16(kRam7fTrackGrid + 2u * (uint32_t)(by * bcols + bx));
                if (id >= pieces || !piece_mask[id]) continue;
                for (int k = 0; k < 16; k++) {
                    if (!(piece_mask[id] & (1u << k))) continue;
                    candidates++;
                    int32_t ax = bx * 4 + (k & 3) - smp[seed].wx;
                    ax %= s_src_cols;               /* X wraps: one canonical ax */
                    if (ax < 0) ax += s_src_cols;
                    int32_t ay = by * 4 + (k >> 2) - smp[seed].wy;
                    if (WsSrcAgreement(smp, n, ax, ay, max_miss) < 0) continue;
                    int dup = 0;
                    for (int i = 0; i < survivors && i < WS_SRC_MAX_ANCHORS; i++)
                        if (sur_ax[i] == ax && sur_ay[i] == ay) dup = 1;
                    if (dup) continue;
                    if (survivors < WS_SRC_MAX_ANCHORS) {
                        sur_ax[survivors] = ax;
                        sur_ay[survivors] = ay;
                    }
                    survivors++;
                }
            }
        }
        if (candidates > WS_SRC_MAX_CANDIDATES) continue;   /* too common */
        double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (survivors >= 1 && survivors <= WS_SRC_MAX_ANCHORS) {
            for (int i = 0; i < survivors; i++) {
                v->ax[i] = sur_ax[i];
                v->ay[i] = sur_ay[i];
            }
            v->valid = survivors;
            for (int i = 0; i < survivors; i++) v->self_fail[i] = 0;
            v->seed_fail = 0;
            WsSrcLog(vp, "%d anchor ok ax=%d ay=%d anchors=%d grid=%dx%d samples=%d "
                     "seed=%04x candidates=%d latency=%d search_ms=%.3f\n",
                     snes_frame_counter, sur_ax[0], sur_ay[0], survivors,
                     s_src_cols, s_src_rows, n, word, candidates,
                     v->race_frame0 >= 0 ? snes_frame_counter - v->race_frame0 : -1,
                     ms);
            return;
        }
        WsSrcLog(vp, "%d anchor retry seed=%04x candidates=%d survivors=%d "
                 "search_ms=%.3f\n", snes_frame_counter, word, candidates,
                 survivors, ms);
        if (survivors > 0) {            /* too ambiguous: wait for the camera */
            v->seed_fail++;
            return;
        }
    }
    v->seed_fail++;
}

/* After each drawn frame: keep, verify or (re)establish the viewport's anchor. */
static void WsSrcFrame(int vp)
{
    WsSrcVp *v = &s_src_vp[vp];
    if (v->generation != (int)WsRingSceneGeneration(vp)) {
        v->generation = WsRingSceneGeneration(vp);
        if (v->valid)
            WsSrcLog(vp, "%d anchor dropped: scene reset\n", snes_frame_counter);
        v->valid = 0;
        v->seed_fail = 0;
        memset(v->self_fail, 0, sizeof(v->self_fail));
        v->race_frame0 = snes_frame_counter;
        v->seed_world_valid = 0;
        if (WsRingScrollValidVp(vp)) {
            uint16_t rx = vp == 0 ? kRamRider1X : kRamRider2X;
            uint16_t cx = vp == 0 ? kRamCamX : kRamCam2X;
            int rider_screen_x = (int16_t)(ram_read16(rx) - ram_read16(cx));
            v->seed_world_x = WsRingFloor16((int64_t)WsRingScrollHVp(vp) +
                                            rider_screen_x);
            v->seed_world_valid = 1;
        }
    }
    if (!v->valid) {
        WsSrcSearch(vp);
        return;
    }
    if (!WsSrcHeader()) {
        v->valid = 0;
        WsSrcLog(vp, "%d anchor dropped: header invalid\n", snes_frame_counter);
        return;
    }
    WsSrcSample smp[WS_SRC_CHECK_SAMPLES];
    int n = WsSrcSamples(vp, smp, WS_SRC_CHECK_SAMPLES, 0);
    if (n == 0) return;
    int kept = 0, before = v->valid;
    /* While the rider is airborne the jump moves the BG1 rows, so the
     * verified cells stop matching the table even though the anchor still
     * names the table origin. Keep the anchor for the whole jump instead of
     * dropping it (the drop would mask the margins for seconds) and resume
     * the ordinary grace once the rider is back on the track. */
    int airborne = g_ram[kRamAirborne] != 0;
    for (int i = 0; i < v->valid; i++) {
        int ok = WsSrcAgreement(smp, n, v->ax[i], v->ay[i], n);
        if (ok * 20 < n * 19) {
            if (airborne) {
                v->self_fail[i] = 0;
                WsSrcLog(vp, "%d self-check (%d,%d) %d/%d hold air\n",
                         snes_frame_counter, v->ax[i], v->ay[i], ok, n);
            } else if (v->self_fail[i] < WS_SRC_SELF_GRACE) {
                v->self_fail[i]++;
                WsSrcLog(vp, "%d self-check (%d,%d) %d/%d hold %d/%d\n",
                         snes_frame_counter, v->ax[i], v->ay[i], ok, n,
                         v->self_fail[i], WS_SRC_SELF_GRACE);
            } else {
                WsSrcLog(vp, "%d anchor (%d,%d) dropped: self-check %d/%d "
                         "after %d holds\n", snes_frame_counter, v->ax[i],
                         v->ay[i], ok, n, WS_SRC_SELF_GRACE);
                continue;
            }
        } else {
            if (ok != n)
                WsSrcLog(vp, "%d self-check (%d,%d) %d/%d\n",
                         snes_frame_counter, v->ax[i], v->ay[i], ok, n);
            v->self_fail[i] = 0;
        }
        v->ax[kept] = v->ax[i];
        v->ay[kept] = v->ay[i];
        v->self_fail[kept] = v->self_fail[i];
        kept++;
    }
    v->valid = kept;
    if (!kept) {
        WsSrcLog(vp, "%d anchor dropped: no anchor passes the self-check\n",
                 snes_frame_counter);
    } else if (kept != before && kept == 1) {
        WsSrcLog(vp, "%d anchor resolved ax=%d ay=%d\n", snes_frame_counter,
                 v->ax[0], v->ay[0]);
    }
}

static void WsSrcInvalidate(void)
{
    for (int vp = 0; vp < WS_VP_MAX; vp++) {
        WsSrcVp *v = &s_src_vp[vp];
        if (v->valid)
            WsSrcLog(vp, "%d anchor dropped: race not live\n", snes_frame_counter);
        v->valid = 0;
        memset(v->self_fail, 0, sizeof(v->self_fail));
    }
}

int WsSourceValid(void) { return s_src_vp[0].valid > 0; }
int32_t WsSourceAnchorX(int i) { return s_src_vp[0].ax[i]; }
int32_t WsSourceAnchorY(int i) { return s_src_vp[0].ay[i]; }

int WsSourceCell(int32_t tx, int32_t ty, uint16_t *out)
{
    return WsSrcCell(tx, ty, out);
}

int WsSourceAgree(int vp, int32_t wx, int32_t wy, uint16_t *out)
{
    WsSrcVp *v = &s_src_vp[vp < 0 || vp >= WS_VP_MAX ? 0 : vp];
    if (v->valid <= 0) return 0;
    uint16_t w;
    if (!WsSrcCell(wx + v->ax[0], wy + v->ay[0], &w)) return 0;
    for (int i = 1; i < v->valid; i++) {
        uint16_t wi;
        if (!WsSrcCell(wx + v->ax[i], wy + v->ay[i], &wi) || wi != w)
            return 0;
    }
    *out = w;
    return 1;
}

void WsSourceFrame(int vp) { WsSrcFrame(vp); }
void WsSourceInvalidate(void) { WsSrcInvalidate(); }
