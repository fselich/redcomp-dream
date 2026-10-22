/* recomp_net/chat_report.h — build a chat-moderation report, for any console.
 *
 * WHY THIS IS SHARED
 * ------------------
 * The lobby CLIENT is written once per framework (psx_lobby_client.c,
 * snes_lobby_client.c, and one per console after them), because the transport
 * and the ring belong to that runtime. What a report CONTAINS does not: it is
 * the same JSON, against the same server, whatever machine is being emulated.
 * Written into each lobby client it would be a copy per console that cannot
 * inherit a fix -- and the fix most likely to be needed here is a rule about
 * what may be sent, which is exactly the kind that must not exist in five
 * versions.
 *
 * So this builds the frame and the caller sends it. A lobby client's whole
 * involvement is one call and one queue_send.
 *
 * WHAT A REPORT MAY CONTAIN, AND WHAT IT MAY NOT
 * ----------------------------------------------
 * A report names MESSAGES. It never carries their text.
 *
 * The server relayed those lines and keeps a short ring of them, so it writes
 * down what IT relayed. A report that carried its own copy would let anyone
 * compose a message, attribute it to somebody, and have them sanctioned for
 * words they never typed -- the difference between a moderation record and an
 * accusation. There is deliberately no text parameter below, and adding one
 * would be a defect rather than a feature.
 *
 * Several ids may be reported at once, because harassment is usually a burst
 * rather than a line, and making somebody file six reports to describe one
 * incident produces six rows that each look minor.
 *
 * The metadata says where it happened: which game, which server, which lobby.
 * The server knows some of that already, but not all of it -- a LAN room or a
 * direct connection has no server-side record at all -- and a moderator
 * reading a queue that spans every title needs the context attached to the
 * row rather than inferred from it.
 */
#ifndef RECOMP_NET_CHAT_REPORT_H
#define RECOMP_NET_CHAT_REPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Categories. Anything else a client sends is filed as "other" rather than
 * refused -- a newer client offering a finer category must not have its report
 * thrown away over a label. */
#define RNET_REPORT_HARASSMENT     "harassment"
#define RNET_REPORT_HATE_SPEECH    "hate_speech"
#define RNET_REPORT_SEXUAL_CONTENT "sexual_content"
#define RNET_REPORT_SPAM           "spam"
#define RNET_REPORT_THREATS        "threats"
#define RNET_REPORT_CHEATING       "cheating_claim"
#define RNET_REPORT_OTHER          "other"

/* At most this many messages in one report. A burst is a handful of lines; a
 * request to file hundreds is either a mistake or an attempt to use the queue
 * as storage. */
#define RNET_REPORT_MAX_MESSAGES 16

/* Free text from the reporter. One sentence for a human, never a channel. */
#define RNET_REPORT_NOTE_MAX 500

/* Where it happened.
 *
 * Every field is optional (NULL or "" omits nothing but its own value) so a
 * client that knows less than another still files a usable report.
 *
 * `platform` names the emulated machine ("snes", "psx", …). It is metadata,
 * and metadata only: nothing downstream may key a FILE NAME or a directory on
 * it. Reports from every title land in one queue and are read by one person,
 * and splitting the evidence by console would fragment a moderation record
 * along a line that has nothing to do with moderation.
 */
typedef struct RNetChatReportMeta {
    const char *game;          /* title as the lobby announces it */
    const char *game_version;  /* the release this client is */
    const char *platform;      /* emulated machine; metadata only, see above */
    const char *server;        /* lobby server this ran against; "" for LAN */
    const char *lobby;         /* lobby/room id; "" outside a room */
    const char *scope;         /* "lobby" | "server" — which chat channel */
} RNetChatReportMeta;

/*
 * Build the `chat_report` frame into `out`.
 *
 * `mids` are the server's own ids for the lines being reported, from the chat
 * ring (SnesLobbyChatMsg.mid / PsxLobbyChatMsg.mid). Entries that are NULL or
 * empty are skipped; a line with no id cannot be reported at all, which is the
 * right answer for a system line, a locally generated one, and anything from a
 * server too old to assign one — in each case there is no referent both sides
 * agree on.
 *
 * Returns the number of bytes written, 0 if nothing could be built (no usable
 * id, or `out` too small). Never writes a partial frame: a truncated report is
 * one that names fewer messages than the reporter chose, and filing that
 * silently would understate what happened.
 */
size_t rnet_chat_report_build(char *out, size_t cap,
                              const char *const *mids, int mid_count,
                              const char *reason, const char *note,
                              const RNetChatReportMeta *meta);

#ifdef __cplusplus
}
#endif

#endif /* RECOMP_NET_CHAT_REPORT_H */
