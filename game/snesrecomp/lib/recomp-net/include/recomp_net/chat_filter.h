/* recomp_net/chat_filter.h — mask profanity and slurs in a chat line.
 *
 * One list, one algorithm, every path: the lobby server runs the same rules
 * (recomp-net-server/src/chat_filter.rs) on lines it relays, and every
 * client runs this on every line it puts in its chat ring -- so a LAN room,
 * which has no server, is filtered exactly like an online one, and a line
 * from an older server is filtered on arrival.
 *
 * The list lives in data/chat_filter_words.txt (see its header for the
 * matching rules); tools/gen_chat_filter_words.py bakes it into the library.
 *
 * Text is matched after folding: lower case, Latin diacritics stripped,
 * full-width ASCII narrowed, leetspeak undone. Repeated letters and
 * separators between the letters of a word are tolerated, so "fuuuck" and
 * "f.u.c.k" are caught. A hit is replaced by one '*' per character.
 *
 * Environment: RNET_CHAT_FILTER=0 turns the client-side filter off (a
 * developer looking at raw lines); the server's own switch is CHAT_FILTER. */
#ifndef RECOMP_NET_CHAT_FILTER_H
#define RECOMP_NET_CHAT_FILTER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mask in place. `text` is NUL-terminated UTF-8 in a buffer of `cap` bytes;
 * the result is never longer than the input. Returns the number of masked
 * ranges (0 = untouched). Malformed UTF-8 is passed through unchanged. */
int rnet_chat_filter_apply(char *text, size_t cap);

/* Copying form for callers that keep the original. Returns masked ranges. */
int rnet_chat_filter_copy(const char *in, char *out, size_t out_cap);

/* 1 when the filter will act (list loaded, not disabled by environment). */
int rnet_chat_filter_enabled(void);

/* Number of entries the built-in list parsed to (0 before first use). */
int rnet_chat_filter_word_count(void);

#ifdef __cplusplus
}
#endif

#endif /* RECOMP_NET_CHAT_FILTER_H */
