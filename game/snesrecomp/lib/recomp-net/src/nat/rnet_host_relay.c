/*
 * Host relay (recomp_net/host_relay.h): the host's reachable port, the
 * guest's probe, and the waiting-room orchestration. Pump-driven C11 port of
 * retcomm-launcher's netplay_nat.cpp; see the header for the contract.
 */
#include "recomp_net/host_relay.h"

#include "platform/rnet_platform.h"
#include "platform/rnet_stun_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <net/if.h>
#include <net/route.h>
#include <sys/sysctl.h>
#endif
#endif

/* ------------------------------------------------------------------------ */
/* small string helpers                                                       */

static void copy_str(char *dst, size_t cap, const char *src, size_t n)
{
    if (!cap) return;
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void set_str(char *dst, size_t cap, const char *src)
{
    copy_str(dst, cap, src ? src : "", src ? strlen(src) : 0);
}

static int ieq_n(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        const int ca = tolower((unsigned char)a[i]), cb = tolower((unsigned char)b[i]);
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
    return 1;
}

/* Case-insensitive find of `needle` in the first `len` bytes of `hay`. */
static const char *ifind(const char *hay, size_t len, const char *needle)
{
    const size_t nl = strlen(needle);
    size_t i;
    if (!nl || len < nl) return NULL;
    for (i = 0; i + nl <= len; ++i)
        if (ieq_n(hay + i, needle, nl)) return hay + i;
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* pure encoders and parsers                                                  */

int rnet_hr_ssdp_location(const char *response, char *out, size_t cap)
{
    const char *line = response;
    if (!response || !out || !cap) return 0;
    out[0] = '\0';
    while (*line) {
        const char *end = strchr(line, '\n');
        const size_t n = end ? (size_t)(end - line) : strlen(line);
        const char *colon = memchr(line, ':', n);
        if (colon && (size_t)(colon - line) == 8 && ieq_n(line, "location", 8)) {
            const char *v = colon + 1;
            const char *vend = line + n;
            while (v < vend && (*v == ' ' || *v == '\t')) ++v;
            while (vend > v && (vend[-1] == '\r' || vend[-1] == ' ' || vend[-1] == '\t')) --vend;
            copy_str(out, cap, v, (size_t)(vend - v));
            return out[0] != '\0';
        }
        if (!end) break;
        line = end + 1;
    }
    return 0;
}

int rnet_hr_xml_text(const char *xml, const char *tag, char *out, size_t cap)
{
    const size_t tl = tag ? strlen(tag) : 0;
    const char *at;
    if (!xml || !tl || !out || !cap) return 0;
    out[0] = '\0';
    /* Namespaced or not: <tag> or <x:tag>. */
    for (at = xml; (at = strstr(at, tag)) != NULL; ++at) {
        const char *open;
        const char *close;
        if (at[tl] != '>') continue;
        for (open = at; open > xml && *open != '<'; --open) {}
        if (*open != '<') continue;
        {
            const size_t hl = (size_t)(at - open - 1);
            if (hl > 0 && (open[1] == '/' || memchr(open + 1, ' ', hl))) continue;
            if (hl > 0 && open[hl] != ':') continue;
        }
        close = strstr(at + tl + 1, "</");
        if (!close) return 0;
        copy_str(out, cap, at + tl + 1, (size_t)(close - (at + tl + 1)));
        return 1;
    }
    return 0;
}

static int split_url(const char *url, char *origin, size_t ocap, char *path, size_t pcap)
{
    const char *scheme = strstr(url, "://");
    const char *slash;
    if (!scheme) return 0;
    slash = strchr(scheme + 3, '/');
    if (origin) copy_str(origin, ocap, url, slash ? (size_t)(slash - url) : strlen(url));
    if (path) set_str(path, pcap, slash ? slash : "/");
    return 1;
}

int rnet_hr_absolute_url(const char *ref, const char *base, char *out, size_t cap)
{
    char origin[256], path[512];
    const char *last;
    if (!ref || !out || !cap) return 0;
    if (strstr(ref, "://")) { set_str(out, cap, ref); return 1; }
    if (!base || !split_url(base, origin, sizeof(origin), path, sizeof(path))) {
        set_str(out, cap, ref);
        return 1;
    }
    if (ref[0] == '/') {
        snprintf(out, cap, "%s%s", origin, ref);
        return 1;
    }
    last = strrchr(path, '/');
    snprintf(out, cap, "%s%.*s%s", origin, last ? (int)(last - path + 1) : 1, path, ref);
    return 1;
}

int rnet_hr_igd_control_url(const char *xml, const char *location,
                            char *control_url, size_t cap,
                            char *service_type, size_t type_cap)
{
    static const char *const wanted[] = {
        "urn:schemas-upnp-org:service:WANIPConnection:2",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:service:WANPPPConnection:1",
    };
    char base_tag[256];
    const char *base;
    size_t w;
    if (!xml) return 0;
    base = rnet_hr_xml_text(xml, "URLBase", base_tag, sizeof(base_tag)) && base_tag[0]
               ? base_tag : location;
    for (w = 0; w < sizeof(wanted) / sizeof(wanted[0]); ++w) {
        const char *at = xml;
        while ((at = strstr(at, "<service>")) != NULL) {
            const char *end = strstr(at, "</service>");
            char svc[2048], type[160], ctl[512];
            if (!end) break;
            copy_str(svc, sizeof(svc), at, (size_t)(end - at));
            at = end;
            if (!rnet_hr_xml_text(svc, "serviceType", type, sizeof(type)) ||
                strcmp(type, wanted[w]) != 0)
                continue;
            if (!rnet_hr_xml_text(svc, "controlURL", ctl, sizeof(ctl)) || !ctl[0])
                continue;
            if (control_url) rnet_hr_absolute_url(ctl, base, control_url, cap);
            if (service_type) set_str(service_type, type_cap, wanted[w]);
            return 1;
        }
    }
    return 0;
}

int rnet_hr_soap_body(const char *service_type, const char *action,
                      const char *const *kv, int pairs, char *out, size_t cap)
{
    int n, i;
    size_t off;
    if (!service_type || !action || !out || !cap) return 0;
    n = snprintf(out, cap,
                 "<?xml version=\"1.0\"?>\r\n"
                 "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                 "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                 "<s:Body><u:%s xmlns:u=\"%s\">", action, service_type);
    if (n < 0 || (size_t)n >= cap) return 0;
    off = (size_t)n;
    for (i = 0; i < pairs; ++i) {
        n = snprintf(out + off, cap - off, "<%s>%s</%s>", kv[2 * i], kv[2 * i + 1], kv[2 * i]);
        if (n < 0 || (size_t)n >= cap - off) return 0;
        off += (size_t)n;
    }
    n = snprintf(out + off, cap - off, "</u:%s></s:Body></s:Envelope>\r\n", action);
    if (n < 0 || (size_t)n >= cap - off) return 0;
    return 1;
}

void rnet_hr_natpmp_mapping_request(rnet_u8 out[12], unsigned short internal_port,
                                    unsigned short external_port, rnet_u32 lifetime_s)
{
    out[0] = 0; out[1] = 1; out[2] = 0; out[3] = 0;
    out[4] = (rnet_u8)(internal_port >> 8); out[5] = (rnet_u8)internal_port;
    out[6] = (rnet_u8)(external_port >> 8); out[7] = (rnet_u8)external_port;
    out[8] = (rnet_u8)(lifetime_s >> 24); out[9] = (rnet_u8)(lifetime_s >> 16);
    out[10] = (rnet_u8)(lifetime_s >> 8); out[11] = (rnet_u8)lifetime_s;
}

int rnet_hr_natpmp_parse_mapping(const rnet_u8 *r, size_t len,
                                 unsigned short *external_port, unsigned short *result_code)
{
    unsigned short rc;
    if (!r || len < 16 || r[0] != 0 || r[1] != 129) return 0;
    rc = (unsigned short)((r[2] << 8) | r[3]);
    if (result_code) *result_code = rc;
    if (rc != 0) return 0;
    if (external_port) *external_port = (unsigned short)((r[10] << 8) | r[11]);
    return 1;
}

int rnet_hr_natpmp_parse_address(const rnet_u8 *r, size_t len, char *ip, size_t cap)
{
    if (!r || len < 12 || r[0] != 0 || r[1] != 128 || r[2] != 0 || r[3] != 0) return 0;
    if (ip) snprintf(ip, cap, "%u.%u.%u.%u", r[8], r[9], r[10], r[11]);
    return 1;
}

int rnet_hr_gateway_from_route(const char *text, char *out, size_t cap)
{
    const char *line;
    if (!text || !out || !cap) return 0;
    out[0] = '\0';
    line = strchr(text, '\n'); /* header */
    while (line && *line) {
        char iface[32], dest[32], gw[32];
        const char *next;
        ++line;
        next = strchr(line, '\n');
        if (sscanf(line, "%31s %31s %31s", iface, dest, gw) == 3 &&
            strcmp(dest, "00000000") == 0 && strcmp(gw, "00000000") != 0) {
            const unsigned long v = strtoul(gw, NULL, 16); /* little-endian hex */
            snprintf(out, cap, "%lu.%lu.%lu.%lu", v & 0xFF, (v >> 8) & 0xFF,
                     (v >> 16) & 0xFF, (v >> 24) & 0xFF);
            return 1;
        }
        line = next;
    }
    return 0;
}

int rnet_hr_http_split(const char *raw, size_t len, int *status,
                       const char **body, size_t *body_len)
{
    const char *sep;
    size_t i;
    if (!raw || len < 12 || memcmp(raw, "HTTP/", 5) != 0) return 0;
    for (i = 5; i < len && raw[i] != ' '; ++i) {}
    if (i + 4 > len) return 0;
    if (status) *status = atoi(raw + i + 1);
    sep = NULL;
    for (i = 0; i + 3 < len; ++i)
        if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n') {
            sep = raw + i + 4;
            break;
        }
    if (!sep) {
        for (i = 0; i + 1 < len; ++i)
            if (raw[i] == '\n' && raw[i + 1] == '\n') { sep = raw + i + 2; break; }
    }
    if (body) *body = sep ? sep : raw + len;
    if (body_len) *body_len = sep ? (size_t)(raw + len - sep) : 0;
    return 1;
}

int rnet_hr_default_gateway(char *out, size_t cap)
{
    if (!out || !cap) return 0;
    out[0] = '\0';
#if defined(_WIN32)
    {
        ULONG size = 0;
        MIB_IPFORWARDTABLE *tbl;
        DWORD i, rc;
        if (GetIpForwardTable(NULL, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || !size)
            return 0;
        tbl = (MIB_IPFORWARDTABLE *)malloc(size);
        if (!tbl) return 0;
        rc = GetIpForwardTable(tbl, &size, FALSE);
        if (rc == NO_ERROR) {
            for (i = 0; i < tbl->dwNumEntries; ++i) {
                const MIB_IPFORWARDROW *row = &tbl->table[i];
                if (row->dwForwardDest == 0 && row->dwForwardNextHop != 0) {
                    const rnet_u32 v = (rnet_u32)row->dwForwardNextHop; /* network order */
                    snprintf(out, cap, "%u.%u.%u.%u", v & 0xFF, (v >> 8) & 0xFF,
                             (v >> 16) & 0xFF, (v >> 24) & 0xFF);
                    break;
                }
            }
        }
        free(tbl);
        return out[0] != '\0';
    }
#elif defined(__linux__)
    {
        FILE *f = fopen("/proc/net/route", "r");
        char *text;
        size_t n = 0, cap_text = 16384;
        if (!f) return 0;
        text = (char *)malloc(cap_text);
        if (!text) { fclose(f); return 0; }
        n = fread(text, 1, cap_text - 1, f);
        text[n] = '\0';
        fclose(f);
        {
            const int ok = rnet_hr_gateway_from_route(text, out, cap);
            free(text);
            return ok;
        }
    }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    {
        int mib[6] = { CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_GATEWAY };
        size_t needed = 0;
        char *buf, *next, *lim;
        if (sysctl(mib, 6, NULL, &needed, NULL, 0) < 0 || !needed) return 0;
        buf = (char *)malloc(needed);
        if (!buf) return 0;
        if (sysctl(mib, 6, buf, &needed, NULL, 0) < 0) { free(buf); return 0; }
        lim = buf + needed;
        for (next = buf; next < lim;) {
            const struct rt_msghdr *rtm = (const struct rt_msghdr *)next;
            const struct sockaddr *sa = (const struct sockaddr *)(rtm + 1);
            const struct sockaddr *dst = NULL, *gw = NULL;
            int i;
            for (i = 0; i < RTAX_MAX; ++i) {
                if (!(rtm->rtm_addrs & (1 << i))) continue;
                if (i == RTAX_DST) dst = sa;
                if (i == RTAX_GATEWAY) gw = sa;
                sa = (const struct sockaddr *)((const char *)sa +
                     (sa->sa_len ? ((sa->sa_len + sizeof(long) - 1) & ~(sizeof(long) - 1)) : sizeof(long)));
            }
            if (dst && gw && dst->sa_family == AF_INET && gw->sa_family == AF_INET &&
                ((const struct sockaddr_in *)dst)->sin_addr.s_addr == 0) {
                inet_ntop(AF_INET, &((const struct sockaddr_in *)gw)->sin_addr, out, (socklen_t)cap);
                break;
            }
            next += rtm->rtm_msglen;
        }
        free(buf);
        return out[0] != '\0';
    }
#else
    return 0;
#endif
}

/* ------------------------------------------------------------------------ */
/* sockets (the platform layer has datagram sockets; HTTP needs a stream)    */

static int sock_err_would_block(void)
{
#ifdef _WIN32
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN;
#endif
}

static void sock_close(rnet_socket *s)
{
    if (!s || !rnet_os_socket_valid(*s)) return;
#ifdef _WIN32
    closesocket(*s);
#else
    close(*s);
#endif
    *s = RNET_SOCKET_INVALID;
}

static rnet_socket tcp_create_nonblocking(void)
{
    rnet_socket s;
    rnet_os_startup();
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (!rnet_os_socket_valid(s)) return RNET_SOCKET_INVALID;
    if (rnet_os_set_nonblocking(s) != 0) { sock_close(&s); return RNET_SOCKET_INVALID; }
    return s;
}

/* 1 when the socket is readable (want_write 0) / writable (1) right now. */
static int sock_ready(rnet_socket s, int want_write)
{
    fd_set set, eset;
    struct timeval tv;
    int rc;
    FD_ZERO(&set);
    FD_ZERO(&eset);
    FD_SET(s, &set);
    FD_SET(s, &eset);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
#ifdef _WIN32
    rc = select(0, want_write ? NULL : &set, want_write ? &set : NULL, &eset, &tv);
#else
    rc = select((int)s + 1, want_write ? NULL : &set, want_write ? &set : NULL, &eset, &tv);
#endif
    if (rc <= 0) return 0;
    if (FD_ISSET(s, &eset)) return -1;
    return FD_ISSET(s, &set) ? 1 : 0;
}

static rnet_socket udp_create_nonblocking(unsigned short port, int *bound_ok)
{
    rnet_socket s = rnet_os_socket_create_dgram();
    struct sockaddr_in a;
    if (bound_ok) *bound_ok = 0;
    if (!rnet_os_socket_valid(s)) return RNET_SOCKET_INVALID;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (rnet_os_bind(s, &a) != 0) { rnet_os_socket_destroy(&s); return RNET_SOCKET_INVALID; }
    if (bound_ok) *bound_ok = 1;
    (void)rnet_os_set_nonblocking(s);
    return s;
}

/* The address this machine uses to reach `ip`: a connected UDP socket's name. */
static int local_ip_toward(const char *ip, char *out, size_t cap)
{
    rnet_socket s = rnet_os_socket_create_dgram();
    struct sockaddr_in d, me;
#ifdef _WIN32
    int ml = sizeof(me);
#else
    socklen_t ml = sizeof(me);
#endif
    int ok = 0;
    if (!rnet_os_socket_valid(s)) return 0;
    if (rnet_os_resolve_sockaddr(ip, 9, &d) == 0 &&
        connect(s, (struct sockaddr *)&d, sizeof(d)) == 0 &&
        getsockname(s, (struct sockaddr *)&me, &ml) == 0) {
        const rnet_u32 v = ntohl(me.sin_addr.s_addr);
        snprintf(out, cap, "%u.%u.%u.%u", (v >> 24) & 0xFF, (v >> 16) & 0xFF,
                 (v >> 8) & 0xFF, v & 0xFF);
        ok = 1;
    }
    rnet_os_socket_destroy(&s);
    return ok;
}

/* ------------------------------------------------------------------------ */
/* non-blocking HTTP/1.1 exchange (GET or SOAP POST), one at a time           */

enum { HTTP_IDLE, HTTP_CONNECTING, HTTP_SENDING, HTTP_RECEIVING, HTTP_DONE, HTTP_FAILED };

#define HTTP_RESP_CAP 65536

typedef struct HrHttp {
    int state;
    rnet_socket s;
    char *req;
    size_t req_len, req_off;
    char *resp;
    size_t resp_len;
    size_t content_length;      /* 0 = unknown: read to EOF */
    size_t header_len;          /* 0 until the blank line arrived */
    rnet_u64 deadline_ms;
    int status;
} HrHttp;

static void http_reset(HrHttp *h)
{
    sock_close(&h->s);
    free(h->req);
    h->req = NULL;
    h->req_len = h->req_off = 0;
    h->resp_len = h->content_length = h->header_len = 0;
    h->status = 0;
    h->state = HTTP_IDLE;
}

static int url_parts(const char *url, char *host, size_t hcap, unsigned short *port,
                     char *path, size_t pcap)
{
    const char *p = strstr(url, "://");
    const char *slash, *colon, *hend;
    if (!p || strncmp(url, "http://", 7) != 0) return 0;
    p += 3;
    slash = strchr(p, '/');
    hend = slash ? slash : p + strlen(p);
    colon = memchr(p, ':', (size_t)(hend - p));
    *port = 80;
    if (colon) {
        *port = (unsigned short)atoi(colon + 1);
        copy_str(host, hcap, p, (size_t)(colon - p));
    } else {
        copy_str(host, hcap, p, (size_t)(hend - p));
    }
    set_str(path, pcap, slash ? slash : "/");
    return host[0] != '\0' && *port != 0;
}

/* soap_action NULL = GET. Returns 0 when started. */
static int http_begin(HrHttp *h, const char *url, const char *soap_action, const char *body,
                      int timeout_ms)
{
    char host[128], path[512];
    unsigned short port;
    struct sockaddr_in to;
    size_t cap;
    int n;
    http_reset(h);
    if (!url_parts(url, host, sizeof(host), &port, path, sizeof(path))) return -1;
    if (rnet_os_resolve_sockaddr(host, port, &to) != 0) return -1;
    if (!h->resp) {
        h->resp = (char *)malloc(HTTP_RESP_CAP);
        if (!h->resp) return -1;
    }
    cap = 1024 + strlen(path) + (body ? strlen(body) : 0) + (soap_action ? strlen(soap_action) : 0);
    h->req = (char *)malloc(cap);
    if (!h->req) return -1;
    if (soap_action) {
        n = snprintf(h->req, cap,
                     "POST %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: recomp-net\r\n"
                     "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"%s\"\r\n"
                     "Content-Length: %u\r\nConnection: close\r\n\r\n%s",
                     path, host, (unsigned)port, soap_action,
                     (unsigned)(body ? strlen(body) : 0), body ? body : "");
    } else {
        n = snprintf(h->req, cap,
                     "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: recomp-net\r\n"
                     "Accept: text/xml\r\nConnection: close\r\n\r\n",
                     path, host, (unsigned)port);
    }
    if (n < 0 || (size_t)n >= cap) { http_reset(h); return -1; }
    h->req_len = (size_t)n;
    h->s = tcp_create_nonblocking();
    if (!rnet_os_socket_valid(h->s)) { http_reset(h); return -1; }
    if (connect(h->s, (struct sockaddr *)&to, sizeof(to)) != 0 && !sock_err_would_block()) {
        http_reset(h);
        return -1;
    }
    h->deadline_ms = rnet_os_monotonic_ms() + (rnet_u64)(timeout_ms > 0 ? timeout_ms : 4000);
    h->state = HTTP_CONNECTING;
    return 0;
}

static void http_scan_headers(HrHttp *h)
{
    const char *sep;
    if (h->header_len) return;
    sep = NULL;
    {
        size_t i;
        for (i = 0; i + 3 < h->resp_len; ++i)
            if (h->resp[i] == '\r' && h->resp[i + 1] == '\n' && h->resp[i + 2] == '\r' &&
                h->resp[i + 3] == '\n') {
                sep = h->resp + i + 4;
                break;
            }
    }
    if (!sep) return;
    h->header_len = (size_t)(sep - h->resp);
    {
        const char *cl = ifind(h->resp, h->header_len, "content-length:");
        if (cl) h->content_length = (size_t)strtoul(cl + 15, NULL, 10);
    }
}

/* HTTP_DONE / HTTP_FAILED when finished; else the state it is in. */
static int http_pump(HrHttp *h)
{
    if (h->state == HTTP_IDLE || h->state == HTTP_DONE || h->state == HTTP_FAILED)
        return h->state;
    if (rnet_os_monotonic_ms() > h->deadline_ms) {
        /* A server that never closes but sent a whole body is a success. */
        if (h->state == HTTP_RECEIVING && h->header_len) goto finish;
        sock_close(&h->s);
        h->state = HTTP_FAILED;
        return h->state;
    }
    if (h->state == HTTP_CONNECTING) {
        const int w = sock_ready(h->s, 1);
        if (w < 0) { sock_close(&h->s); h->state = HTTP_FAILED; return h->state; }
        if (!w) return h->state;
        h->state = HTTP_SENDING;
    }
    if (h->state == HTTP_SENDING) {
        while (h->req_off < h->req_len) {
            const int n = (int)send(h->s, h->req + h->req_off, (int)(h->req_len - h->req_off), 0);
            if (n > 0) { h->req_off += (size_t)n; continue; }
            if (n < 0 && sock_err_would_block()) return h->state;
            sock_close(&h->s);
            h->state = HTTP_FAILED;
            return h->state;
        }
        h->state = HTTP_RECEIVING;
    }
    if (h->state == HTTP_RECEIVING) {
        for (;;) {
            int n;
            if (h->resp_len >= HTTP_RESP_CAP - 1) goto finish;
            n = (int)recv(h->s, h->resp + h->resp_len, (int)(HTTP_RESP_CAP - 1 - h->resp_len), 0);
            if (n > 0) {
                h->resp_len += (size_t)n;
                http_scan_headers(h);
                if (h->header_len && h->content_length &&
                    h->resp_len >= h->header_len + h->content_length)
                    goto finish;
                continue;
            }
            if (n == 0) goto finish; /* EOF */
            if (sock_err_would_block()) return h->state;
            if (h->header_len) goto finish;
            sock_close(&h->s);
            h->state = HTTP_FAILED;
            return h->state;
        }
    }
    return h->state;
finish:
    sock_close(&h->s);
    h->resp[h->resp_len] = '\0';
    http_scan_headers(h);
    if (!rnet_hr_http_split(h->resp, h->resp_len, &h->status, NULL, NULL)) {
        h->state = HTTP_FAILED;
        return h->state;
    }
    h->state = HTTP_DONE;
    return h->state;
}

static const char *http_body(const HrHttp *h)
{
    return h->header_len ? h->resp + h->header_len : "";
}

/* ------------------------------------------------------------------------ */
/* the host side: RNetHostPort                                                */

enum {
    HP_UPNP_DISCOVER, HP_UPNP_DESC, HP_UPNP_ADD, HP_UPNP_EXTIP,
    HP_NATPMP_ADDR, HP_NATPMP_MAP, HP_STUN, HP_DONE
};

struct RNetHostPort {
    rnet_socket game;           /* the game port; probes answered here */
    rnet_socket ssdp;           /* UPnP discovery */
    rnet_socket pmp;            /* NAT-PMP to the gateway */
    HrHttp http;
    int stage;
    int try_router;
    rnet_u64 stage_deadline_ms;
    rnet_u64 stun_next_ms;
    int stun_tries;
    rnet_u8 stun_txid[RNET_STUN_TRANSACTION_ID_SIZE];
    struct sockaddr_in stun_to;
    char stun_hostport[128];
    char location[512];
    char control_url[512];
    char service_type[96];
    char local_ip[48];
    char gateway[48];
    char pmp_ext_ip[48];
    struct sockaddr_in pmp_to;
    unsigned short mapped_port; /* what unmap removes (0 = nothing) */
    int natpmp_mapped;
    RNetHostPortStatus st;
};

static void hp_set_stage(RNetHostPort *hp, int stage, const char *label, int budget_ms)
{
    hp->stage = stage;
    set_str(hp->st.stage, sizeof(hp->st.stage), label);
    hp->stage_deadline_ms = rnet_os_monotonic_ms() + (rnet_u64)budget_ms;
}

static void hp_finish(RNetHostPort *hp, const char *endpoint, const char *how, const char *detail)
{
    set_str(hp->st.endpoint, sizeof(hp->st.endpoint), endpoint);
    set_str(hp->st.how, sizeof(hp->st.how), how);
    set_str(hp->st.detail, sizeof(hp->st.detail), detail);
    set_str(hp->st.stage, sizeof(hp->st.stage), "done");
    hp->st.done = 1;
    hp->stage = HP_DONE;
    sock_close(&hp->ssdp);
    sock_close(&hp->pmp);
    http_reset(&hp->http);
}

static void hp_begin_stun(RNetHostPort *hp)
{
    char host[128];
    rnet_u16 port = 0;
    hp_set_stage(hp, HP_STUN, "stun", 2000);
    hp->stun_tries = 0;
    hp->stun_next_ms = 0;
    if (rnet_os_parse_hostport(hp->stun_hostport, host, sizeof(host), &port) != 0 || !port ||
        rnet_os_resolve_sockaddr(host, port, &hp->stun_to) != 0 ||
        rnet_os_random_bytes(hp->stun_txid, sizeof(hp->stun_txid)) != 0) {
        hp_finish(hp, "", "",
                  "Could not find a public address for this machine: the lobby "
                  "server will relay the match.");
    }
}

static void hp_begin_natpmp(RNetHostPort *hp)
{
    static const rnet_u8 addr_req[2] = { 0, 0 };
    if (!hp->try_router || !rnet_hr_default_gateway(hp->gateway, sizeof(hp->gateway)) ||
        rnet_os_resolve_sockaddr(hp->gateway, 5351, &hp->pmp_to) != 0) {
        hp_begin_stun(hp);
        return;
    }
    hp->pmp = udp_create_nonblocking(0, NULL);
    if (!rnet_os_socket_valid(hp->pmp)) { hp_begin_stun(hp); return; }
    (void)rnet_os_sendto(hp->pmp, addr_req, sizeof(addr_req), &hp->pmp_to);
    hp_set_stage(hp, HP_NATPMP_ADDR, "nat-pmp", 800);
}

static void hp_begin_upnp(RNetHostPort *hp)
{
    static const char *const st[] = {
        "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
    };
    struct sockaddr_in d;
    size_t i;
    if (!hp->try_router) { hp_begin_stun(hp); return; }
    hp->ssdp = udp_create_nonblocking(0, NULL);
    if (!rnet_os_socket_valid(hp->ssdp) ||
        rnet_os_resolve_sockaddr("239.255.255.250", 1900, &d) != 0) {
        sock_close(&hp->ssdp);
        hp_begin_natpmp(hp);
        return;
    }
    for (i = 0; i < 2; ++i) {
        char req[320];
        const int n = snprintf(req, sizeof(req),
                               "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
                               "MAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n", st[i]);
        if (n > 0 && (size_t)n < sizeof(req))
            (void)rnet_os_sendto(hp->ssdp, req, (size_t)n, &d);
    }
    hp_set_stage(hp, HP_UPNP_DISCOVER, "upnp", 2500);
}

int rnet_host_port_open(RNetHostPort **out, unsigned short port,
                        const char *stun_hostport, int try_router)
{
    RNetHostPort *hp;
    const char *env_ep;
    const char *env_nr;
    unsigned short p, lo, hi;
    if (!out) return -1;
    *out = NULL;
    rnet_os_startup();
    hp = (RNetHostPort *)calloc(1, sizeof(*hp));
    if (!hp) return -1;
    hp->game = hp->ssdp = hp->pmp = RNET_SOCKET_INVALID;
    hp->http.s = RNET_SOCKET_INVALID;
    lo = port ? port : 7777;
    hi = port ? port : 7808;
    for (p = lo; p <= hi; ++p) {
        int ok = 0;
        hp->game = udp_create_nonblocking(p, &ok);
        if (ok) { hp->st.local_port = p; break; }
        if (p == 65535) break;
    }
    if (!rnet_os_socket_valid(hp->game)) {
        free(hp);
        return -2;
    }
    env_nr = getenv("RNET_HOST_RELAY_NO_ROUTER");
    hp->try_router = try_router && !(env_nr && env_nr[0] == '1');
    set_str(hp->stun_hostport, sizeof(hp->stun_hostport),
            stun_hostport && stun_hostport[0] ? stun_hostport : "stun.l.google.com:19302");
    snprintf(hp->st.detail, sizeof(hp->st.detail), "Opening UDP port %u...",
             (unsigned)hp->st.local_port);
    env_ep = getenv("RNET_HOST_RELAY_ENDPOINT");
    if (env_ep && env_ep[0]) {
        hp_finish(hp, env_ep, "env", "Advertising RNET_HOST_RELAY_ENDPOINT (test override).");
    } else {
        hp_begin_upnp(hp);
    }
    *out = hp;
    return 0;
}

/* Answer probes; hand anything else to the STUN stage. */
static void hp_drain_game(RNetHostPort *hp)
{
    for (;;) {
        rnet_u8 buf[1500];
        struct sockaddr_in from;
        int wb = 0;
        const int n = rnet_os_recvfrom(hp->game, buf, sizeof(buf), &from, &wb);
        if (n <= 0) return;
        if ((size_t)n > strlen(RNET_HOST_PROBE_MAGIC) &&
            memcmp(buf, RNET_HOST_PROBE_MAGIC, strlen(RNET_HOST_PROBE_MAGIC)) == 0) {
            char ack[256];
            const size_t ml = strlen(RNET_HOST_PROBE_MAGIC);
            const size_t nl = (size_t)n - ml;
            const size_t al = strlen(RNET_HOST_PROBE_ACK_MAGIC);
            if (al + nl < sizeof(ack)) {
                memcpy(ack, RNET_HOST_PROBE_ACK_MAGIC, al);
                memcpy(ack + al, buf + ml, nl);
                (void)rnet_os_sendto(hp->game, ack, al + nl, &from);
                hp->st.probes_answered++;
            }
            continue;
        }
        if (hp->stage == HP_STUN) {
            rnet_u32 addr = 0;
            rnet_u16 mport = 0;
            if (rnet_stun_parse_binding_response_ex(buf, (size_t)n, hp->stun_txid, &addr, &mport) ==
                    RNET_STUN_PARSE_OK && mport) {
                char ep[64], detail[200];
                snprintf(ep, sizeof(ep), "%u.%u.%u.%u:%u", (addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
                         (addr >> 8) & 0xFF, addr & 0xFF, (unsigned)mport);
                snprintf(detail, sizeof(detail),
                         "No UPnP or NAT-PMP router answered. Players reach you only if UDP "
                         "port %u is forwarded to this machine; otherwise the lobby server "
                         "relays the match.", (unsigned)hp->st.local_port);
                hp_finish(hp, ep, "stun", detail);
            }
        }
    }
}

static void hp_pump_upnp_discover(RNetHostPort *hp)
{
    for (;;) {
        char buf[2048];
        struct sockaddr_in from;
        int wb = 0;
        const int n = rnet_os_recvfrom(hp->ssdp, buf, sizeof(buf) - 1, &from, &wb);
        if (n <= 0) break;
        buf[n] = '\0';
        if (rnet_hr_ssdp_location(buf, hp->location, sizeof(hp->location))) {
            sock_close(&hp->ssdp);
            if (http_begin(&hp->http, hp->location, NULL, NULL, 4000) == 0) {
                hp_set_stage(hp, HP_UPNP_DESC, "upnp", 4500);
            } else {
                hp_begin_natpmp(hp);
            }
            return;
        }
    }
    if (rnet_os_monotonic_ms() > hp->stage_deadline_ms) {
        sock_close(&hp->ssdp);
        hp_begin_natpmp(hp);
    }
}

static void hp_pump_upnp_http(RNetHostPort *hp)
{
    const int st = http_pump(&hp->http);
    if (st != HTTP_DONE && st != HTTP_FAILED) {
        if (rnet_os_monotonic_ms() > hp->stage_deadline_ms) { http_reset(&hp->http); hp_begin_natpmp(hp); }
        return;
    }
    if (st == HTTP_FAILED || hp->http.status != 200) { http_reset(&hp->http); hp_begin_natpmp(hp); return; }
    if (hp->stage == HP_UPNP_DESC) {
        char ctl_host[128], path[512];
        unsigned short ctl_port;
        const char *kv[16];
        char port_s[8];
        if (!rnet_hr_igd_control_url(http_body(&hp->http), hp->location, hp->control_url,
                                     sizeof(hp->control_url), hp->service_type,
                                     sizeof(hp->service_type)) ||
            !url_parts(hp->control_url, ctl_host, sizeof(ctl_host), &ctl_port, path, sizeof(path)) ||
            !local_ip_toward(ctl_host, hp->local_ip, sizeof(hp->local_ip))) {
            http_reset(&hp->http);
            hp_begin_natpmp(hp);
            return;
        }
        snprintf(port_s, sizeof(port_s), "%u", (unsigned)hp->st.local_port);
        kv[0] = "NewRemoteHost"; kv[1] = "";
        kv[2] = "NewExternalPort"; kv[3] = port_s;
        kv[4] = "NewProtocol"; kv[5] = "UDP";
        kv[6] = "NewInternalPort"; kv[7] = port_s;
        kv[8] = "NewInternalClient"; kv[9] = hp->local_ip;
        kv[10] = "NewEnabled"; kv[11] = "1";
        kv[12] = "NewPortMappingDescription"; kv[13] = "Retro netplay host";
        kv[14] = "NewLeaseDuration"; kv[15] = "7200";
        {
            char soap[2048], action[160];
            if (!rnet_hr_soap_body(hp->service_type, "AddPortMapping", kv, 8, soap, sizeof(soap))) {
                http_reset(&hp->http); hp_begin_natpmp(hp); return;
            }
            snprintf(action, sizeof(action), "%s#AddPortMapping", hp->service_type);
            if (http_begin(&hp->http, hp->control_url, action, soap, 4000) != 0) {
                hp_begin_natpmp(hp); return;
            }
            hp_set_stage(hp, HP_UPNP_ADD, "upnp", 4500);
        }
        return;
    }
    if (hp->stage == HP_UPNP_ADD) {
        char soap[1024], action[160];
        hp->mapped_port = hp->st.local_port; /* the router now forwards it */
        if (!rnet_hr_soap_body(hp->service_type, "GetExternalIPAddress", NULL, 0, soap, sizeof(soap))) {
            http_reset(&hp->http); hp_begin_natpmp(hp); return;
        }
        snprintf(action, sizeof(action), "%s#GetExternalIPAddress", hp->service_type);
        if (http_begin(&hp->http, hp->control_url, action, soap, 4000) != 0) { hp_begin_natpmp(hp); return; }
        hp_set_stage(hp, HP_UPNP_EXTIP, "upnp", 4500);
        return;
    }
    if (hp->stage == HP_UPNP_EXTIP) {
        char ip[64], ep[80], detail[200];
        if (!rnet_hr_xml_text(http_body(&hp->http), "NewExternalIPAddress", ip, sizeof(ip)) || !ip[0] ||
            strcmp(ip, "0.0.0.0") == 0) {
            http_reset(&hp->http);
            hp_begin_natpmp(hp);
            return;
        }
        snprintf(ep, sizeof(ep), "%s:%u", ip, (unsigned)hp->st.local_port);
        snprintf(detail, sizeof(detail), "Your router forwards UDP port %u (UPnP).",
                 (unsigned)hp->st.local_port);
        hp_finish(hp, ep, "upnp", detail);
    }
}

static void hp_pump_natpmp(RNetHostPort *hp)
{
    for (;;) {
        rnet_u8 buf[64];
        struct sockaddr_in from;
        int wb = 0;
        const int n = rnet_os_recvfrom(hp->pmp, buf, sizeof(buf), &from, &wb);
        if (n <= 0) break;
        if (hp->stage == HP_NATPMP_ADDR) {
            if (rnet_hr_natpmp_parse_address(buf, (size_t)n, hp->pmp_ext_ip, sizeof(hp->pmp_ext_ip))) {
                rnet_u8 req[12];
                rnet_hr_natpmp_mapping_request(req, hp->st.local_port, hp->st.local_port, 7200);
                (void)rnet_os_sendto(hp->pmp, req, sizeof(req), &hp->pmp_to);
                hp_set_stage(hp, HP_NATPMP_MAP, "nat-pmp", 800);
                return;
            }
        } else if (hp->stage == HP_NATPMP_MAP) {
            unsigned short ext = 0, rc = 0;
            if (rnet_hr_natpmp_parse_mapping(buf, (size_t)n, &ext, &rc) && ext) {
                char ep[80], detail[200];
                hp->natpmp_mapped = 1;
                hp->mapped_port = ext;
                snprintf(ep, sizeof(ep), "%s:%u", hp->pmp_ext_ip, (unsigned)ext);
                snprintf(detail, sizeof(detail), "Your router forwards UDP port %u (NAT-PMP).",
                         (unsigned)ext);
                hp_finish(hp, ep, "nat-pmp", detail);
                return;
            }
            sock_close(&hp->pmp);
            hp_begin_stun(hp);
            return;
        }
    }
    if (rnet_os_monotonic_ms() > hp->stage_deadline_ms) {
        sock_close(&hp->pmp);
        hp_begin_stun(hp);
    }
}

static void hp_pump_stun(RNetHostPort *hp)
{
    const rnet_u64 now = rnet_os_monotonic_ms();
    if (now >= hp->stun_next_ms) {
        rnet_u8 req[20];
        if (hp->stun_tries >= 3) {
            hp_finish(hp, "", "",
                      "Could not find a public address for this machine: the lobby "
                      "server will relay the match.");
            return;
        }
        req[0] = 0x00; req[1] = 0x01; req[2] = 0x00; req[3] = 0x00;
        req[4] = 0x21; req[5] = 0x12; req[6] = 0xA4; req[7] = 0x42;
        memcpy(req + 8, hp->stun_txid, 12);
        (void)rnet_os_sendto(hp->game, req, sizeof(req), &hp->stun_to);
        hp->stun_tries++;
        hp->stun_next_ms = now + 500;
    }
}

void rnet_host_port_pump(RNetHostPort *hp)
{
    if (!hp) return;
    hp_drain_game(hp);
    switch (hp->stage) {
    case HP_UPNP_DISCOVER: hp_pump_upnp_discover(hp); break;
    case HP_UPNP_DESC:
    case HP_UPNP_ADD:
    case HP_UPNP_EXTIP: hp_pump_upnp_http(hp); break;
    case HP_NATPMP_ADDR:
    case HP_NATPMP_MAP: hp_pump_natpmp(hp); break;
    case HP_STUN: hp_pump_stun(hp); break;
    default: break;
    }
}

void rnet_host_port_status(const RNetHostPort *hp, RNetHostPortStatus *out)
{
    if (!out) return;
    if (!hp) { memset(out, 0, sizeof(*out)); return; }
    *out = hp->st;
}

static void hp_free(RNetHostPort *hp)
{
    sock_close(&hp->game);
    sock_close(&hp->ssdp);
    sock_close(&hp->pmp);
    http_reset(&hp->http);
    free(hp->http.resp);
    free(hp);
}

void rnet_host_port_release(RNetHostPort **hp)
{
    if (!hp || !*hp) return;
    hp_free(*hp);
    *hp = NULL;
}

void rnet_host_port_unmap_release(RNetHostPort **php)
{
    RNetHostPort *hp = php ? *php : NULL;
    if (!hp) return;
    if (hp->mapped_port && hp->control_url[0] && !hp->natpmp_mapped) {
        char soap[1024], action[160], port_s[8];
        const char *kv[6];
        const rnet_u64 until = rnet_os_monotonic_ms() + 600;
        snprintf(port_s, sizeof(port_s), "%u", (unsigned)hp->mapped_port);
        kv[0] = "NewRemoteHost"; kv[1] = "";
        kv[2] = "NewExternalPort"; kv[3] = port_s;
        kv[4] = "NewProtocol"; kv[5] = "UDP";
        snprintf(action, sizeof(action), "%s#DeletePortMapping", hp->service_type);
        if (rnet_hr_soap_body(hp->service_type, "DeletePortMapping", kv, 3, soap, sizeof(soap)) &&
            http_begin(&hp->http, hp->control_url, action, soap, 600) == 0) {
            while (rnet_os_monotonic_ms() < until) {
                const int st = http_pump(&hp->http);
                if (st == HTTP_DONE || st == HTTP_FAILED) break;
                rnet_os_sleep_micros(10000);
            }
        }
    }
    if (hp->natpmp_mapped && hp->gateway[0] && hp->mapped_port) {
        rnet_socket s = udp_create_nonblocking(0, NULL);
        if (rnet_os_socket_valid(s)) {
            rnet_u8 req[12];
            rnet_hr_natpmp_mapping_request(req, hp->st.local_port, 0, 0); /* lifetime 0 = delete */
            (void)rnet_os_sendto(s, req, sizeof(req), &hp->pmp_to);
            rnet_os_socket_destroy(&s);
        }
    }
    hp_free(hp);
    *php = NULL;
}

/* ------------------------------------------------------------------------ */
/* the guest side: RNetHostProbe                                              */

struct RNetHostProbe {
    rnet_socket s;
    struct sockaddr_in to;
    char msg[96];
    char ack[112];
    size_t msg_len, ack_len;
    int tries, sent;
    rnet_u64 next_ms;
};

int rnet_host_probe_open(RNetHostProbe **out, const char *endpoint, int tries)
{
    RNetHostProbe *p;
    char host[128];
    rnet_u16 port = 0;
    rnet_u32 nonce = 0;
    if (!out) return -1;
    *out = NULL;
    if (!endpoint || rnet_os_parse_hostport(endpoint, host, sizeof(host), &port) != 0 || !port)
        return -1;
    p = (RNetHostProbe *)calloc(1, sizeof(*p));
    if (!p) return -1;
    if (rnet_os_resolve_sockaddr(host, port, &p->to) != 0) { free(p); return -1; }
    p->s = udp_create_nonblocking(0, NULL);
    if (!rnet_os_socket_valid(p->s)) { free(p); return -1; }
    (void)rnet_os_random_bytes(&nonce, sizeof(nonce));
    p->msg_len = (size_t)snprintf(p->msg, sizeof(p->msg), "%s%u", RNET_HOST_PROBE_MAGIC, (unsigned)nonce);
    p->ack_len = (size_t)snprintf(p->ack, sizeof(p->ack), "%s%u", RNET_HOST_PROBE_ACK_MAGIC, (unsigned)nonce);
    p->tries = tries > 0 ? tries : 6;
    p->next_ms = 0;
    *out = p;
    return 0;
}

int rnet_host_probe_pump(RNetHostProbe *p)
{
    rnet_u64 now;
    if (!p) return -1;
    for (;;) {
        rnet_u8 buf[256];
        struct sockaddr_in from;
        int wb = 0;
        const int n = rnet_os_recvfrom(p->s, buf, sizeof(buf), &from, &wb);
        if (n <= 0) break;
        if ((size_t)n == p->ack_len && memcmp(buf, p->ack, p->ack_len) == 0) return 1;
    }
    now = rnet_os_monotonic_ms();
    if (now >= p->next_ms) {
        if (p->sent >= p->tries) return -1;
        (void)rnet_os_sendto(p->s, p->msg, p->msg_len, &p->to);
        p->sent++;
        p->next_ms = now + 400;
    }
    return 0;
}

void rnet_host_probe_close(RNetHostProbe **pp)
{
    if (!pp || !*pp) return;
    sock_close(&(*pp)->s);
    free(*pp);
    *pp = NULL;
}

/* ------------------------------------------------------------------------ */
/* the orchestration: RNetHostRelay                                           */

#define HR_REPORT_REFRESH_MS 45000u   /* the server trusts a report for 120 s */
#define HR_FAIL_RETRY_MS     20000u
#define HR_OPEN_RETRY_MS     5000u

struct RNetHostRelay {
    RNetHostPort *port;
    RNetHostProbe *probe;
    rnet_u64 open_retry_ms;
    rnet_u64 report_due_ms;
    RNetHostRelayStatus st;
};

RNetHostRelay *rnet_host_relay_create(void)
{
    return (RNetHostRelay *)calloc(1, sizeof(RNetHostRelay));
}

void rnet_host_relay_destroy(RNetHostRelay **hr)
{
    if (!hr || !*hr) return;
    rnet_host_relay_leave(*hr);
    free(*hr);
    *hr = NULL;
}

static int hr_send(const RNetHostRelayView *v, const char *json)
{
    return v->send_json ? v->send_json(json, v->ctx) : -1;
}

static void hr_forget_guest(RNetHostRelay *hr)
{
    rnet_host_probe_close(&hr->probe);
    hr->st.probing = 0;
    hr->st.probed[0] = '\0';
    hr->st.last_report[0] = '\0';
    hr->report_due_ms = 0;
}

static void hr_update_host(RNetHostRelay *hr, const RNetHostRelayView *v)
{
    const rnet_u64 now = rnet_os_monotonic_ms();
    hr->st.role = 1;
    hr_forget_guest(hr);
    if (!hr->port) {
        if (now < hr->open_retry_ms) return;
        if (rnet_host_port_open(&hr->port, v->bind_port, v->stun_hostport, 1) != 0) {
            hr->open_retry_ms = now + HR_OPEN_RETRY_MS;
            memset(&hr->st.port, 0, sizeof(hr->st.port));
            snprintf(hr->st.port.detail, sizeof(hr->st.port.detail),
                     "UDP port %u is in use; retrying.", (unsigned)v->bind_port);
            return;
        }
    }
    rnet_host_port_pump(hr->port);
    rnet_host_port_status(hr->port, &hr->st.port);
    if (hr->st.port.done && hr->st.port.endpoint[0] &&
        strcmp(hr->st.port.endpoint, hr->st.advertised) != 0) {
        char json[160];
        snprintf(json, sizeof(json), "{\"op\":\"set_host_endpoint\",\"host_endpoint\":\"%s\"}",
                 hr->st.port.endpoint);
        if (hr_send(v, json) == 0)
            set_str(hr->st.advertised, sizeof(hr->st.advertised), hr->st.port.endpoint);
    }
}

static void hr_update_guest(RNetHostRelay *hr, const RNetHostRelayView *v)
{
    const rnet_u64 now = rnet_os_monotonic_ms();
    const char *ep = v->host_endpoint ? v->host_endpoint : "";
    hr->st.role = 2;
    if (hr->port) rnet_host_port_release(&hr->port); /* role changed under us */
    hr->st.advertised[0] = '\0';
    if (!ep[0]) { hr_forget_guest(hr); return; }
    if (strcmp(ep, hr->st.probed) != 0) {
        /* A new address must be proven afresh (the server cleared our report). */
        rnet_host_probe_close(&hr->probe);
        hr->st.probing = 0;
        hr->st.last_report[0] = '\0';
        set_str(hr->st.probed, sizeof(hr->st.probed), ep);
        hr->report_due_ms = 0;
    }
    if (hr->probe) {
        const int r = rnet_host_probe_pump(hr->probe);
        if (r == 0) return;
        rnet_host_probe_close(&hr->probe);
        hr->st.probing = 0;
        {
            char json[64];
            const char *path = r > 0 ? "direct" : "fail";
            snprintf(json, sizeof(json), "{\"op\":\"path_report\",\"path\":\"%s\"}", path);
            if (hr_send(v, json) == 0) {
                set_str(hr->st.last_report, sizeof(hr->st.last_report), path);
                hr->st.reports_sent++;
            }
            hr->report_due_ms = now + (r > 0 ? HR_REPORT_REFRESH_MS : HR_FAIL_RETRY_MS);
        }
        return;
    }
    if (now < hr->report_due_ms) return;
    if (rnet_host_probe_open(&hr->probe, ep, 6) == 0) {
        hr->st.probing = 1;
    } else {
        hr->report_due_ms = now + HR_FAIL_RETRY_MS;
    }
}

void rnet_host_relay_path_cleared(RNetHostRelay *hr)
{
    if (!hr || hr->st.role != 2 || hr->probe || !hr->st.last_report[0]) return;
    hr->st.last_report[0] = '\0';
    hr->report_due_ms = 0;
}

void rnet_host_relay_update(RNetHostRelay *hr, const RNetHostRelayView *v)
{
    if (!hr || !v) return;
    if (!v->active) {
        /* Not asked for (or not seated online): hold nothing. The mapping a
         * previous hosting made stays until leave(). */
        if (hr->port) rnet_host_port_release(&hr->port);
        hr_forget_guest(hr);
        hr->st.role = 0;
        return;
    }
    if (v->is_host) hr_update_host(hr, v);
    else hr_update_guest(hr, v);
}

void rnet_host_relay_release_port(RNetHostRelay *hr)
{
    if (!hr) return;
    rnet_host_port_release(&hr->port);
    rnet_host_probe_close(&hr->probe);
    hr->st.probing = 0;
}

void rnet_host_relay_leave(RNetHostRelay *hr)
{
    if (!hr) return;
    rnet_host_port_unmap_release(&hr->port);
    hr_forget_guest(hr);
    memset(&hr->st, 0, sizeof(hr->st));
    hr->open_retry_ms = 0;
}

void rnet_host_relay_status(const RNetHostRelay *hr, RNetHostRelayStatus *out)
{
    if (!out) return;
    if (!hr) { memset(out, 0, sizeof(*out)); return; }
    *out = hr->st;
}
