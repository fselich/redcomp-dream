/* Widescreen frame predicates, margin and per-frame context.
 *
 * Owns the single definition of "widescreen enabled" and "live race", the
 * clamped margin, the cached environment lookups and the small parsing
 * helpers. Reads guest state and g_config; writes neither.
 */
#ifndef WS_FRAME_H
#define WS_FRAME_H

#include <stddef.h>

#include "snes/ppu.h"

typedef struct {
    Ppu *ppu;       /* may be NULL */
    int wide;       /* WsWideEnabled() */
    int live;       /* WsInLiveRace() */
    int active;     /* ppu && wide && live */
    int extra;      /* WsExtra() */
    int two_player; /* kRamTwoPlayer != 0 */
} WsFrameCtx;

int WsWideEnabled(void);
int WsInLiveRace(void);
int WsExtra(void);
void WsFrameCtxBuild(WsFrameCtx *ctx, Ppu *ppu);

/* Environment switches are fixed for the life of the process, so each name is
 * looked up once. g_config values are NOT cached. */
const char *WsEnvStr(const char *name);
int WsEnvFlag(const char *name, int def);
int WsEnvInt(const char *name, int def, int lo, int hi);

/* Small shared parsers. */
char *WsTrim(char *s);
int WsCiEq(const char *a, const char *b);
int WsParseBool(const char *v, int def);

/* String `key` from `section` (e.g. "[Video]") of `path`. Returns 1 and copies
 * the trimmed value when the key is found, 0 otherwise. The framework parser
 * ignores unknown keys in its host-owned sections, so a port can read its own
 * keys there without a framework change. */
int WsConfigReadStr(const char *path, const char *section, const char *key,
                    char *out, size_t cap);

/* Boolean `key` from `section` of config.ini, then config.local.ini, falling
 * back to `def`. Kept for the debug helpers; user options live in
 * ws_settings.[ch]. */
int WsConfigBool(const char *section, const char *key, int def);

#endif /* WS_FRAME_H */
