/* Lazy debug-log files.
 *
 * Every CSV/text log in the port opens its file on the first write, keyed by
 * an environment variable (SNESRECOMP_WS_*_LOG). This module owns that open
 * and its per-process cache. It never touches guest state.
 */
#ifndef WS_DEBUG_H
#define WS_DEBUG_H

#include <stdio.h>

/* Returns the cached FILE* for env_name, opening it on first use. Writes
 * header once if non-NULL. Returns NULL when the variable is unset or the
 * file cannot be opened; the caller treats that as "logging disabled". */
FILE *WsLogOpen(const char *env_name, const char *header);

#endif /* WS_DEBUG_H */
