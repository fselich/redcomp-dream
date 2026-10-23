/* Lazy debug-log files. See ws_debug.h. */
#include "ws_debug.h"

#include "ws_frame.h"

#include <string.h>

enum { kWsLogCache = 16 };

typedef struct {
    const char *env_name;
    FILE *file;
    int tried;
} WsLogSlot;

FILE *WsLogOpen(const char *env_name, const char *header)
{
    static WsLogSlot cache[kWsLogCache];
    static int used;
    for (int i = 0; i < used; i++) {
        if (cache[i].env_name == env_name ||
            strcmp(cache[i].env_name, env_name) == 0) {
            if (!cache[i].tried) {
                cache[i].tried = 1;
                const char *path = WsEnvStr(env_name);
                if (path && *path) {
                    cache[i].file = fopen(path, "w");
                    if (cache[i].file && header && *header) {
                        fprintf(cache[i].file, "%s\n", header);
                        fflush(cache[i].file);
                    }
                }
            }
            return cache[i].file;
        }
    }
    if (used >= kWsLogCache) return NULL;
    cache[used].env_name = env_name;
    cache[used].file = NULL;
    cache[used].tried = 0;
    used++;
    return WsLogOpen(env_name, header);
}
