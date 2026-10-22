/*
 * The lobby WebSocket writer under would-block (snesrecomp#104).
 *
 * The lobby socket is non-blocking. The writer used to treat "socket full"
 * as failure after part of a frame had already gone, and its callers ignored
 * that and wrote the next frame straight after the fragment: the server then
 * parsed payload bytes as frame headers. The fix is RNetWsTx, a frame-atomic
 * outbound buffer the client flushes from its pump.
 *
 * Every case runs over an AF_UNIX socketpair whose buffers are shrunk to the
 * kernel minimum, so the writer really does hit EAGAIN, and really does hit
 * it inside a frame (the cases assert both -- a run that never split a frame
 * proves nothing). The reader then parses the byte stream as a server would
 * and checks it is EXACTLY the queued frame sequence.
 *
 *   1. legacy_write_corrupts: the old path (rnet_ws_write_text, return
 *      ignored, as flush_pending did) on the same socket -- the stream fails
 *      the check. This is the bug, reproduced.
 *   2. tx_frames_arrive_intact: RNetWsTx, frames of 1..12000 bytes, reader
 *      draining in odd-sized chunks between flushes.
 *   3. tx_cap_refuses_whole: a backlog past the cap refuses the frame whole
 *      and leaves the stream valid.
 *   4. client_signal_burst: the real lobby client (its translation unit is
 *      included, like lobby_client_test) sends a burst of ICE signals through
 *      rnet_lobby_send_signal_to, the path the issue names, while the "server"
 *      is not reading; rnet_lobby_pump finishes it. The client stays
 *      connected and every signal arrives intact and in order.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../src/lobby/rnet_lobby_client.c"

/* The client TU reaches for these; no case here opens a UDP port or an ICE
 * agent, so each aborts rather than pretending (see lobby_client_test). */
int rnet_udp_find_free_port(int preferred, int span)
{
    (void)preferred; (void)span;
    fprintf(stderr, "lobby_ws_backlog_test: rnet_udp_find_free_port called\n");
    abort();
}
#define XFER_TRAP(name) \
    do { fprintf(stderr, "lobby_ws_backlog_test: %s called\n", name); abort(); } while (0)
int  rnet_ice_xfer_open(RNetIceXfer **o, const RNetIceConfig *c,
                        RNetIceXferSignalEmitFn e, void *u)
{ (void)o; (void)c; (void)e; (void)u; XFER_TRAP("rnet_ice_xfer_open"); }
void rnet_ice_xfer_close(RNetIceXfer **x) { (void)x; XFER_TRAP("close"); }
void rnet_ice_xfer_push_signal(RNetIceXfer *x, const RNetSignal *m)
{ (void)x; (void)m; XFER_TRAP("push_signal"); }
void rnet_ice_xfer_pump(RNetIceXfer *x) { (void)x; XFER_TRAP("pump"); }
int  rnet_ice_xfer_queue_blob(RNetIceXfer *x, uint8_t *d, size_t l)
{ (void)x; (void)d; (void)l; XFER_TRAP("queue_blob"); }
int  rnet_ice_xfer_send_idle(const RNetIceXfer *x) { (void)x; XFER_TRAP("send_idle"); }
int  rnet_ice_xfer_take_blob(RNetIceXfer *x, uint8_t **d, size_t *l)
{ (void)x; (void)d; (void)l; XFER_TRAP("take_blob"); }
int  rnet_ice_xfer_progress(const RNetIceXfer *x) { (void)x; XFER_TRAP("progress"); }
int  rnet_ice_xfer_failed(const RNetIceXfer *x, char *e, size_t c)
{ (void)x; (void)e; (void)c; XFER_TRAP("failed"); }
void rnet_ice_xfer_path(const RNetIceXfer *x, char *o, size_t c)
{ (void)x; (void)o; (void)c; XFER_TRAP("path"); }
RNetIceState rnet_ice_xfer_state(const RNetIceXfer *x) { (void)x; XFER_TRAP("state"); }
const char *rnet_ice_state_name(RNetIceState st) { (void)st; XFER_TRAP("state_name"); }
const char *rnet_account_session(void) { XFER_TRAP("rnet_account_session"); }

static int fails;

static void ck(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        fails++;
    }
}

/* ── the "server": a byte sink that parses frames ───────────────────── */

typedef struct Sink {
    uint8_t *buf;
    size_t len, alloc;
} Sink;

static void sink_put(Sink *k, const uint8_t *p, size_t n)
{
    if (k->len + n > k->alloc) {
        k->alloc = (k->len + n) * 2 + 4096;
        k->buf = (uint8_t *)realloc(k->buf, k->alloc);
    }
    memcpy(k->buf + k->len, p, n);
    k->len += n;
}

/* Read up to `max` bytes (0 = everything available) in `chunk`-byte reads. */
static size_t sink_read(Sink *k, int fd, size_t chunk, size_t max)
{
    uint8_t tmp[65536];
    size_t got = 0;
    if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
    for (;;) {
        size_t want = chunk;
        ssize_t n;
        if (max && got + want > max) want = max - got;
        if (want == 0) break;
        n = recv(fd, tmp, want, MSG_DONTWAIT);
        if (n <= 0) break;
        sink_put(k, tmp, (size_t)n);
        got += (size_t)n;
    }
    return got;
}

/* Parse the stream as RFC 6455 client->server text frames and compare with
 * the expected payloads. Returns the number of frames that matched, in
 * order, before the first difference; *clean = 1 when the whole stream is
 * exactly the expected sequence. */
static int sink_check(const Sink *k, char **want, int n_want, int *clean)
{
    size_t at = 0;
    int i = 0;
    *clean = 0;
    while (at < k->len) {
        size_t plen, h, j;
        const uint8_t *m;
        if (k->len - at < 2) return i;
        if (k->buf[at] != 0x81 || !(k->buf[at + 1] & 0x80)) return i;
        plen = k->buf[at + 1] & 0x7f;
        h = 2;
        if (plen == 126) {
            if (k->len - at < 4) return i;
            plen = ((size_t)k->buf[at + 2] << 8) | k->buf[at + 3];
            h = 4;
        } else if (plen == 127) {
            return i;
        }
        if (k->len - at < h + 4 + plen) return i;
        m = k->buf + at + h;
        if (i >= n_want || strlen(want[i]) != plen) return i;
        for (j = 0; j < plen; ++j)
            if ((char)(m[4 + j] ^ m[j & 3]) != want[i][j]) return i;
        at += h + 4 + plen;
        i++;
    }
    *clean = (i == n_want);
    return i;
}

/* Deterministic payload of `len` bytes that names its sequence number, so a
 * shifted or truncated frame can never compare equal to its neighbour. */
static char *payload(int seq, size_t len)
{
    char *p = (char *)malloc(len + 1);
    size_t i;
    int head = snprintf(p, len + 1, "#%d:", seq);
    for (i = (size_t)(head > 0 ? head : 0); i < len; ++i)
        p[i] = (char)('a' + (char)((seq * 7 + (int)i) % 26));
    p[len] = '\0';
    return p;
}

static size_t size_of(int seq)
{
    /* 1..12000 bytes: tiny, 125/126 boundary, 16-bit lengths. */
    static const size_t sizes[] = {1, 40, 125, 126, 127, 700, 3000, 12000, 5555, 90};
    return sizes[seq % 10];
}

static void tiny_pair(int sv[2])
{
    int small = 1; /* the kernel rounds up to its minimum */
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    ck(rc == 0, "socketpair");
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
    setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));
    fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL, 0) | O_NONBLOCK);
}

#define N_FRAMES 240

static void legacy_write_corrupts(void)
{
    int sv[2], i, clean = 0, matched, failed_writes = 0;
    char *want[N_FRAMES];
    Sink k = {0};

    tiny_pair(sv);
    for (i = 0; i < N_FRAMES; ++i) {
        want[i] = payload(i, size_of(i));
        /* What flush_pending did: write, ignore the result, write the next. */
        if (rnet_ws_write_text(sv[0], want[i], 1) < 0) failed_writes++;
        if (i % 3 == 0) sink_read(&k, sv[1], 997, 4000);
    }
    sink_read(&k, sv[1], 4096, 0);
    matched = sink_check(&k, want, N_FRAMES, &clean);
    printf("legacy: %d writes failed on would-block; %d/%d frames parsed before the "
           "stream broke\n", failed_writes, matched, N_FRAMES);
    ck(failed_writes > 0, "legacy: the socket filled (else this proves nothing)");
    ck(!clean, "legacy: ignoring a would-block corrupts the stream (the bug)");
    for (i = 0; i < N_FRAMES; ++i) free(want[i]);
    free(k.buf);
    close(sv[0]);
    close(sv[1]);
}

static void tx_frames_arrive_intact(void)
{
    int sv[2], i, clean = 0, matched, errors = 0, rounds = 0;
    char *want[N_FRAMES];
    RNetWsTx tx;
    Sink k = {0};

    memset(&tx, 0, sizeof(tx));
    tx.cap = 4u * 1024u * 1024u;
    tiny_pair(sv);
    for (i = 0; i < N_FRAMES; ++i) {
        want[i] = payload(i, size_of(i));
        ck(rnet_ws_tx_queue_text(&tx, want[i], 1) == 0, "tx: frame queued");
        if (rnet_ws_tx_flush(&tx, sv[0]) < 0) errors++;
        /* The reader lags and reads odd sizes, so the writer stops at
         * arbitrary byte offsets. */
        if (i % 3 == 0) sink_read(&k, sv[1], 997, 4000);
    }
    while (rnet_ws_tx_pending(&tx) > 0 && rounds++ < 100000) {
        if (rnet_ws_tx_flush(&tx, sv[0]) < 0) errors++;
        sink_read(&k, sv[1], 1531, 0);
    }
    sink_read(&k, sv[1], 4096, 0);
    matched = sink_check(&k, want, N_FRAMES, &clean);
    printf("tx: %llu would-blocks, %llu inside a frame, high water %zu bytes; "
           "%d/%d frames intact\n", (unsigned long long)tx.would_blocks,
           (unsigned long long)tx.split_frames, tx.high_water, matched, N_FRAMES);
    ck(tx.split_frames > 0, "tx: would-block hit inside a frame (else this proves nothing)");
    ck(errors == 0, "tx: would-block is never a hard error");
    ck(rnet_ws_tx_pending(&tx) == 0, "tx: the backlog drained");
    ck(clean, "tx: the stream is exactly the queued frames, in order");
    rnet_ws_tx_free(&tx);
    for (i = 0; i < N_FRAMES; ++i) free(want[i]);
    free(k.buf);
    close(sv[0]);
    close(sv[1]);
}

static void tx_cap_refuses_whole(void)
{
    int sv[2], i, n_ok = 0, refused = 0, clean = 0;
    char *want[64];
    RNetWsTx tx;
    Sink k = {0};

    memset(&tx, 0, sizeof(tx));
    tx.cap = 64u * 1024u;
    tiny_pair(sv);
    for (i = 0; i < 64; ++i) {
        char *p = payload(i, 12000);
        int rc = rnet_ws_tx_queue_text(&tx, p, 1);
        if (rc == 0) {
            want[n_ok++] = p;
            (void)rnet_ws_tx_flush(&tx, sv[0]);
        } else {
            ck(rc == -2, "cap: a refusal is the backlog cap");
            refused++;
            free(p);
        }
        ck(rnet_ws_tx_pending(&tx) <= tx.cap, "cap: backlog never passes the cap");
    }
    ck(refused > 0, "cap: a reader that never reads hits the cap");
    while (rnet_ws_tx_pending(&tx) > 0) {
        ck(rnet_ws_tx_flush(&tx, sv[0]) >= 0, "cap: flush after refusal");
        sink_read(&k, sv[1], 4096, 0);
    }
    sink_read(&k, sv[1], 4096, 0);
    (void)sink_check(&k, want, n_ok, &clean);
    ck(clean, "cap: a refused frame left no fragment; the stream stays valid");
    rnet_ws_tx_free(&tx);
    for (i = 0; i < n_ok; ++i) free(want[i]);
    free(k.buf);
    close(sv[0]);
    close(sv[1]);
}

static void client_signal_burst(void)
{
    enum { N = 160 };
    int sv[2], i, clean = 0, matched, rounds = 0, rc_bad = 0;
    char *want[N];
    char *texts[N];
    Sink k = {0};

    tiny_pair(sv);
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = sv[0];
    g_lc.connected = 1;
    g_lc.handshake_done = 1;
    g_lc.in_lobby = 1;
    g_lc.is_host = 1;   /* a guest would add RTT pings to the stream */
    g_srv_ping_wanted = 0; /* and so would the per-connection server ping */
    snprintf(g_lc.join.lobby_id, sizeof(g_lc.join.lobby_id), "L1");
    for (i = 0; i < N; ++i) {
        char json[4608];
        texts[i] = payload(i, 1 + (size_t)(i * 37) % 3000);   /* an SDP-sized burst */
        snprintf(json, sizeof(json),
                 "{\"op\":\"signal\",\"lobby_id\":\"L1\",\"to_player_id\":\"\","
                 "\"type\":%d,\"flag\":%d,\"text\":\"%s\"}", i % 6, i & 1, texts[i]);
        want[i] = strdup(json);
        if (rnet_lobby_send_signal_to("", i % 6, i & 1, texts[i]) != 0) rc_bad++;
        if (i % 5 == 0) sink_read(&k, sv[1], 997, 3000);
    }
    ck(rc_bad == 0, "client: every signal accepted (would-block is not failure)");
    ck(rnet_lobby_connected(), "client: still connected after the burst");
    ck(g_lc.tx.split_frames > 0, "client: the burst hit would-block inside a frame");
    while (rnet_ws_tx_pending(&g_lc.tx) > 0 && rounds++ < 100000) {
        rnet_lobby_pump();
        sink_read(&k, sv[1], 1531, 0);
        if (!rnet_lobby_connected()) break;
    }
    sink_read(&k, sv[1], 4096, 0);
    matched = sink_check(&k, want, N, &clean);
    printf("client: %llu would-blocks, %llu inside a frame; %d/%d signals intact\n",
           (unsigned long long)g_lc.tx.would_blocks,
           (unsigned long long)g_lc.tx.split_frames, matched, N);
    ck(rnet_lobby_connected(), "client: the pump finished the backlog and stayed connected");
    ck(clean, "client: every signal arrived intact and in order");
    for (i = 0; i < N; ++i) {
        free(want[i]);
        free(texts[i]);
    }
    free(k.buf);
    rnet_lobby_disconnect();   /* closes sv[0] */
    close(sv[1]);
}

int main(void)
{
    legacy_write_corrupts();
    tx_frames_arrive_intact();
    tx_cap_refuses_whole();
    client_signal_burst();
    if (fails) {
        fprintf(stderr, "lobby_ws_backlog_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("lobby_ws_backlog_test: ok\n");
    return 0;
}
