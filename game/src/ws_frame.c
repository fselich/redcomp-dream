/* Widescreen frame predicates and cached environment helpers.
 *
 * See ws_frame.h. This module reads g_config and guest WRAM; it never writes
 * guest state.
 */
#include "ws_frame.h"

#include "common_rtl.h"
#include "config.h"
#include "variables.h"
#include "widescreen.h"
#include "ws_cull.h"
#include "ws_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kWsRaceMaxExtra = 128, kWsEnvCache = 32 };

typedef struct {
    const char *name;
    const char *value;
} WsEnvSlot;

const char *WsEnvStr(const char *name)
{
    static WsEnvSlot cache[kWsEnvCache];
    static int used;
    for (int i = 0; i < used; i++) {
        if (cache[i].name == name || strcmp(cache[i].name, name) == 0)
            return cache[i].value;
    }
    const char *value = getenv(name);
    if (used < kWsEnvCache) {
        cache[used].name = name;
        cache[used].value = value;
        used++;
    }
    return value;
}

int WsEnvFlag(const char *name, int def)
{
    const char *e = WsEnvStr(name);
    if (!e || !*e) return def;
    return strcmp(e, "0") != 0;
}

int WsWideEnabled(void)
{
    return WsSettingBool(WS_SET_WIDESCREEN);
}

int WsInLiveRace(void)
{
    /* kRamPauseFinish is nonzero in pause and after the finish; kRamScreenMode
     * <= 1 excludes the menus and selection screens. In 1P kRamRaceMode is 1
     * on tour 1 and 0x0B on later tours (only "not zero" is a race signal).
     * In 2P/VS kRamRaceMode is 0 but kRamTwoPlayer is 1 for the split-screen
     * race. */
    if (g_ram[kRamPauseFinish] != 0 || g_ram[kRamScreenMode] > 1) return 0;
    /* Menus and the track-selection transition can leave the race-mode byte
     * set with $0300=0. The rider setup writes a plausible race cull window;
     * a menu/transition window is not. */
    if (!WsCullLooksLive()) return 0;
    if (g_ram[kRamTwoPlayer] != 0) return 1;       /* 2P/VS split screen */
    return g_ram[kRamRaceMode] != 0;               /* 1P: 1 or 0x0B */
}

int WsViewportOfLine(int two_player, int line)
{
    if (!two_player) return 0;
    if (line < WS_VP_DIVIDER_FIRST) return 0;
    if (line < WS_VP_BOTTOM_FIRST) return -1;
    return 1;
}

int WsExtra(void)
{
    int e = g_ws_extra;
    if (e < 0) e = 0;
    if (e > kWsRaceMaxExtra) e = kWsRaceMaxExtra;
    return e;
}

void WsFrameCtxBuild(WsFrameCtx *ctx, Ppu *ppu)
{
    ctx->ppu = ppu;
    ctx->wide = WsWideEnabled();
    ctx->live = WsInLiveRace();
    ctx->active = ppu && ctx->wide && ctx->live;
    ctx->extra = WsExtra();
    ctx->two_player = g_ram[kRamTwoPlayer] != 0;
}

char *WsTrim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' ||
                     e[-1] == '\n'))
        e--;
    *e = 0;
    return s;
}

int WsCiEq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++, b++;
    }
    return *a == *b;
}

int WsParseBool(const char *v, int def)
{
    if (WsCiEq(v, "1") || WsCiEq(v, "true") || WsCiEq(v, "on") ||
        WsCiEq(v, "yes"))
        return 1;
    if (WsCiEq(v, "0") || WsCiEq(v, "false") || WsCiEq(v, "off") ||
        WsCiEq(v, "no"))
        return 0;
    return def;
}

int WsConfigReadStr(const char *path, const char *section, const char *key,
                    char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int found = 0;
    char line[512];
    int in_section = 0;
    while (fgets(line, sizeof(line), f)) {
        char *hash = strpbrk(line, ";#");
        if (hash) *hash = 0;
        char *p = WsTrim(line);
        if (!*p) continue;
        if (*p == '[') {
            in_section = WsCiEq(p, section);
            continue;
        }
        if (!in_section) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        if (WsCiEq(WsTrim(p), key)) {
            snprintf(out, cap, "%s", WsTrim(eq + 1));
            found = 1;
        }
    }
    fclose(f);
    return found;
}
