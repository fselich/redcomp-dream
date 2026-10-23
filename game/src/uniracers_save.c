/* Battery-SRAM helpers and the opt-in tour unlock. See uniracers_save.h.
 *
 * Guest-state rule: this module writes one SRAM byte (kSramTourStage), only
 * when UnlockAllTours is on, never in netplay, only upward, and only into a
 * valid image. The game saves it on exit, so the unlock is permanent.
 */
#include "uniracers_save.h"

#include "common_rtl.h"
#include "variables.h"
#include "ws_frame.h"
#include "ws_settings.h"

#include <stdio.h>
#include <string.h>

extern int snes_netplay_active(void);

static const char kSramMagic[] = "ASJIver3.30";

/* Each block [start, end) is followed at `end` by the 16-bit sum of its
 * little-endian words (verified on fresh and progressed saves). */
static const struct { uint16_t start, end; } kSramBlocks[] = {
    { 0x000C, 0x016C }, /* names */
    { 0x0230, 0x02B0 }, /* progress counters */
    { 0x02B2, 0x054E }, /* records */
    { 0x0550, 0x05E6 }, /* track table */
    { 0x05E8, 0x073C }, /* tours */
};

static int s_unlock_announced;

static uint16_t SramRead16(int off)
{
    return (uint16_t)(g_sram[off] | (g_sram[off + 1] << 8));
}

int SaveImageValid(void)
{
    if (!g_sram || g_sram_size < kSramSize) return 0;
    if (memcmp(g_sram, kSramMagic, sizeof(kSramMagic) - 1) != 0) return 0;
    for (size_t b = 0; b < sizeof(kSramBlocks) / sizeof(kSramBlocks[0]); b++) {
        uint16_t sum = 0;
        for (int i = kSramBlocks[b].start; i < kSramBlocks[b].end; i += 2)
            sum = (uint16_t)(sum + SramRead16(i));
        if (sum != SramRead16(kSramBlocks[b].end)) return 0;
    }
    return 1;
}

void SaveUnlockTick(void)
{
    if (!WsSettingBool(WS_SET_UNLOCK_TOURS) || snes_netplay_active()) return;
    if (!g_sram || g_sram_size < kSramSize) return;
    /* Cheap test first: nothing to do once every tour is shown. */
    if (g_sram[kSramTourStage] >= kSramTourStageAll) return;
    /* Never write into a blank, foreign or half-written image: a missing save
     * becomes valid once the game has formatted it during boot. */
    if (!SaveImageValid()) return;
    uint8_t old = g_sram[kSramTourStage];
    g_sram[kSramTourStage] = kSramTourStageAll;
    if (!s_unlock_announced) {
        s_unlock_announced = 1;
        fprintf(stderr, "[unlock] tour stage %u -> %u (UnlockAllTours)\n",
                old, (unsigned)kSramTourStageAll);
    }
}
