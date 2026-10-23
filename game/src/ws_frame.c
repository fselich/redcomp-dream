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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kWsRaceMaxExtra = 128, kWsEnvCache = 32 };

typedef struct {
    const char *name;
    const char *value;
    int set;
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
        cache[used].set = 1;
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

int WsEnvInt(const char *name, int def, int lo, int hi)
{
    const char *e = WsEnvStr(name);
    int v = e && *e ? atoi(e) : def;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

int WsWideEnabled(void)
{
    const char *e = WsEnvStr("SNESRECOMP_WS");
    if (e && *e) return strcmp(e, "0") != 0;
    return g_config.widescreen;
}

int WsInLiveRace(void)
{
    /* kRamRaceMode is 1 on tour 1 but 0x0B on later tours: only "not zero" is
     * a race signal. kRamScreenMode <= 1 excludes the menus; kRamPauseFinish
     * is nonzero in pause and after the finish. */
    return g_ram[kRamRaceMode] != 0 && g_ram[kRamPauseFinish] == 0 &&
           g_ram[kRamScreenMode] <= 1;
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

static void WsConfigReadBool(const char *path, const char *section,
                             const char *key, int *value)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
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
        if (WsCiEq(WsTrim(p), key))
            *value = WsParseBool(WsTrim(eq + 1), *value);
    }
    fclose(f);
}

int WsConfigBool(const char *section, const char *key, int def)
{
    int value = def;
    WsConfigReadBool("config.ini", section, key, &value);
    WsConfigReadBool("config.local.ini", section, key, &value);
    return value;
}
