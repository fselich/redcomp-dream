/* Game-specific WRAM names for Uniracers.
 *
 * The framework includes this by name (snes/cpu.c), so every project must
 * provide one. The hand-written port reads and writes these addresses through
 * the names below; the generated code may use the same constants.
 *
 * The read/write helpers are the ONLY place the port touches WRAM directly.
 * Production writes are confined to the cull module (see ws_cull.c); the
 * helpers are generic so the framework can include this file safely.
 */

#ifndef VARIABLES_H
#define VARIABLES_H

#include "types.h"

/* $7E addresses (g_ram index). */
enum {
    kRamRider1X     = 0x0411, /* player unicycle screen X (P1 in 1P and 2P) */
    kRamRider2X     = 0x0413, /* CPU rival in 1P; player 2 in 2P */
    kRamRider1Y     = 0x0415, /* player unicycle screen Y (P1 in 1P and 2P) */
    kRamRider2Y     = 0x0417, /* CPU rival in 1P; player 2 in 2P */
    kRamCamX        = 0x0419, /* race camera X; P1 camera in 2P */
    kRamCam2X       = 0x041B, /* P2 camera X in 2P split screen */
    kRamCamY        = 0x041D, /* race camera Y; P1 camera in 2P */
    kRamCam2Y       = 0x041F, /* P2 camera Y in 2P split screen */
    kRamCullLeft    = 0x0421, /* rider cull window, left edge */
    kRamCullRight   = 0x0423, /* rider cull window, right edge */
    kRamAirborne    = 0x0BE9, /* nonzero while the rider is airborne/stunting:
                               * the BG1 rows move with the jump, so the source
                               * self-check holds its anchor while this is set */
    kRamCullZoomShift = 0x03ED, /* cull comparison shift (Zoom Zoo = 2) */
    kRamRaceMode    = 0x00B4, /* nonzero in race/pause; 0x0B on later tours */
    kRamPauseFinish = 0x0EE9, /* nonzero in pause and after the finish */
    kRamScreenMode  = 0x0300, /* 0/1 in race/countdown, larger in menus */
    kRamTwoPlayer   = 0x0DDB, /* nonzero in 2P split screen */
};

/* $7F track table, unpacked by $81:B8F1. Offsets are relative to $7F:0000. */
enum {
    kRam7fBase             = 0x10000, /* g_ram index of $7F:0000 */
    kRam7fTrackLibraryEnd  = 0x000B,  /* library end word (rewritten per load) */
    kRam7fTrackColsBlock   = 0x000D,  /* grid cols / 4 (0 means 256) */
    kRam7fTrackRowsBlock   = 0x000E,  /* grid rows / 4 (0 means 256) */
    kRam7fTrackGrid        = 0x000F,  /* u16 piece-id grid */
    kRam7fTrackLibrary     = 0x800F,  /* 4x4-cell piece library */
    kRam7fTrackGridBytes   = 0x8000,  /* fixed id-grid size */
};

/* Battery SRAM (g_sram index). See docs/formats/SAVE_FORMAT.md; the
 * checksummed block table lives in uniracers_save.c. */
enum {
    kSramSize          = 0x2000,
    kSramTourMedal0    = 0x069C, /* medal under starting tour n: + n * $20 */
    kSramLastLevel     = 0x10D1, /* 0 Bronze, 1 Silver, 2 Gold */
    kSramTourStage     = 0x10D3, /* tours shown in PICK TOUR, not checksummed */
    kSramTourStageAll  = 3,      /* >= 3: all nine tours, Hunter included */
};

extern uint8_t g_ram[0x20000];

static inline uint16_t ram_read16(uint16_t addr)
{
    return (uint16_t)(g_ram[addr] | ((uint16_t)g_ram[(uint16_t)(addr + 1)] << 8));
}

static inline void ram_write16(uint16_t addr, uint16_t value)
{
    g_ram[addr] = (uint8_t)(value & 0xFF);
    g_ram[(uint16_t)(addr + 1)] = (uint8_t)(value >> 8);
}

/* $7F:xxxx is g_ram[0x10000 + xxxx]. */
static inline uint16_t ram7f_read16(uint16_t off)
{
    return (uint16_t)(g_ram[kRam7fBase + off] |
                    ((uint16_t)g_ram[kRam7fBase + (uint16_t)(off + 1)] << 8));
}

#endif /* VARIABLES_H */
