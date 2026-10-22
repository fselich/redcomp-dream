/* rnet_auth.c -- see recomp_net/auth.h.
 *
 * All network work happens on one worker thread; the launcher only ever reads
 * flags this file publishes. That is the same shape the lobby client already
 * uses for its off-thread connect, and it is why a login cannot stall a frame.
 */

#include "recomp_net/auth.h"
#include "rnet_open_url.h"
#include "rnet_sha256.h"
#include "../platform/rnet_platform.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <windows.h>
#define close closesocket
typedef HANDLE auth_thread_t;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
typedef pthread_t auth_thread_t;
#endif

#define SECRET_MAX 96
#define SESSION_MAX 1024
#define NAME_MAX_LEN 64
#define ID_MAX 64

/* What the worker is doing. The launcher polls rnet_account_state(). */
enum { JOB_NONE = 0, JOB_LOGIN, JOB_SESSION, JOB_SET_HANDLE, JOB_SIGN_OUT };

static struct {
    char host[192];
    int  port;

    /* Published to the launcher. Written by the worker, read on the UI
     * thread; single-writer plus aligned word stores, which is what the
     * lobby client already relies on for its connect flags. */
    volatile int state;       /* RNET_ACCOUNT_* */
    /* 0 = not known to be unavailable, i.e. offer sign-in and let the first
     * /auth/discord/start settle it; 1 = the server answered 503 and does not
     * do logins at all.
     *
     * Phrased negatively ON PURPOSE. This was `available` with -1 meaning
     * "unknown", which a zero-initialised static made 0 -- "no logins here" --
     * until rnet_account_init ran. Init runs from the netplay pump, which the
     * launcher only reaches on the lobby browser page, so the very first visit
     * to the chooser saw "no sign-in" and skipped past it; backing out and
     * re-entering worked, because by then the browser page had pumped. Zero
     * now means the right thing without anyone having to initialise it. */
    volatile int unavailable;
    /* Set to abandon an in-flight login so a Retry can start a fresh one. */
    volatile int cancel;
    volatile int busy;

    char secret[SECRET_MAX];
    char session[SESSION_MAX];
    char player_id[ID_MAX];
    char handle[NAME_MAX_LEN];
    char username[NAME_MAX_LEN];
    char error[192];

    /* Login in flight. */
    char pair_code[128];
    char pending_handle[NAME_MAX_LEN];

    int job;
    int thread_valid;
    auth_thread_t thread;
    int initialized;
    int tried_stored_key;
} g;

/* ---- small helpers ------------------------------------------------------ */

static void set_err(const char *e) {
    snprintf(g.error, sizeof(g.error), "%s", e ? e : "");
}

/* Minimal extraction for the flat, server-generated JSON these endpoints
 * return. Not a parser: it is the same shape psx_lobby_client.c already uses,
 * and the bodies come from our own server. */
static int json_str(const char *json, const char *key, char *out, size_t cap) {
    char pat[64];
    const char *p;
    size_t o = 0;
    if (!json || !out || cap == 0) return 0;
    out[0] = '\0';
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) return 0;
    p = strchr(p + strlen(pat), ':');
    if (!p) return 0;
    ++p;
    while (*p && isspace((unsigned char)*p)) ++p;
    if (*p != '"') return 0;
    ++p;
    while (*p && *p != '"' && o + 1 < cap) {
        if (*p == '\\' && p[1]) {
            ++p;
            switch (*p) {
            case 'n': out[o++] = '\n'; break;
            case 't': out[o++] = '\t'; break;
            case 'r': out[o++] = '\r'; break;
            default:  out[o++] = *p;   break;
            }
            ++p;
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
    return o > 0;
}

/* Escape for a JSON string body. Same rules as the lobby client's escaper:
 * quote and backslash escaped, control characters dropped. */
static void json_esc(const char *in, char *out, size_t cap) {
    size_t o = 0;
    if (!out || cap == 0) return;
    while (in && *in && o + 2 < cap) {
        unsigned char c = (unsigned char)*in++;
        if (c == '"' || c == '\\') {
            if (o + 3 >= cap) break;
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c >= 0x20) {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

static void hex_encode(const unsigned char *in, size_t n, char *out) {
    static const char H[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; ++i) {
        out[i * 2]     = H[in[i] >> 4];
        out[i * 2 + 1] = H[in[i] & 15];
    }
    out[n * 2] = '\0';
}

/* HMAC-SHA256, keyed by the hex verifier string, over the nonce. The server
 * computes the identical thing (secrets::expected_proof). */
static void hmac_sha256_hex(const char *key, const char *msg, char out_hex[65]) {
    unsigned char k[64], inner[32], outer[32], pad[64];
    unsigned char tmp[96];
    rnet_sha256_ctx c;
    size_t klen = strlen(key), i;

    memset(k, 0, sizeof(k));
    if (klen > 64) {
        rnet_sha256_compute((const unsigned char *)key, klen, k);
    } else {
        memcpy(k, key, klen);
    }

    for (i = 0; i < 64; ++i) pad[i] = k[i] ^ 0x36;
    rnet_sha256_init(&c);
    rnet_sha256_update(&c, pad, 64);
    rnet_sha256_update(&c, (const unsigned char *)msg, strlen(msg));
    rnet_sha256_final(&c, inner);

    for (i = 0; i < 64; ++i) pad[i] = k[i] ^ 0x5c;
    memcpy(tmp, pad, 64);
    memcpy(tmp + 64, inner, 32);
    rnet_sha256_compute(tmp, 96, outer);
    hex_encode(outer, 32, out_hex);
}

/* ---- the secret file ---------------------------------------------------- */

/* Absolute path set by the host via rnet_account_set_secret_path(). Empty
 * means "not configured", and the legacy CWD-relative name is used. */
static char g_secret_path[512];

void rnet_account_set_secret_path(const char *path) {
    if (!path || !path[0]) { g_secret_path[0] = '\0'; return; }
    snprintf(g_secret_path, sizeof(g_secret_path), "%s", path);
}

#define SECRET_LEGACY_NAME "netplay_secret"

static void secret_path(char *out, size_t cap) {
    /* The host's configured location if it gave one. Otherwise the legacy
     * bare name -- which resolves against the CWD, not the executable
     * directory, and is why a build could appear to forget its login after a
     * rebuild that ran from somewhere else. */
    if (g_secret_path[0]) {
        snprintf(out, cap, "%s", g_secret_path);
        return;
    }
    snprintf(out, cap, "%s", SECRET_LEGACY_NAME);
}

static void secret_store(const char *player_id, const char *secret);

static void secret_load(void) {
    char path[512];
    FILE *f;
    size_t n;
    int from_legacy = 0;
    secret_path(path, sizeof(path));
    g.secret[0] = '\0';
    g.player_id[0] = '\0';
    f = fopen(path, "rb");
    if (!f && g_secret_path[0]) {
        /* Configured path is empty but the old CWD-relative file may still be
         * sitting where a previous run left it. Read it rather than making the
         * player sign in again just because the path moved. */
        f = fopen(SECRET_LEGACY_NAME, "rb");
        from_legacy = (f != NULL);
    }
    if (!f) return;
    /* Read the WHOLE LINE into its own buffer, not into g.secret.
     *
     * The line is "<player_id> <secret>\n" -- for a UUID id and a 68-char key
     * that is 106 bytes -- and it was read into g.secret, which is SECRET_MAX
     * (96). fread capped at 95, so the secret was silently truncated (68 chars
     * to 58) before it was ever split out. The proof computed from it was
     * wrong, the server answered 401, and the key was then discarded as
     * invalid.
     *
     * That is why a sign-in never survived a relaunch: job_login stores the
     * key straight from the login reply, so signing IN always worked, and only
     * the next launch -- the one that has to read the file back -- failed.
     * The buffer has to hold both fields and the separator, not just whichever
     * one is larger. */
    {
        char line[ID_MAX + SECRET_MAX + 8];
        char *sp, *nl;
        n = fread(line, 1, sizeof(line) - 1, f);
        line[n] = '\0';
        fclose(f);
        nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        sp = strchr(line, ' ');
        if (!sp) return;            /* malformed: no id/secret separator */
        *sp = '\0';
        snprintf(g.player_id, sizeof(g.player_id), "%s", line);
        snprintf(g.secret, sizeof(g.secret), "%s", sp + 1);
    }
    /* Migrate forward so the next run finds it in the stable place. The old
     * file is deliberately left alone: it is a credential, and deleting one
     * on the strength of a write we have not confirmed is the wrong order. */
    if (from_legacy && g.secret[0])
        secret_store(g.player_id, g.secret);
}

static void secret_store(const char *player_id, const char *secret) {
    char path[512];
    FILE *f;
    secret_path(path, sizeof(path));
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "%s %s\n", player_id ? player_id : "", secret ? secret : "");
    fclose(f);
#if !defined(_WIN32)
    /* A password in a file: keep it to the owner. */
    (void)chmod(path, S_IRUSR | S_IWUSR);
#endif
    snprintf(g.secret, sizeof(g.secret), "%s", secret ? secret : "");
    snprintf(g.player_id, sizeof(g.player_id), "%s", player_id ? player_id : "");
}

static void secret_forget(void) {
    char path[512];
    secret_path(path, sizeof(path));
    remove(path);
    g.secret[0] = '\0';
    g.player_id[0] = '\0';
}

/* ---- blocking HTTP POST (worker thread only) ---------------------------- */

/* Returns the HTTP status, or <0 on a transport failure. Body is copied into
 * `body` (truncated to fit), which is enough for these small JSON replies. */
static int http_post(const char *path, const char *json, char *body, size_t cap) {
    struct addrinfo hints, *res = NULL, *ai;
    char portstr[16], req[2048];
    char buf[4096];
    int fd = -1, status = -1;
    size_t total = 0;
    const char *hdr_end;

    if (body && cap) body[0] = '\0';
    /* Sign-in can precede any lobby, LAN or debug socket. Initialize the
     * shared transport here so first-use HTTP also works on Windows. */
    rnet_os_startup();
    snprintf(portstr, sizeof(portstr), "%d", g.port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(g.host, portstr, &hints, &res) != 0 || !res) return -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = (int)socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    /* Bound the exchange. There was no timeout at all, so a server that
     * accepted the connection and then said nothing parked this worker
     * thread forever -- and with it any hope of the launcher ever answering
     * "am I signed in?". Retries are worthless if one attempt can hang. */
    if (fd >= 0) {
#if defined(_WIN32)
        DWORD tv = 5000;
#else
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
#endif
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
    }
    freeaddrinfo(res);
    if (fd < 0) return -2;

    snprintf(req, sizeof(req),
             "POST %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n\r\n%s",
             path, g.host, g.port, (int)strlen(json), json);
    if (send(fd, req, (int)strlen(req), 0) < 0) {
        close(fd);
        return -3;
    }

    for (;;) {
        int n = (int)recv(fd, buf + total, (int)(sizeof(buf) - 1 - total), 0);
        if (n <= 0) break;
        total += (size_t)n;
        if (total >= sizeof(buf) - 1) break;
    }
    close(fd);
    buf[total] = '\0';
    if (total < 12) return -4;
    status = atoi(buf + 9); /* "HTTP/1.1 NNN ..." */
    hdr_end = strstr(buf, "\r\n\r\n");
    if (hdr_end && body && cap) {
        snprintf(body, cap, "%s", hdr_end + 4);
    }
    return status;
}

/* ---- one challenge/proof round ------------------------------------------ */

/*
 * Outcome of one challenge/proof round.
 *
 * The distinction is load-bearing: the caller DELETES the stored key on a
 * rejection, so "could not reach the server" must never be reported as
 * "this key is no longer valid". Conflating them meant a network blip at
 * launch destroyed a working credential and made the player sign in again.
 */
enum { SESSION_OK = 0, SESSION_REJECTED, SESSION_UNREACHABLE };

/* Only an authoritative "we do not know this key" is a rejection. A transport
 * failure (negative), a server fault (5xx), or a malformed reply are all
 * "ask again later" -- the key may be perfectly good. */
static int session_verdict_from_status(int st) {
    if (st == 401 || st == 403 || st == 404) return SESSION_REJECTED;
    return SESSION_UNREACHABLE;
}

/* Fills `session` and the display fields. Returns a SESSION_* code. The key
 * never leaves this function: only the HMAC does. */
static int prove_and_get_session(void) {
    char body[2048], reqbuf[512], nonce[128], verifier_hex[65], proof[65];
    unsigned char digest[32];
    char id_esc[128];
    int st;

    if (!g.secret[0] || !g.player_id[0]) return SESSION_REJECTED;

    json_esc(g.player_id, id_esc, sizeof(id_esc));
    snprintf(reqbuf, sizeof(reqbuf), "{\"player_id\":\"%s\"}", id_esc);
    st = http_post("/auth/challenge", reqbuf, body, sizeof(body));
    if (st != 200) return session_verdict_from_status(st);
    if (!json_str(body, "nonce", nonce, sizeof(nonce))) return SESSION_UNREACHABLE;

    rnet_sha256_compute((const unsigned char *)g.secret, strlen(g.secret), digest);
    hex_encode(digest, 32, verifier_hex);
    hmac_sha256_hex(verifier_hex, nonce, proof);

    snprintf(reqbuf, sizeof(reqbuf),
             "{\"player_id\":\"%s\",\"nonce\":\"%s\",\"proof\":\"%s\"}",
             id_esc, nonce, proof);
    st = http_post("/auth/session", reqbuf, body, sizeof(body));
    if (st != 200) return session_verdict_from_status(st);
    json_str(body, "session", g.session, sizeof(g.session));
    json_str(body, "handle", g.handle, sizeof(g.handle));
    json_str(body, "discord_username", g.username, sizeof(g.username));
    /* 200 with no session is the server misbehaving, not the key being bad. */
    return g.session[0] ? SESSION_OK : SESSION_UNREACHABLE;
}

static void auth_sleep_ms(int ms) {
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/* ---- worker ------------------------------------------------------------- */

static void job_login(void) {
    char body[2048], url[1024];
    int st, tries;

    st = http_post("/auth/discord/start", "{}", body, sizeof(body));
    if (st == 503) {
        g.unavailable = 1;
        set_err("This lobby server does not offer Discord sign-in.");
        g.state = RNET_ACCOUNT_FAILED;
        return;
    }
    if (st != 200 || !json_str(body, "url", url, sizeof(url)) ||
        !json_str(body, "code", g.pair_code, sizeof(g.pair_code))) {
        set_err("Could not reach the lobby server to start the sign-in.");
        g.state = RNET_ACCOUNT_FAILED;
        return;
    }
    g.unavailable = 0;
    if (!rnet_open_url(url)) {
        set_err("Could not launch a browser. Check your default browser "
                "setup, then press Retry.");
        g.state = RNET_ACCOUNT_FAILED;
        return;
    }

    /* Poll until the server has an answer. The pairing code expires server
     * side after ten minutes; stop a little before that rather than spinning
     * forever if the player closed the tab. */
    /* Poll for as long as a person plausibly needs to find their password
     * manager and click Authorize. The UI offers Retry long before this, but
     * it keeps polling underneath -- aborting after a few seconds would make
     * signing in impossible, not more responsive. */
    for (tries = 0; tries < 150 && !g.cancel; ++tries) {
        char req[256];
        char code_esc[160];
        json_esc(g.pair_code, code_esc, sizeof(code_esc));
        snprintf(req, sizeof(req), "{\"code\":\"%s\"}", code_esc);
        st = http_post("/auth/discord/poll", req, body, sizeof(body));
        if (st == 200) {
            char secret[SECRET_MAX], pid[ID_MAX];
            json_str(body, "session", g.session, sizeof(g.session));
            json_str(body, "handle", g.handle, sizeof(g.handle));
            json_str(body, "discord_username", g.username, sizeof(g.username));
            if (json_str(body, "netplay_secret", secret, sizeof(secret)) &&
                json_str(body, "player_id", pid, sizeof(pid))) {
                /* Returned exactly once. Store it now or it is gone. */
                secret_store(pid, secret);
            }
            g.state = RNET_ACCOUNT_SIGNED_IN;
            return;
        }
        if (st == 202) {
            /* Sleep in slices so a Retry is noticed in well under a second
             * rather than after the poll interval. */
            int slice;
            for (slice = 0; slice < 8 && !g.cancel; ++slice) auth_sleep_ms(250);
            continue;
        }
        /* Anything else is final. Say what it was: a login that dies with a
         * generic message is the hardest kind to report. */
        fprintf(stderr, "rnet_account: poll ended with HTTP %d %s\n", st, body);
        if (st == 403 && strstr(body, "expired")) {
            set_err("That sign-in expired before it finished. "
                    "Press Retry to start a new one.");
        } else if (st < 0) {
            set_err("Lost contact with the lobby server during sign-in. "
                    "Press Retry.");
        } else {
            set_err("Discord sign-in did not complete. Press Retry.");
        }
        g.state = RNET_ACCOUNT_FAILED;
        return;
    }
    if (g.cancel) return; /* superseded by a Retry; that job owns the state */
    set_err("Discord did not answer in time. Press Retry.");
    g.state = RNET_ACCOUNT_FAILED;
}

/*
 * Redeeming the stored key, with retries for transient failures.
 *
 * One connect that happens to lose is not evidence about the credential, but
 * it used to end the attempt: the launcher fell out to the sign-in prompt and
 * asked a player with a perfectly good key to do a Discord round trip. Roughly
 * one launch in eight in practice.
 *
 * Retried ONLY for SESSION_UNREACHABLE. A rejection is authoritative -- the
 * server has looked at the key and said no -- so retrying it would be a
 * pointless round trip on every launch and, worse, would delay the honest
 * "you are signed out" answer.
 *
 * Three attempts with a short backoff. The budget is deliberately smaller than
 * the launcher's five-second sign-in gate, so the retries finish inside the
 * window where the UI is already holding ONLINE and the player sees one
 * continuous "checking" rather than a card that unlocks and then changes its
 * mind.
 */
enum { SESSION_ATTEMPTS = 3 };
static const int kSessionBackoffMs[SESSION_ATTEMPTS - 1] = { 250, 750 };

static void job_session(void) {
    int verdict = SESSION_UNREACHABLE;
    int attempt;

    for (attempt = 0; attempt < SESSION_ATTEMPTS; attempt++) {
        if (attempt > 0) {
            /* A Retry or a sign-out landing mid-backoff owns the state now. */
            if (g.cancel) return;
            auth_sleep_ms(kSessionBackoffMs[attempt - 1]);
            if (g.cancel) return;
        }
        verdict = prove_and_get_session();
        if (verdict != SESSION_UNREACHABLE) break;
    }

    if (verdict == SESSION_OK) {
        g.state = RNET_ACCOUNT_SIGNED_IN;
        return;
    }
    g.session[0] = '\0';
    if (verdict == SESSION_REJECTED) {
        /* The server authoritatively does not know this key (revoked, or a
         * different server): drop it and fall back to guest rather than
         * nagging every launch. */
        secret_forget();
        g.state = RNET_ACCOUNT_GUEST;
        return;
    }
    /* Could not get an answer, after retrying. KEEP THE KEY. It was previously
     * deleted here too, so a launch during a network blip -- or against a
     * server having a bad minute -- silently destroyed a working credential
     * and demanded a fresh Discord sign-in. */
    set_err("Could not reach the sign-in server. Your sign-in is saved; "
            "this will retry next time.");
    g.state = RNET_ACCOUNT_FAILED;
}

static void job_set_handle(void) {
    char body[1024], req[768], esc[160], id_esc[128], nonce[128];
    char verifier_hex[65], proof[65];
    unsigned char digest[32];
    int st;

    if (!g.secret[0] || !g.player_id[0]) {
        set_err("Sign in before changing your name.");
        return;
    }
    json_esc(g.player_id, id_esc, sizeof(id_esc));
    snprintf(req, sizeof(req), "{\"player_id\":\"%s\"}", id_esc);
    if (http_post("/auth/challenge", req, body, sizeof(body)) != 200 ||
        !json_str(body, "nonce", nonce, sizeof(nonce))) {
        set_err("Could not reach the lobby server.");
        return;
    }
    rnet_sha256_compute((const unsigned char *)g.secret, strlen(g.secret), digest);
    hex_encode(digest, 32, verifier_hex);
    hmac_sha256_hex(verifier_hex, nonce, proof);

    json_esc(g.pending_handle, esc, sizeof(esc));
    snprintf(req, sizeof(req),
             "{\"player_id\":\"%s\",\"nonce\":\"%s\",\"proof\":\"%s\",\"handle\":\"%s\"}",
             id_esc, nonce, proof, esc);
    st = http_post("/auth/handle", req, body, sizeof(body));
    if (st == 200) {
        json_str(body, "handle", g.handle, sizeof(g.handle));
    } else {
        set_err("That name can't be used. Please pick another one.");
    }
}

static void job_sign_out(void) {
    char body[512], req[512], id_esc[128], nonce[128], verifier_hex[65], proof[65];
    unsigned char digest[32];

    /* Best effort: tell the server to retire this device's key, then forget it
     * locally whatever happened. Signing out must work offline. */
    if (g.secret[0] && g.player_id[0]) {
        json_esc(g.player_id, id_esc, sizeof(id_esc));
        snprintf(req, sizeof(req), "{\"player_id\":\"%s\"}", id_esc);
        if (http_post("/auth/challenge", req, body, sizeof(body)) == 200 &&
            json_str(body, "nonce", nonce, sizeof(nonce))) {
            rnet_sha256_compute((const unsigned char *)g.secret, strlen(g.secret), digest);
            hex_encode(digest, 32, verifier_hex);
            hmac_sha256_hex(verifier_hex, nonce, proof);
            snprintf(req, sizeof(req),
                     "{\"player_id\":\"%s\",\"nonce\":\"%s\",\"proof\":\"%s\"}",
                     id_esc, nonce, proof);
            (void)http_post("/auth/secret/revoke", req, body, sizeof(body));
        }
    }
    secret_forget();
    g.session[0] = '\0';
    g.handle[0] = '\0';
    g.username[0] = '\0';
    g.state = RNET_ACCOUNT_GUEST;
}

#if defined(_WIN32)
static unsigned __stdcall auth_worker(void *arg) {
#else
static void *auth_worker(void *arg) {
#endif
    int job = (int)(intptr_t)arg;
    switch (job) {
    case JOB_LOGIN:      job_login();      break;
    case JOB_SESSION:    job_session();    break;
    case JOB_SET_HANDLE: job_set_handle(); break;
    case JOB_SIGN_OUT:   job_sign_out();   break;
    default: break;
    }
    g.busy = 0;
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int start_job(int job) {
    if (g.busy) return -1;
    g.busy = 1;
    g.job = job;
#if defined(_WIN32)
    {
        uintptr_t th = _beginthreadex(NULL, 0, auth_worker, (void *)(intptr_t)job, 0, NULL);
        if (!th) { g.busy = 0; return -1; }
        g.thread = (HANDLE)th;
    }
#else
    if (pthread_create(&g.thread, NULL, auth_worker, (void *)(intptr_t)job) != 0) {
        g.busy = 0;
        return -1;
    }
    pthread_detach(g.thread);
#endif
    g.thread_valid = 1;
    return 0;
}

/* ---- public surface ----------------------------------------------------- */

void rnet_account_init(const char *ws_url) {
    const char *p = ws_url;
    const char *slash;
    const char *colon;
    char hostport[192];

    memset(g.host, 0, sizeof(g.host));
    g.port = 8765;
    if (p) {
        if (strncmp(p, "ws://", 5) == 0) p += 5;
        else if (strncmp(p, "wss://", 6) == 0) p += 6;
        slash = strchr(p, '/');
        snprintf(hostport, sizeof(hostport), "%.*s",
                 slash ? (int)(slash - p) : (int)strlen(p), p);
        colon = strrchr(hostport, ':');
        if (colon && strchr(colon, ']') == NULL) {
            g.port = atoi(colon + 1);
            *(char *)colon = '\0';
        }
        snprintf(g.host, sizeof(g.host), "%s", hostport);
    }
    if (!g.initialized) {
        g.state = RNET_ACCOUNT_GUEST;
        secret_load();
        g.initialized = 1;
    }
}

void rnet_account_pump(void) {
    if (!g.initialized || g.busy) return;
    /* A stored key is redeemed once, lazily, on the first pump: the launcher
     * should come up signed in without the player doing anything. */
    if (!g.tried_stored_key && g.secret[0] && g.player_id[0]) {
        g.tried_stored_key = 1;
        g.state = RNET_ACCOUNT_WAITING;
        (void)start_job(JOB_SESSION);
    }
}

int rnet_account_available(void) {
    /* Unknown counts as available: offering the button is right, because the
     * first /auth/discord/start settles it and a 503 turns it off for good. */
    return !g.unavailable;
}


int rnet_account_login_begin(void) {
    /* Retry has to work while a login is still polling, so ask the old worker
     * to stand down and wait briefly for it. It checks the flag every 250ms. */
    if (g.busy) {
        int spins = 0;
        g.cancel = 1;
        while (g.busy && spins++ < 40) auth_sleep_ms(50);
        if (g.busy) {
            set_err("Still finishing the last sign-in attempt. Try again in a moment.");
            return -1;
        }
    }
    g.cancel = 0;
    g.error[0] = '\0';
    g.state = RNET_ACCOUNT_WAITING;
    return start_job(JOB_LOGIN);
}


int rnet_account_state(void) { return g.state; }
const char *rnet_account_handle(void) { return g.handle; }
const char *rnet_account_username(void) { return g.username; }
const char *rnet_account_error(void) { return g.error; }
const char *rnet_account_session(void) { return g.session; }

int rnet_account_sign_out(void) {
    if (g.busy) return -1;
    return start_job(JOB_SIGN_OUT);
}

int rnet_account_set_handle(const char *handle) {
    if (g.busy || !handle || !handle[0]) return -1;
    snprintf(g.pending_handle, sizeof(g.pending_handle), "%s", handle);
    g.error[0] = '\0';
    if (start_job(JOB_SET_HANDLE) != 0) return -1;
    /* The launcher wants a synchronous verdict for its modal, and the request
     * is one round trip on a LAN-ish connection: wait briefly rather than
     * inventing an async path through the UI for a rename. */
    {
        int spins = 0;
        while (g.busy && spins++ < 400) {
#if defined(_WIN32)
            Sleep(10);
#else
            struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 10 * 1000 * 1000;
            nanosleep(&ts, NULL);
#endif
        }
    }
    return g.error[0] ? -1 : 0;
}
