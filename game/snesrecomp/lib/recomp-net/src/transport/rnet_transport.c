#include "rnet_transport.h"

#include "protocol/rnet_protocol.h"

#include <string.h>

#define RNET_HUB_MIN_PACKET 14 /* magic(4)+type(2)+session(4)+checksum(4) */
#define RNET_HUB_HEADER_LEN 10

void rnet_transport_init(RNetTransport *t)
{
    if (t == NULL)
    {
        return;
    }
    memset(t, 0, sizeof(*t));
    t->sock = RNET_SOCKET_INVALID;
    t->mode = RNET_TRANSPORT_NONE;
}

void rnet_transport_shutdown(RNetTransport *t)
{
    if (t == NULL)
    {
        return;
    }
    if (t->mode == RNET_TRANSPORT_LAN_UDP)
    {
        rnet_os_socket_destroy(&t->sock);
    }
    t->mode = RNET_TRANSPORT_NONE;
    t->ice_send = NULL;
    t->ice_recv = NULL;
    t->ice_ctx = NULL;
    t->peer_known = 0;
    t->pending_peer_known = 0;
    t->accept_first_peer = 0;
    t->hub_mode = 0;
    memset(t->hub_ice_ctx, 0, sizeof(t->hub_ice_ctx));
    t->hub_rr = 0;
    memset(t->hub_slot_known, 0, sizeof(t->hub_slot_known));
    memset(t->hub_slot_addr, 0, sizeof(t->hub_slot_addr));
    rnet_netsim_destroy(t->netsim);
    t->netsim = NULL;
    t->netsim_probed = 0;
}

static int sockaddr_equal(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_family == b->sin_family && a->sin_port == b->sin_port &&
           a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static int lan_bind_socket(RNetTransport *t, const char *bind_hostport)
{
    char host[128];
    rnet_u16 port = 0;
    struct sockaddr_in bind_addr;

    if ((t == NULL) || (bind_hostport == NULL))
    {
        return -1;
    }
    rnet_transport_shutdown(t);
    rnet_os_startup();

    if (rnet_os_parse_hostport(bind_hostport, host, sizeof(host), &port) != 0)
    {
        return -1;
    }
    if (rnet_os_resolve_sockaddr(host, port, &bind_addr) != 0)
    {
        return -1;
    }

    t->sock = rnet_os_socket_create_dgram();
    if (!rnet_os_socket_valid(t->sock))
    {
        return -1;
    }
    (void)rnet_os_setsockopt_reuseaddr(t->sock, 1);
    (void)rnet_os_setsockopt_recvbuf(t->sock, 256 * 1024);
    if (rnet_os_bind(t->sock, &bind_addr) != 0)
    {
        rnet_os_socket_destroy(&t->sock);
        return -1;
    }
    if (rnet_os_set_nonblocking(t->sock) != 0)
    {
        rnet_os_socket_destroy(&t->sock);
        return -1;
    }
    t->mode = RNET_TRANSPORT_LAN_UDP;
    return 0;
}

int rnet_transport_start_lan(RNetTransport *t, const char *bind_hostport, const char *peer_hostport)
{
    char host[128];
    rnet_u16 port = 0;

    if (lan_bind_socket(t, bind_hostport) != 0)
    {
        return -1;
    }

    if (peer_hostport != NULL && peer_hostport[0] != '\0')
    {
        if (rnet_os_parse_hostport(peer_hostport, host, sizeof(host), &port) != 0 || port == 0)
        {
            rnet_os_socket_destroy(&t->sock);
            t->mode = RNET_TRANSPORT_NONE;
            return -1;
        }
        if (rnet_os_resolve_sockaddr(host, port, &t->peer) != 0)
        {
            rnet_os_socket_destroy(&t->sock);
            t->mode = RNET_TRANSPORT_NONE;
            return -1;
        }
        t->peer_known = 1;
    }
    else
    {
        t->peer_known = 0;
        t->accept_first_peer = 1;
    }
    return 0;
}

int rnet_transport_start_ice_hub(RNetTransport *t,
                                 int (*send_fn)(void *, const rnet_u8 *, size_t),
                                 int (*recv_fn)(void *, rnet_u8 *, size_t, size_t *),
                                 void *const ctx[RNET_MAX_SLOTS])
{
    int i;
    int any = 0;

    if (t == NULL || send_fn == NULL || recv_fn == NULL || ctx == NULL)
    {
        return -1;
    }
    rnet_transport_shutdown(t);
    for (i = 1; i < RNET_MAX_SLOTS; ++i)
    {
        t->hub_ice_ctx[i] = ctx[i];
        if (ctx[i] != NULL)
        {
            any = 1;
        }
    }
    if (!any)
    {
        return -1;
    }
    t->mode = RNET_TRANSPORT_ICE_HUB;
    t->ice_send = send_fn;
    t->ice_recv = recv_fn;
    t->hub_mode = 1;
    t->peer_known = 0;
    t->accept_first_peer = 0;
    return 0;
}

int rnet_transport_start_lan_hub(RNetTransport *t, const char *bind_hostport)
{
    if (lan_bind_socket(t, bind_hostport) != 0)
    {
        return -1;
    }
    t->hub_mode = 1;
    t->peer_known = 0;
    t->accept_first_peer = 0;
    return 0;
}

static int hub_packet_local_slot(const rnet_u8 *buf, int n, int *slot_out)
{
    rnet_u16 pkt_type;

    if (slot_out == NULL || n < RNET_HUB_MIN_PACKET)
    {
        return 0;
    }
    pkt_type = (rnet_u16)(buf[4] | ((rnet_u16)buf[5] << 8));
    /* START / DELAY_SYNC have no local_slot at body[0]. */
    if (pkt_type == RNET_PKT_START || pkt_type == RNET_PKT_DELAY_SYNC)
    {
        return 0;
    }
    *slot_out = (int)buf[RNET_HUB_HEADER_LEN];
    return 1;
}

static void hub_learn_and_forward(RNetTransport *t, const rnet_u8 *buf, int n, const struct sockaddr_in *src)
{
    int slot = -1;
    int i;

    if (t == NULL || buf == NULL || src == NULL || n < RNET_HUB_MIN_PACKET)
    {
        return;
    }

    if (hub_packet_local_slot(buf, n, &slot))
    {
        if (slot >= 0 && slot < RNET_MAX_SLOTS)
        {
            t->hub_slot_addr[slot] = *src;
            t->hub_slot_known[slot] = 1;
            t->peer_known = 1;
        }
    }
    else
    {
        /* Map by source if already known (START / DELAY_SYNC). */
        for (i = 0; i < RNET_MAX_SLOTS; ++i)
        {
            if (t->hub_slot_known[i] && sockaddr_equal(&t->hub_slot_addr[i], src))
            {
                slot = i;
                break;
            }
        }
    }

    for (i = 0; i < RNET_MAX_SLOTS; ++i)
    {
        if (!t->hub_slot_known[i])
        {
            continue;
        }
        if (sockaddr_equal(&t->hub_slot_addr[i], src))
        {
            continue;
        }
        (void)rnet_os_sendto(t->sock, buf, (size_t)n, &t->hub_slot_addr[i]);
    }
}

int rnet_transport_send(RNetTransport *t, const rnet_u8 *buf, size_t len)
{
    if ((t == NULL) || (buf == NULL) || (len == 0))
    {
        return -1;
    }
    if (t->mode == RNET_TRANSPORT_LAN_UDP)
    {
        int i;
        int sent = 0;
        int any = 0;

        if (!rnet_os_socket_valid(t->sock))
        {
            return -1;
        }
        if (t->hub_mode)
        {
            for (i = 0; i < RNET_MAX_SLOTS; ++i)
            {
                int r;
                if (!t->hub_slot_known[i])
                {
                    continue;
                }
                /* Never send host traffic back to seat 0 binding. */
                if (i == 0)
                {
                    continue;
                }
                r = rnet_os_sendto(t->sock, buf, len, &t->hub_slot_addr[i]);
                if (r >= 0)
                {
                    sent = r;
                    any = 1;
                }
            }
            return any ? sent : 0;
        }
        if (!t->peer_known)
        {
            return -1;
        }
        return rnet_os_sendto(t->sock, buf, len, &t->peer);
    }
    if (t->mode == RNET_TRANSPORT_ICE_HUB)
    {
        int i;
        int sent = 0;
        int any = 0;

        if (t->ice_send == NULL)
        {
            return -1;
        }
        for (i = 1; i < RNET_MAX_SLOTS; ++i)
        {
            int r;
            if (t->hub_ice_ctx[i] == NULL)
            {
                continue;
            }
            r = t->ice_send(t->hub_ice_ctx[i], buf, len);
            if (r >= 0)
            {
                sent = r;
                any = 1;
            }
        }
        return any ? sent : 0;
    }
    if (t->mode == RNET_TRANSPORT_ICE)
    {
        if (t->ice_send == NULL)
        {
            return -1;
        }
        return t->ice_send(t->ice_ctx, buf, len);
    }
    return -1;
}

/* One datagram straight off the wire. `out_src` is the sender when the mode
 * has one; `out_have_src` says whether it was filled (ICE has no sockaddr).
 *
 * Peer LEARNING deliberately does not happen here. Filtering does — a
 * datagram from a stranger is discarded on arrival, as before — but recording
 * the pending peer belongs to the datagram the session actually decodes, and
 * under the link simulator those are different datagrams. Leaving it here
 * would have let a packet still sitting in the hold queue name the peer. */
static int transport_recv_raw(RNetTransport *t, rnet_u8 *buf, size_t cap,
                              struct sockaddr_in *out_src, int *out_have_src)
{
    int would_block = 0;
    int n;
    struct sockaddr_in src;

    *out_have_src = 0;
    if (t->mode == RNET_TRANSPORT_LAN_UDP)
    {
        if (!rnet_os_socket_valid(t->sock))
        {
            return -1;
        }
        for (;;)
        {
            n = rnet_os_recvfrom(t->sock, buf, cap, &src, &would_block);
            if (n < 0)
            {
                return would_block ? 0 : -1;
            }
            if (t->hub_mode)
            {
                hub_learn_and_forward(t, buf, n, &src);
                *out_src = src;
                *out_have_src = 1;
                return n;
            }
            if (!t->peer_known || sockaddr_equal(&src, &t->peer))
            {
                *out_src = src;
                *out_have_src = 1;
                return n;
            }
        }
    }
    if (t->mode == RNET_TRANSPORT_ICE_HUB)
    {
        int k;

        if (t->ice_recv == NULL)
        {
            return -1;
        }
        /* Round-robin over seats so one chatty guest cannot starve the rest.
         * The source seat is the agent the datagram arrived on; the packet
         * body is never consulted, so a guest cannot claim another seat. */
        for (k = 0; k < RNET_MAX_SLOTS; ++k)
        {
            int i = (t->hub_rr + k) % RNET_MAX_SLOTS;
            size_t out_len = 0;
            int j;

            if (i == 0 || t->hub_ice_ctx[i] == NULL)
            {
                continue;
            }
            while (t->ice_recv(t->hub_ice_ctx[i], buf, cap, &out_len) == 0)
            {
                if (out_len < RNET_HUB_MIN_PACKET)
                {
                    continue;
                }
                t->hub_slot_known[i] = 1;
                t->peer_known = 1;
                for (j = 1; j < RNET_MAX_SLOTS; ++j)
                {
                    if (j == i || t->hub_ice_ctx[j] == NULL)
                    {
                        continue;
                    }
                    (void)t->ice_send(t->hub_ice_ctx[j], buf, out_len);
                }
                t->hub_rr = (i + 1) % RNET_MAX_SLOTS;
                return (int)out_len;
            }
        }
        return 0;
    }
    if (t->mode == RNET_TRANSPORT_ICE)
    {
        size_t out_len = 0;
        if (t->ice_recv == NULL)
        {
            return -1;
        }
        if (t->ice_recv(t->ice_ctx, buf, cap, &out_len) != 0)
        {
            return 0;
        }
        return (int)out_len;
    }
    return 0;
}

static void transport_note_peer(RNetTransport *t, const struct sockaddr_in *src,
                                int have_src)
{
    if (!have_src || t->hub_mode)
    {
        return;
    }
    if (t->accept_first_peer && !t->peer_known)
    {
        t->pending_peer = *src;
        t->pending_peer_known = 1;
    }
}

int rnet_transport_recv(RNetTransport *t, rnet_u8 *buf, size_t cap)
{
    struct sockaddr_in src;
    int have_src = 0;
    int n;

    if ((t == NULL) || (buf == NULL) || (cap == 0))
    {
        return -1;
    }
    if (t->mode == RNET_TRANSPORT_ICE_HUB)
    {
        /* Link simulator is not applied to the ICE hub. */
        return transport_recv_raw(t, buf, cap, &src, &have_src);
    }
    if (!t->netsim_probed)
    {
        t->netsim = rnet_netsim_create();
        t->netsim_probed = 1;
    }
    if (t->netsim == NULL)
    {
        n = transport_recv_raw(t, buf, cap, &src, &have_src);
        if (n > 0)
        {
            transport_note_peer(t, &src, have_src);
        }
        return n;
    }

    rnet_netsim_heartbeat(t->netsim);
    /* Drain the socket completely into the hold queue BEFORE delivering
     * anything. The caller stops its drain loop on the first 0, so a datagram
     * that is not due yet must never be what stands between the loop and one
     * that jitter made due earlier. */
    for (;;)
    {
        rnet_u8 tmp[RNET_MAX_PACKET];
        struct sockaddr_in tsrc;
        int thave = 0;
        int m = transport_recv_raw(t, tmp, sizeof(tmp), &tsrc, &thave);
        if (m <= 0)
        {
            /* A hard error is still a hard error, but only once the queue has
             * drained — otherwise a socket teardown would discard datagrams
             * that were already accepted. */
            if (m < 0)
            {
                n = rnet_netsim_due(t->netsim, buf, cap, &src);
                if (n > 0)
                {
                    transport_note_peer(t, &src, 1);
                    return n;
                }
                return -1;
            }
            break;
        }
        if (!rnet_netsim_hold(t->netsim, tmp, (size_t)m,
                              thave ? &tsrc : NULL))
        {
            /* Queue full (already announced). Delivering it undelayed beats
             * dropping it: the run's timings are compromised either way, and
             * silent loss would be read as a netcode defect. */
            if ((size_t)m <= cap)
            {
                memcpy(buf, tmp, (size_t)m);
                transport_note_peer(t, &tsrc, thave);
                return m;
            }
            break;
        }
    }

    n = rnet_netsim_due(t->netsim, buf, cap, &src);
    if (n > 0)
    {
        transport_note_peer(t, &src, 1);
    }
    return n;
}

void rnet_transport_accept_pending_peer(RNetTransport *t)
{
    if (t == NULL || t->hub_mode || !t->accept_first_peer || t->peer_known || !t->pending_peer_known)
    {
        return;
    }
    t->peer = t->pending_peer;
    t->peer_known = 1;
    t->pending_peer_known = 0;
}
