/* rnet_chat_report.c — see recomp_net/chat_report.h. */

#include "recomp_net/chat_report.h"

#include <stdio.h>
#include <string.h>

/* JSON string escaping, local because this file must not depend on a lobby
 * client's copy -- the whole point is that it belongs to none of them.
 * Conservative: anything below 0x20 becomes \uXXXX rather than being dropped,
 * so a control character in a note cannot end the string early. */
static size_t json_escape(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!in) return 0;
    for (; *in; ++in) {
        const unsigned char c = (unsigned char)*in;
        char buf[8];
        const char *piece = buf;
        size_t len;
        if (c == '"' || c == '\\') {
            buf[0] = '\\'; buf[1] = (char)c; buf[2] = '\0'; len = 2;
        } else if (c == '\n') {
            piece = "\\n"; len = 2;
        } else if (c == '\r') {
            piece = "\\r"; len = 2;
        } else if (c == '\t') {
            piece = "\\t"; len = 2;
        } else if (c < 0x20) {
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            len = strlen(buf);
        } else {
            buf[0] = (char)c; buf[1] = '\0'; len = 1;
        }
        if (o + len + 1 > cap) break;
        memcpy(out + o, piece, len);
        o += len;
        out[o] = '\0';
    }
    return o;
}

/* One `"key":"escaped",` pair, appended. Returns 0 if it did not fit, and the
 * caller abandons the whole frame -- see the header on never writing a partial
 * report. A field omitted because it did not fit would be a report that quietly
 * says less than the reporter did. */
static int append_str(char *out, size_t cap, size_t *o,
                      const char *key, const char *value)
{
    char esc[1024];
    int n;
    if (!value) value = "";
    json_escape(value, esc, sizeof(esc));
    n = snprintf(out + *o, cap - *o, "\"%s\":\"%s\",", key, esc);
    if (n < 0 || (size_t)n >= cap - *o) return 0;
    *o += (size_t)n;
    return 1;
}

size_t rnet_chat_report_build(char *out, size_t cap,
                              const char *const *mids, int mid_count,
                              const char *reason, const char *note,
                              const RNetChatReportMeta *meta)
{
    static const RNetChatReportMeta kEmpty;
    size_t o = 0;
    int i;
    int written = 0;
    int n;
    char note_cut[RNET_REPORT_NOTE_MAX + 1];

    if (!out || cap < 64 || !mids || mid_count <= 0) return 0;
    if (!meta) meta = &kEmpty;
    if (mid_count > RNET_REPORT_MAX_MESSAGES)
        mid_count = RNET_REPORT_MAX_MESSAGES;

    n = snprintf(out, cap, "{\"op\":\"chat_report\",\"v\":1,");
    if (n < 0 || (size_t)n >= cap) return 0;
    o = (size_t)n;

    /* The note is cut here as well as at the server, so the wire itself cannot
     * be used as a channel by a client that skips the UI. */
    snprintf(note_cut, sizeof(note_cut), "%s", note ? note : "");

    if (!append_str(out, cap, &o, "reason",
                    (reason && reason[0]) ? reason : RNET_REPORT_OTHER))
        return 0;
    if (!append_str(out, cap, &o, "note", note_cut)) return 0;
    if (!append_str(out, cap, &o, "game", meta->game)) return 0;
    if (!append_str(out, cap, &o, "game_version", meta->game_version)) return 0;
    if (!append_str(out, cap, &o, "platform", meta->platform)) return 0;
    if (!append_str(out, cap, &o, "server", meta->server)) return 0;
    if (!append_str(out, cap, &o, "lobby", meta->lobby)) return 0;
    if (!append_str(out, cap, &o, "scope", meta->scope)) return 0;

    /* An ARRAY, and an array even for one id.
     *
     * A ';'-joined string would be valid JSON that every reader using
     * as_array() sees as empty -- which in this repo is not a hypothetical:
     * match_caps.mod_plan shipped that way and a host with a full plan read as
     * a host requiring nothing, failing open and silently. A list of things
     * the server must look up is exactly where that direction of failure is
     * worst. */
    n = snprintf(out + o, cap - o, "\"mids\":[");
    if (n < 0 || (size_t)n >= cap - o) return 0;
    o += (size_t)n;

    for (i = 0; i < mid_count; ++i) {
        char esc[96];
        if (!mids[i] || !mids[i][0]) continue;   /* unreportable line */
        json_escape(mids[i], esc, sizeof(esc));
        n = snprintf(out + o, cap - o, "%s\"%s\"", written ? "," : "", esc);
        if (n < 0 || (size_t)n >= cap - o) return 0;
        o += (size_t)n;
        ++written;
    }
    if (!written) return 0;      /* nothing reportable was passed */

    n = snprintf(out + o, cap - o, "]}");
    if (n < 0 || (size_t)n >= cap - o) return 0;
    o += (size_t)n;
    return o;
}
