#include "recomp_net/rnet_ws.h"
#include "recomp_net/rnet_sha1.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#endif

static int socket_would_block(void)
{
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int socket_interrupted(void)
{
#if defined(_WIN32)
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

static const char *B64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64_encode(const uint8_t *in, size_t n, char *out)
{
    size_t i = 0, o = 0;
    while (i < n) {
        uint32_t v = (uint32_t)in[i++] << 16;
        if (i < n) {
            v |= (uint32_t)in[i++] << 8;
        }
        if (i < n) {
            v |= (uint32_t)in[i++];
        }
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = (i > n + (n % 3 == 1 ? 1 : 0) && (n % 3 == 1)) ? '=' : B64[(v >> 6) & 63];
        out[o++] = (n % 3 == 1) ? '=' : ((n % 3 == 2 && i >= n) ? '=' : B64[v & 63]);
    }
    /* Fix padding properly */
    {
        size_t full = (n / 3) * 3;
        size_t rem = n - full;
        o = 0;
        for (i = 0; i < full; i += 3) {
            uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
            out[o++] = B64[(v >> 18) & 63];
            out[o++] = B64[(v >> 12) & 63];
            out[o++] = B64[(v >> 6) & 63];
            out[o++] = B64[v & 63];
        }
        if (rem == 1) {
            uint32_t v = (uint32_t)in[full] << 16;
            out[o++] = B64[(v >> 18) & 63];
            out[o++] = B64[(v >> 12) & 63];
            out[o++] = '=';
            out[o++] = '=';
        } else if (rem == 2) {
            uint32_t v = ((uint32_t)in[full] << 16) | ((uint32_t)in[full + 1] << 8);
            out[o++] = B64[(v >> 18) & 63];
            out[o++] = B64[(v >> 12) & 63];
            out[o++] = B64[(v >> 6) & 63];
            out[o++] = '=';
        }
        out[o] = '\0';
    }
}

int rnet_ws_accept_key(const char *client_key, char out_b64[32])
{
    char concat[128];
    uint8_t digest[20];
    int n;
    if (!client_key || !out_b64) {
        return -1;
    }
    n = snprintf(concat, sizeof(concat), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", client_key);
    if (n <= 0 || (size_t)n >= sizeof(concat)) {
        return -1;
    }
    rnet_sha1((const uint8_t *)concat, (size_t)n, digest);
    b64_encode(digest, 20, out_b64);
    return 0;
}

#if defined(MSG_NOSIGNAL)
#define RNET_WS_SEND_FLAGS MSG_NOSIGNAL
#else
#define RNET_WS_SEND_FLAGS 0
#endif

static int send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
#if defined(_WIN32)
        int n = send(fd, p + sent, (int)(len - sent), 0);
#else
        ssize_t n = send(fd, p + sent, len - sent, RNET_WS_SEND_FLAGS);
#endif
        if (n < 0) {
            if (socket_interrupted()) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int rnet_ws_frame_text(const char *text, int client_mask, uint8_t *out, size_t out_cap)
{
    size_t len;
    size_t hlen = 0;
    size_t i;
    uint8_t mask[4] = {0, 0, 0, 0};

    if (!text || !out) {
        return -1;
    }
    len = strlen(text);
    if (len >= 65536) {
        return -1;
    }
    hlen = (len < 126 ? 2u : 4u) + (client_mask ? 4u : 0u);
    if (out_cap < hlen + len || hlen + len > 0x7fffffffu) {
        return -1;
    }
    out[0] = 0x81; /* FIN + text */
    if (len < 126) {
        out[1] = (uint8_t)((client_mask ? 0x80 : 0) | len);
    } else {
        out[1] = (uint8_t)((client_mask ? 0x80 : 0) | 126);
        out[2] = (uint8_t)((len >> 8) & 0xff);
        out[3] = (uint8_t)(len & 0xff);
    }
    if (client_mask) {
        uint32_t r = (uint32_t)rand();
        mask[0] = (uint8_t)(r);
        mask[1] = (uint8_t)(r >> 8);
        mask[2] = (uint8_t)(r >> 16);
        mask[3] = (uint8_t)(r >> 24);
        memcpy(out + hlen - 4, mask, 4);
    }
    for (i = 0; i < len; ++i) {
        out[hlen + i] = (uint8_t)text[i] ^ mask[i & 3];
    }
    return (int)(hlen + len);
}

int rnet_ws_write_text(int fd, const char *text, int client_mask)
{
    uint8_t stack[2048];
    uint8_t *frame = stack;
    size_t need;
    int n, rc;

    if (!text) {
        return -1;
    }
    need = strlen(text) + 8;
    if (need > sizeof(stack)) {
        frame = (uint8_t *)malloc(need);
        if (!frame) {
            return -1;
        }
    }
    n = rnet_ws_frame_text(text, client_mask, frame, need);
    rc = n < 0 ? -1 : send_all(fd, frame, (size_t)n);
    if (frame != stack) {
        free(frame);
    }
    return rc;
}

/* ── RNetWsTx: frame-atomic outbound buffer ─────────────────────────── */

static size_t tx_cap(const RNetWsTx *tx)
{
    return tx->cap ? tx->cap : RNET_WS_TX_CAP_DEFAULT;
}

size_t rnet_ws_tx_pending(const RNetWsTx *tx)
{
    return tx ? tx->len - tx->off : 0u;
}

void rnet_ws_tx_free(RNetWsTx *tx)
{
    size_t cap;
    if (!tx) {
        return;
    }
    cap = tx->cap;
    free(tx->buf);
    memset(tx, 0, sizeof(*tx));
    tx->cap = cap;
}

int rnet_ws_tx_queue_text(RNetWsTx *tx, const char *text, int client_mask)
{
    size_t len, frame_max, pending;
    int n;

    if (!tx || !text) {
        return -1;
    }
    len = strlen(text);
    if (len >= 65536) {
        return -1;
    }
    frame_max = len + 8u;
    pending = tx->len - tx->off;
    if (pending + frame_max > tx_cap(tx)) {
        return -2;
    }
    /* Drop the frames that have gone whole before growing (keep the one
     * that is part-sent, so frame boundaries stay walkable from buf[0]). */
    if (tx->frame_at > 0) {
        memmove(tx->buf, tx->buf + tx->frame_at, tx->len - tx->frame_at);
        tx->len -= tx->frame_at;
        tx->off -= tx->frame_at;
        tx->frame_at = 0;
    }
    if (tx->len + frame_max > tx->alloc) {
        size_t want = tx->alloc ? tx->alloc : 4096u;
        uint8_t *nb;
        while (want < tx->len + frame_max) {
            want *= 2u;
        }
        nb = (uint8_t *)realloc(tx->buf, want);
        if (!nb) {
            return -3;
        }
        tx->buf = nb;
        tx->alloc = want;
    }
    n = rnet_ws_frame_text(text, client_mask, tx->buf + tx->len, tx->alloc - tx->len);
    if (n < 0) {
        return -1;
    }
    tx->len += (size_t)n;
    tx->frames_queued++;
    if (tx->len - tx->off > tx->high_water) {
        tx->high_water = tx->len - tx->off;
    }
    return 0;
}

/* Length of the whole frame starting at buf[at] (our own encoding). */
static size_t frame_len_at(const uint8_t *buf, size_t at)
{
    size_t plen = buf[at + 1] & 0x7fu;
    size_t h = 2;
    if (plen == 126) {
        plen = ((size_t)buf[at + 2] << 8) | buf[at + 3];
        h = 4;
    }
    if (buf[at + 1] & 0x80u) {
        h += 4;
    }
    return h + plen;
}

long rnet_ws_tx_flush(RNetWsTx *tx, int fd)
{
    if (!tx) {
        return -1;
    }
    while (tx->off < tx->len) {
#if defined(_WIN32)
        int n = send(fd, (const char *)tx->buf + tx->off, (int)(tx->len - tx->off), 0);
#else
        ssize_t n = send(fd, tx->buf + tx->off, tx->len - tx->off, RNET_WS_SEND_FLAGS);
#endif
        if (n < 0) {
            if (socket_interrupted()) {
                continue;
            }
            if (socket_would_block()) {
                tx->would_blocks++;
                if (tx->off != tx->frame_at) {
                    tx->split_frames++;
                }
                return (long)(tx->len - tx->off);
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        tx->off += (size_t)n;
        while (tx->frame_at < tx->off &&
               tx->frame_at + frame_len_at(tx->buf, tx->frame_at) <= tx->off) {
            tx->frame_at += frame_len_at(tx->buf, tx->frame_at);
        }
    }
    tx->len = 0;
    tx->off = 0;
    tx->frame_at = 0;
    return 0;
}
