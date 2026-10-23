#ifndef GAME_RTL_H
#define GAME_RTL_H

#include "common_cpu_infra.h"

/* One frame of Uniracers. See game_rtl.c — this is the port. */
void GameRunOneFrame(void);
void GameDrawPpuFrame(void);
void GameSessionReset(void);
void GameAfterConfig(void);
struct SnesDesktopHostFrameStats;
void GameAfterRunFrame(const struct SnesDesktopHostFrameStats *stats);

extern const RtlGameInfo kGameInfo;

#endif /* GAME_RTL_H */
