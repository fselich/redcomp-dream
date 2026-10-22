#include "recomp_net/recomp_net.h"
#include "protocol/rnet_protocol.h"
#include "transport/rnet_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
static unsigned test_pid(void) { return (unsigned)_getpid(); }
#else
#include <unistd.h>
static unsigned test_pid(void) { return (unsigned)getpid(); }
#endif

enum { kHashCapacity = 512, kStateBytes = 100000 };

typedef struct HostCtx
{
    rnet_u8 slot;
    int published;
    int overflow;
    rnet_u32 hashes[kHashCapacity];
} HostCtx;

static int g_failures;
static rnet_u64 g_now_ms = 1000;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    g_failures++;
}

static void sample_local(rnet_u32 tick, RNetInputSample *out, void *opaque)
{
    HostCtx *ctx = (HostCtx *)opaque;
    memset(out, 0, sizeof(*out));
    out->size = 4;
    out->bytes[0] = ctx->slot;
    out->bytes[1] = (rnet_u8)tick;
    out->bytes[2] = (rnet_u8)(tick >> 8);
    out->bytes[3] = (rnet_u8)(0xa5u ^ ctx->slot);
    out->valid = 1;
}

static void publish(rnet_u32 tick, const RNetInputSample *by_slot, int slots,
                    void *opaque)
{
    HostCtx *ctx = (HostCtx *)opaque;
    rnet_u32 hash = rnet_checksum(&tick, sizeof(tick));
    int slot;
    for (slot = 0; slot < slots; ++slot)
    {
        hash ^= rnet_checksum(&by_slot[slot], sizeof(by_slot[slot]));
        hash *= 16777619u;
    }
    if (ctx->published < kHashCapacity)
        ctx->hashes[ctx->published] = hash;
    else
        ctx->overflow = 1;
    ctx->published++;
}

static rnet_u64 now_ms(void *opaque)
{
    (void)opaque;
    return g_now_ms;
}

static void pump_pair(RNetSession *a, RNetSession *b)
{
    g_now_ms++;
    rnet_session_pump(a);
    rnet_session_pump(b);
}

static int wait_running(RNetSession *a, RNetSession *b)
{
    int i;
    for (i = 0; i < 3000; ++i)
    {
        pump_pair(a, b);
        if (rnet_session_is_running(a) && rnet_session_is_running(b)) return 1;
    }
    return 0;
}

static int drive_frames(RNetSession *a, RNetSession *b, HostCtx *ha, HostCtx *hb,
                        int target_a, int target_b)
{
    int i;
    for (i = 0; i < 3000; ++i)
    {
        pump_pair(a, b);
        if (ha->published < target_a)
        {
            rnet_u32 tick = rnet_session_sim_tick(a);
            if (rnet_session_try_admit(a, tick)) rnet_session_advance(a);
        }
        if (hb->published < target_b)
        {
            rnet_u32 tick = rnet_session_sim_tick(b);
            if (rnet_session_try_admit(b, tick)) rnet_session_advance(b);
        }
        if (ha->published >= target_a && hb->published >= target_b) return 1;
    }
    return 0;
}

static void compare_hashes(const HostCtx *a, const HostCtx *b, int first, int count)
{
    int i;
    for (i = first; i < first + count; ++i)
    {
        if (a->hashes[i] != b->hashes[i])
        {
            fail("published input hashes differ");
            return;
        }
    }
}

/* Two seats: probe miss -> LOAD transfer -> hard resync, strict pipeline. */
static void two_seat_load_test(void)
{
    RNetConfig ca;
    RNetConfig cb;
    RNetHostVTable va;
    RNetHostVTable vb;
    HostCtx ha;
    HostCtx hb;
    RNetSession *a = NULL;
    RNetSession *b = NULL;
    rnet_u8 *state = NULL;
    char bind_a[64];
    char bind_b[64];
    unsigned base_port = 40000U + (test_pid() % 9000U) * 2U;
    int i;
    int match = -1;
    int before_load;
    int guest_ready = 0;
    int host_ready = 0;

    memset(&ha, 0, sizeof(ha));
    memset(&hb, 0, sizeof(hb));
    ha.slot = 0;
    hb.slot = 1;
    memset(&va, 0, sizeof(va));
    memset(&vb, 0, sizeof(vb));
    va.sample_local = sample_local;
    va.publish = publish;
    va.now_ms = now_ms;
    va.ctx = &ha;
    vb.sample_local = sample_local;
    vb.publish = publish;
    vb.now_ms = now_ms;
    vb.ctx = &hb;

    rnet_config_init_defaults(&ca);
    cb = ca;
    ca.local_slot = 0;
    cb.local_slot = 1;
    ca.input_delay = cb.input_delay = 3;
    ca.session_id = cb.session_id = 0x53544154u ^ test_pid();
    snprintf(bind_a, sizeof(bind_a), "127.0.0.1:%u", base_port);
    snprintf(bind_b, sizeof(bind_b), "127.0.0.1:%u", base_port + 1U);

    a = rnet_session_create(&ca, &va);
    b = rnet_session_create(&cb, &vb);
    if (a == NULL || b == NULL)
    {
        fail("session create");
        goto done;
    }
    if (rnet_session_start_lan(a, bind_a, bind_b) != 0 ||
        rnet_session_start_lan(b, bind_b, bind_a) != 0)
    {
        fail("LAN start");
        goto done;
    }
    if (!wait_running(a, b))
    {
        fail("sessions did not reach running");
        goto done;
    }
    if (!drive_frames(a, b, &ha, &hb, 180, 180))
    {
        fail("pre-state pipeline stalled");
        goto done;
    }
    compare_hashes(&ha, &hb, 0, 180);

    state = (rnet_u8 *)malloc(kStateBytes);
    if (state == NULL)
    {
        fail("state allocation");
        goto done;
    }
    for (i = 0; i < kStateBytes; ++i) state[i] = (rnet_u8)(i * 37 + 11);

    if (rnet_session_state_probe(a, RNET_STATE_OP_LOAD, 2, kStateBytes,
                                 rnet_checksum(state, kStateBytes)) != 0)
    {
        fail("load probe start");
        goto done;
    }
    before_load = ha.published;
    for (i = 0; i < 3000; ++i)
    {
        rnet_u8 op;
        rnet_u8 slot;
        rnet_u32 size;
        rnet_u32 crc;
        pump_pair(a, b);
        if (rnet_session_state_probe_pending(b, &op, &slot, &size, &crc))
        {
            if (op != RNET_STATE_OP_LOAD || slot != 2 || size != kStateBytes ||
                crc != rnet_checksum(state, kStateBytes))
                fail("load probe metadata");
            if (rnet_session_state_probe_reply(b, 0) != 0) fail("load probe reply");
            break;
        }
    }
    if (i == 3000) fail("guest did not receive load probe");
    if (rnet_session_try_admit(a, rnet_session_sim_tick(a)) ||
        rnet_session_try_admit(b, rnet_session_sim_tick(b)))
        fail("load probe did not stall admission");
    if (ha.published != before_load || hb.published != before_load)
        fail("published during load probe");

    for (i = 0; i < 3000; ++i)
    {
        pump_pair(a, b);
        if (rnet_session_state_probe_take_reply(a, &match)) break;
    }
    if (i == 3000 || match != 0)
    {
        fail("host did not receive hash miss");
        goto done;
    }
    if (rnet_session_state_begin(a, RNET_STATE_OP_LOAD, 2, state, kStateBytes) != 0)
    {
        fail("state transfer start");
        goto done;
    }

    for (i = 0; i < 3000 && (!guest_ready || !host_ready); ++i)
    {
        rnet_u8 op;
        rnet_u8 slot;
        const void *data;
        size_t size;
        pump_pair(a, b);
        if (!guest_ready && rnet_session_state_take_ready(b, &op, &slot, &data, &size))
        {
            guest_ready = 1;
            if (op != RNET_STATE_OP_LOAD || slot != 2 || size != kStateBytes ||
                memcmp(data, state, kStateBytes) != 0)
                fail("guest state payload mismatch");
        }
        if (!host_ready && rnet_session_state_take_ready(a, &op, &slot, &data, &size))
            host_ready = 1;
        if (rnet_session_try_admit(a, rnet_session_sim_tick(a)) ||
            rnet_session_try_admit(b, rnet_session_sim_tick(b)))
            fail("state transfer did not stall admission");
    }
    if (!guest_ready || !host_ready)
    {
        fail("state transfer did not complete");
        goto done;
    }

    rnet_session_state_finish(a, 1);
    rnet_session_state_finish(b, 1);
    {
        const rnet_u8 pad_a[4] = { 0, 0x31, 0, 0xa5 };
        const rnet_u8 pad_b[4] = { 1, 0x72, 0, 0xa4 };
        rnet_session_prime_delay_inputs(a, pad_a, sizeof(pad_a));
        rnet_session_prime_delay_inputs(b, pad_b, sizeof(pad_b));
    }
    if (rnet_session_sim_tick(a) != 0 || rnet_session_sim_tick(b) != 0)
        fail("hard resync did not reset sim tick");
    if (!drive_frames(a, b, &ha, &hb, before_load + 80, before_load + 80))
    {
        fail("post-load pipeline stalled");
        goto done;
    }
    compare_hashes(&ha, &hb, before_load, 80);
    if (rnet_session_input_desync(a, NULL, NULL, NULL) ||
        rnet_session_input_desync(b, NULL, NULL, NULL))
        fail("post-load input desync");
    if (ha.overflow || hb.overflow) fail("publish hash capacity exceeded");

done:
    free(state);
    rnet_session_destroy(a);
    rnet_session_destroy(b);
    if (g_failures == 0)
        printf("session_state_test: 2-seat ok (strict pipeline + %u-byte load resync)\n",
               (unsigned)kStateBytes);
}

/* ------------------------------------------------------------------------
 * Multi-seat STATE (3-4 seats) over a lossy, reordering broadcast relay.
 *
 * Every seat's session talks to one relay socket, exactly like a lobby
 * relay or a host hub: whatever a seat sends reaches every other seat. The
 * relay can drop STATE_* datagrams per destination (seeded, so a failure
 * repeats), delay every datagram by 0..jitter ms (which reorders them), drop
 * the next N BEGINs or CHUNKs to one seat, and treat a seat as vanished
 * (not pumped, nothing delivered to it).
 *
 * The broadcast tests use only the single-peer API, so this file built with
 * -DRNET_STATE_TEST_LEGACY_ONLY runs them against the library before
 * multi-receiver STATE and shows what they catch there.
 * ---------------------------------------------------------------------- */

enum { kMaxSeats = 4, kQueueCap = 4096, kRoomPumpBudget = 40000 };

typedef struct QueuedPacket
{
    rnet_u64 due;
    int dst;
    int len;
    rnet_u8 buf[RNET_MAX_PACKET];
} QueuedPacket;

typedef struct Room
{
    int n;
    RNetSession *s[kMaxSeats];
    RNetTransport relay[kMaxSeats];
    HostCtx host[kMaxSeats];
    int vanished[kMaxSeats];
    rnet_u32 rng;
    int state_loss_pct;             /* STATE_* datagrams dropped per destination */
    int jitter_ms;                  /* each delivery delayed 0..jitter_ms */
    int drop_begin_to[kMaxSeats];   /* drop the next N STATE_BEGINs to seat */
    int drop_chunks_to[kMaxSeats];  /* drop the next N STATE_CHUNKs to seat */
    int drop_all_acks_to[kMaxSeats]; /* drop every STATE_ACK to seat */
    rnet_u32 drop_acks_from_at[kMaxSeats]; /* drop STATE_ACKs from seat with ack_bytes >= this (0 = off) */
    int drop_replies;               /* also drop PROBE_REPLY at state_loss_pct */
    int drop_input_src;
    int drop_input_dst;
    rnet_u32 drop_input_tick;
    rnet_u64 drop_input_until_ms;
    unsigned dropped_input;
    int dropped_reply_from[kMaxSeats];
    rnet_u32 first_begin_id[kMaxSeats]; /* first STATE_BEGIN xfer id per sender */
    unsigned dropped;
} Room;

static QueuedPacket g_queue[kQueueCap];
static int g_queue_len;
/* RNET_STATE_TEST_TRACE=<file>: log every datagram the relay sees (decoded
 * STATE fields, no session id), so two library builds can be diffed on the
 * same deterministic run -- e.g. to show two-seat traffic is unchanged. */
static FILE *g_trace;
static int g_trace_probed;

static FILE *trace_file(void)
{
    if (!g_trace_probed)
    {
        const char *path = getenv("RNET_STATE_TEST_TRACE");
        g_trace_probed = 1;
        if (path != NULL && path[0] != '\0')
            g_trace = fopen(path, "w");
    }
    return g_trace;
}

static void trace_packet(int src, const RNetDecodedPacket *p, int len)
{
    if (trace_file() == NULL)
        return;
    fprintf(g_trace, "t=%llu src=%d type=%u len=%d", (unsigned long long)g_now_ms, src, (unsigned)p->type, len);
    switch (p->type)
    {
    case RNET_PKT_STATE_BEGIN:
        fprintf(g_trace, " op=%u slot=%u id=%08x total=%u crc=%08x", p->state_op, p->state_slot,
                (unsigned)p->state_xfer_id, (unsigned)p->state_total_size, (unsigned)p->state_payload_crc);
        break;
    case RNET_PKT_STATE_CHUNK:
        fprintf(g_trace, " id=%08x off=%u size=%u", (unsigned)p->state_xfer_id, (unsigned)p->state_offset,
                (unsigned)p->state_chunk_size);
        break;
    case RNET_PKT_STATE_ACK:
        fprintf(g_trace, " id=%08x ack=%u", (unsigned)p->state_xfer_id, (unsigned)p->state_ack_bytes);
        break;
    case RNET_PKT_STATE_PROBE:
    case RNET_PKT_STATE_PROBE_REPLY:
        fprintf(g_trace, " op=%u slot=%u size=%u crc=%08x match=%u", p->state_op, p->state_slot,
                (unsigned)p->state_total_size, (unsigned)p->state_payload_crc, p->state_probe_match);
        break;
    default:
        break;
    }
    fputc('\n', g_trace);
}

static rnet_u32 room_rand(Room *r)
{
    rnet_u32 x = r->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->rng = x;
    return x;
}

static void room_deliver_due(Room *r)
{
    int i, w = 0;
    for (i = 0; i < g_queue_len; ++i)
    {
        if (g_queue[i].due <= g_now_ms)
        {
            if (!r->vanished[g_queue[i].dst])
                (void)rnet_transport_send(&r->relay[g_queue[i].dst], g_queue[i].buf,
                                          (size_t)g_queue[i].len);
            continue;
        }
        if (w != i)
            g_queue[w] = g_queue[i];
        w++;
    }
    g_queue_len = w;
}

static void room_forward(Room *r)
{
    int src;
    for (src = 0; src < r->n; ++src)
    {
        rnet_u8 buf[RNET_MAX_PACKET];
        int len;
        while ((len = rnet_transport_recv(&r->relay[src], buf, sizeof(buf))) > 0)
        {
            RNetDecodedPacket pkt;
            int type = -1;
            int dst;
            if (rnet_proto_decode(buf, (size_t)len, 0x524E4554u, &pkt) == 0)
            {
                type = (int)pkt.type;
                trace_packet(src, &pkt, len);
            }
            if (type == RNET_PKT_STATE_BEGIN && r->first_begin_id[src] == 0)
                r->first_begin_id[src] = pkt.state_xfer_id;
            for (dst = 0; dst < r->n; ++dst)
            {
                int is_state = type >= RNET_PKT_STATE_BEGIN && type <= RNET_PKT_STATE_PROBE_REPLY;
                if (dst == src || r->vanished[dst])
                    continue;
                if (type == RNET_PKT_INPUT && src == r->drop_input_src &&
                    dst == r->drop_input_dst && g_now_ms < r->drop_input_until_ms)
                {
                    int row;
                    int has_missing_row = 0;
                    for (row = 0; row < pkt.frame_count; ++row)
                        if (pkt.frames[row].tick == r->drop_input_tick)
                            has_missing_row = 1;
                    if (has_missing_row)
                    {
                        r->dropped_input++;
                        continue;
                    }
                }
                if (type == RNET_PKT_STATE_BEGIN && r->drop_begin_to[dst] > 0)
                {
                    r->drop_begin_to[dst]--;
                    r->dropped++;
                    continue;
                }
                if (type == RNET_PKT_STATE_CHUNK && r->drop_chunks_to[dst] > 0)
                {
                    r->drop_chunks_to[dst]--;
                    r->dropped++;
                    continue;
                }
                if (type == RNET_PKT_STATE_ACK &&
                    (r->drop_all_acks_to[dst] ||
                     (r->drop_acks_from_at[src] != 0u && pkt.state_ack_bytes >= r->drop_acks_from_at[src])))
                {
                    r->dropped++;
                    continue;
                }
                if (type == RNET_PKT_STATE_PROBE_REPLY && !r->drop_replies)
                    is_state = 0;
                if (is_state && (int)(room_rand(r) % 100u) < r->state_loss_pct)
                {
                    if (type == RNET_PKT_STATE_PROBE_REPLY)
                        r->dropped_reply_from[src]++;
                    r->dropped++;
                    continue;
                }
                if (g_queue_len >= kQueueCap)
                {
                    r->dropped++;
                    continue;
                }
                g_queue[g_queue_len].due =
                    g_now_ms + (r->jitter_ms > 0 ? room_rand(r) % (rnet_u32)(r->jitter_ms + 1) : 0u);
                g_queue[g_queue_len].dst = dst;
                g_queue[g_queue_len].len = len;
                memcpy(g_queue[g_queue_len].buf, buf, (size_t)len);
                g_queue_len++;
            }
        }
    }
    room_deliver_due(r);
}

static void room_pump(Room *r)
{
    int i;
    g_now_ms++;
    room_forward(r);
    for (i = 0; i < r->n; ++i)
        if (!r->vanished[i])
            rnet_session_pump(r->s[i]);
    room_forward(r);
}

static void room_close(Room *r)
{
    int i;
    for (i = 0; i < r->n; ++i)
    {
        rnet_session_destroy(r->s[i]);
        rnet_transport_shutdown(&r->relay[i]);
    }
    r->n = 0;
    g_queue_len = 0;
}

/* `seats` = slot_count; `occupied` = RNetConfig.occupied_mask (0 = every
 * seat). An empty seat has no session at all and counts as vanished. With
 * `observer`, one more session (index `seats`) joins as a spectator: its
 * local_slot is the observer sentinel and its wire slot is `seats`. */
static int room_open_ex(Room *r, int seats, rnet_u32 seed, rnet_u32 occupied, int observer)
{
    unsigned base = 20000U + (test_pid() % 1000U) * 16U;
    int n = seats + (observer ? 1 : 0);
    int i;
    memset(r, 0, sizeof(*r));
    g_queue_len = 0;
    r->rng = seed ? seed : 0x9e3779b9u;
    if (trace_file() != NULL)
        fprintf(g_trace, "room seats=%d seed=%u occupied=%x\n", n, (unsigned)seed, (unsigned)occupied);
    for (i = 0; i < n; ++i)
    {
        RNetConfig cfg;
        RNetHostVTable vt;
        char bind[64], proxy[64];
        r->n = i + 1;
        rnet_transport_init(&r->relay[i]);
        if (occupied != 0u && !(occupied & (1u << i)))
        {
            r->vanished[i] = 1;
            continue;
        }
        rnet_config_init_defaults(&cfg);
        cfg.slot_count = (rnet_u8)seats;
        cfg.local_slot = (rnet_u8)i; /* i == seats: the observer sentinel */
        if (i == seats)
            cfg.wire_slot = (rnet_u8)seats;
        cfg.input_delay = 3;
        cfg.occupied_mask = occupied;
        cfg.session_id = 0x4d534154u ^ test_pid() ^ (seed << 8);
        r->host[i].slot = (rnet_u8)i;
        memset(&vt, 0, sizeof(vt));
        vt.sample_local = sample_local;
        vt.publish = publish;
        vt.now_ms = now_ms;
        vt.ctx = &r->host[i];
        r->s[i] = rnet_session_create(&cfg, &vt);
        if (r->s[i] == NULL)
        {
            fail("room: session create");
            return 0;
        }
        snprintf(bind, sizeof(bind), "127.0.0.1:%u", base + (unsigned)i);
        snprintf(proxy, sizeof(proxy), "127.0.0.1:%u", base + 8U + (unsigned)i);
        if (rnet_transport_start_lan(&r->relay[i], proxy, bind) != 0 ||
            rnet_session_start_lan(r->s[i], bind, proxy) != 0)
        {
            fail("room: LAN start");
            return 0;
        }
    }
    for (i = 0; i < 5000; ++i)
    {
        int k, all = 1;
        room_pump(r);
        for (k = 0; k < n; ++k)
            all = all && (r->s[k] == NULL || rnet_session_is_running(r->s[k]));
        if (all)
            return 1;
    }
    fail("room: seats did not reach running");
    return 0;
}

#ifndef RNET_STATE_TEST_LEGACY_ONLY
static int room_open_mask(Room *r, int n, rnet_u32 seed, rnet_u32 occupied)
{
    return room_open_ex(r, n, seed, occupied, 0);
}
#endif

static int room_open(Room *r, int n, rnet_u32 seed)
{
    return room_open_ex(r, n, seed, 0u, 0);
}

/* Every live seat admits `frames` more ticks; published inputs must agree. */
static int room_drive(Room *r, int frames)
{
    int target[kMaxSeats];
    int first = r->host[0].published;
    int i, k;
    for (k = 0; k < r->n; ++k)
        target[k] = r->host[k].published + frames;
    for (i = 0; i < kRoomPumpBudget; ++i)
    {
        int done = 1;
        room_pump(r);
        for (k = 0; k < r->n; ++k)
        {
            if (r->vanished[k] || r->host[k].published >= target[k])
                continue;
            done = 0;
            if (rnet_session_try_admit(r->s[k], rnet_session_sim_tick(r->s[k])))
                rnet_session_advance(r->s[k]);
        }
        if (done)
            break;
    }
    if (i == kRoomPumpBudget)
    {
        fail("room: pipeline stalled");
        return 0;
    }
    for (k = 1; k < r->n; ++k)
    {
        int t;
        if (r->vanished[k] || r->host[k].published != r->host[0].published)
            continue;
        for (t = first; t < first + frames && t < kHashCapacity; ++t)
        {
            if (r->host[k].hashes[t] != r->host[0].hashes[t])
            {
                fail("room: published input hashes differ across seats");
                return 0;
            }
        }
    }
    return 1;
}

/* Hold one source row away from one receiver while other seats advance.
 * Their later acknowledgments must not suppress retransmission to the seat
 * with the hole. Both the relay schedule and loss window are deterministic. */
static void multi_seat_input_hole_test(int seats, rnet_u32 seed)
{
    Room r;
    int before = g_failures;
    if (!room_open(&r, seats, seed))
        goto out;
    if (!room_drive(&r, 12))
        goto out;
    r.drop_input_src = 0;
    r.drop_input_dst = 1;
    r.drop_input_tick = rnet_session_sim_tick(r.s[0]) + 5u;
    r.drop_input_until_ms = g_now_ms + 180u;
    if (!room_drive(&r, 35))
        goto out;
    if (r.dropped_input == 0)
        fail("input hole: target row was never dropped");
    printf("session_state_test: %d-seat input hole (seed %u, %u drops) %s\n",
           seats, (unsigned)seed, r.dropped_input,
           g_failures != before ? "FAILED" : "ok");
out:
    room_close(&r);
}

static void fill_pattern(rnet_u8 *p, size_t n, unsigned salt)
{
    size_t i;
    for (i = 0; i < n; ++i)
        p[i] = (rnet_u8)(i * 131u + salt * 17u + (i >> 9));
}

/* Host -> every guest broadcast, legacy single-peer API only. The host must
 * not report completion until every guest holds the whole blob, and every
 * guest must get it -- including one that lost the first BEGINs and one that
 * lost a run of chunks the others received. */
static void multi_seat_broadcast_test(int n, int loss_pct, rnet_u8 op, rnet_u32 seed)
{
    Room r;
    rnet_u8 *blob = NULL;
    int guest_ready[kMaxSeats] = { 0 };
    int host_ready = 0;
    int failures_before = g_failures;
    int i, k;
    char what[96];

    if (!room_open(&r, n, seed))
        goto out;
    if (!room_drive(&r, 20))
        goto out;
    blob = (rnet_u8 *)malloc(kStateBytes);
    if (blob == NULL)
    {
        fail("broadcast: alloc");
        goto out;
    }
    fill_pattern(blob, kStateBytes, seed);
    r.state_loss_pct = loss_pct;
    r.jitter_ms = 3;
    r.drop_begin_to[n - 1] = 3;  /* last seat misses the first three BEGINs */
    if (n > 2)
        r.drop_chunks_to[1] = 40; /* seat 1 loses a run the others receive */
    if (rnet_session_state_begin(r.s[0], op, 0, blob, kStateBytes) != 0)
    {
        fail("broadcast: begin");
        goto out;
    }
    for (i = 0; i < kRoomPumpBudget && !host_ready; ++i)
    {
        rnet_u8 got_op, got_slot;
        const void *data;
        size_t size;
        room_pump(&r);
        for (k = 1; k < n; ++k)
        {
            if (!guest_ready[k] && rnet_session_state_take_ready(r.s[k], &got_op, &got_slot, &data, &size))
            {
                guest_ready[k] = 1;
                if (got_op != op || got_slot != 0 || size != kStateBytes || memcmp(data, blob, size) != 0)
                    fail("broadcast: guest payload mismatch");
            }
        }
        if (rnet_session_state_take_ready(r.s[0], &got_op, &got_slot, &data, &size))
        {
            host_ready = 1;
            for (k = 1; k < n; ++k)
            {
                if (!guest_ready[k])
                {
                    snprintf(what, sizeof(what),
                             "broadcast %d seats: host completed before seat %d had the blob", n, k);
                    fail(what);
                }
            }
        }
        /* A seat with the transfer open never admits. (A guest that has not
         * seen BEGIN yet may run out the D ticks of input it already holds,
         * as with two seats.) */
        for (k = 0; k < n; ++k)
        {
            int busy = rnet_session_state_busy(r.s[k]);
            if (rnet_session_try_admit(r.s[k], rnet_session_sim_tick(r.s[k])))
            {
                if (busy)
                {
                    snprintf(what, sizeof(what), "broadcast %d seats: seat %d admitted with the transfer open", n, k);
                    fail(what);
                }
                rnet_session_advance(r.s[k]);
            }
        }
    }
    /* A guest the host abandoned would still be waiting: give it time. */
    for (i = 0; i < 3000; ++i)
    {
        int all = 1;
        for (k = 1; k < n; ++k)
        {
            rnet_u8 got_op, got_slot;
            const void *data;
            size_t size;
            if (!guest_ready[k] && rnet_session_state_take_ready(r.s[k], &got_op, &got_slot, &data, &size))
                guest_ready[k] = 1;
            all = all && guest_ready[k];
        }
        if (all)
            break;
        room_pump(&r);
    }
    if (!host_ready)
    {
        snprintf(what, sizeof(what), "broadcast %d seats: host never completed", n);
        fail(what);
    }
    for (k = 1; k < n; ++k)
    {
        if (!guest_ready[k])
        {
            snprintf(what, sizeof(what), "broadcast %d seats: seat %d never received the blob", n, k);
            fail(what);
        }
    }
    for (k = 0; k < n; ++k)
        rnet_session_state_finish(r.s[k], 0);
    r.state_loss_pct = 0;
    (void)room_drive(&r, 30); /* the room resumes and still agrees */
    printf("session_state_test: %d-seat %s broadcast (%d%% STATE loss, %u dropped) %s\n", n,
           op == RNET_STATE_OP_BOOT ? "BOOT" : "SRAM", loss_pct, r.dropped,
           g_failures != failures_before ? "FAILED" : "ok");
out:
    free(blob);
    room_close(&r);
}

#ifndef RNET_STATE_TEST_LEGACY_ONLY
static int popcount32(rnet_u32 v)
{
    int c = 0;
    while (v)
    {
        v &= v - 1u;
        c++;
    }
    return c;
}

/* Every guest uploads its MEMCARD at once; the host collects one per guest
 * (per-source receive). */
static void multi_seat_memcard_test(int n, int loss_pct, rnet_u32 seed)
{
    Room r;
    rnet_u8 *cards[kMaxSeats] = { 0 };
    size_t card_size[kMaxSeats] = { 0 };
    int host_got[kMaxSeats] = { 0 };
    int guest_done[kMaxSeats] = { 0 };
    int max_concurrent = 0;
    int failures_before = g_failures;
    int i, k;
    char what[96];

    if (!room_open(&r, n, seed))
        goto out;
    if (!room_drive(&r, 20))
        goto out;
    for (k = 1; k < n; ++k)
    {
        card_size[k] = 20000u + (size_t)k * 7777u;
        cards[k] = (rnet_u8 *)malloc(card_size[k]);
        if (cards[k] == NULL)
        {
            fail("memcard: alloc");
            goto out;
        }
        fill_pattern(cards[k], card_size[k], 0x40u + (unsigned)k);
    }
    r.state_loss_pct = loss_pct;
    r.jitter_ms = 3;
    if (n > 2)
        r.drop_begin_to[0] = 2; /* the host misses the first uploads' BEGINs */
    for (k = 1; k < n; ++k)
    {
        if (rnet_session_state_begin(r.s[k], RNET_STATE_OP_MEMCARD, (rnet_u8)k, cards[k], card_size[k]) != 0)
        {
            fail("memcard: guest begin");
            goto out;
        }
    }
    for (i = 0; i < kRoomPumpBudget; ++i)
    {
        int all = 1;
        int from;
        rnet_u8 op, slot;
        const void *data;
        size_t size;
        room_pump(&r);
        k = popcount32(rnet_session_state_inbound_mask(r.s[0]));
        if (k > max_concurrent)
            max_concurrent = k;
        while (rnet_session_state_take_ready_from(r.s[0], &from, &op, &slot, &data, &size))
        {
            if (from < 1 || from >= n)
            {
                fail("memcard: host take_ready_from reported a bad source");
                break;
            }
            if (op != RNET_STATE_OP_MEMCARD || slot != (rnet_u8)from || size != card_size[from] ||
                memcmp(data, cards[from], size) != 0)
                fail("memcard: host received a wrong card");
            host_got[from]++;
            rnet_session_state_finish_from(r.s[0], from, 0);
        }
        for (k = 1; k < n; ++k)
        {
            if (rnet_session_state_inbound_mask(r.s[k]) != 0u)
                fail("memcard: a guest opened a receive for another guest's upload");
            if (!guest_done[k] && rnet_session_state_take_ready_from(r.s[k], &from, &op, &slot, &data, &size))
            {
                if (from != k || op != RNET_STATE_OP_MEMCARD)
                    fail("memcard: guest completion reported the wrong transfer");
                guest_done[k] = 1;
                rnet_session_state_finish(r.s[k], 0);
            }
            all = all && guest_done[k] && host_got[k];
        }
        if (all)
            break;
    }
    for (k = 1; k < n; ++k)
    {
        if (host_got[k] != 1)
        {
            snprintf(what, sizeof(what), "memcard %d seats: host got seat %d's card %d times", n, k, host_got[k]);
            fail(what);
        }
        if (!guest_done[k])
        {
            snprintf(what, sizeof(what), "memcard %d seats: seat %d upload never completed", n, k);
            fail(what);
        }
    }
    /* Distinct id namespaces: the host's ACK names only the xfer id and is
     * broadcast, so concurrent uploads must never share one. Seat 1's id is
     * the pre-multi-seat value (bit 30 | serial): two seats' wire unchanged. */
    if (r.first_begin_id[1] != 0x40000001u)
        fail("memcard: seat 1 upload id changed from the two-seat value");
    for (k = 2; k < n; ++k)
    {
        if (r.first_begin_id[k] == r.first_begin_id[1] ||
            ((r.first_begin_id[k] >> 24) & 0x3fu) != (rnet_u32)(k - 1))
            fail("memcard: upload ids do not carry the seat");
    }
    if (n > 2 && max_concurrent < 2)
        fail("memcard: uploads never overlapped (test did not exercise concurrency)");
    r.state_loss_pct = 0;
    (void)room_drive(&r, 20);
    printf("session_state_test: %d-seat MEMCARD uploads (%d%% loss, max %d concurrent, %u dropped) %s\n", n,
           loss_pct, max_concurrent, r.dropped, g_failures != failures_before ? "FAILED" : "ok");
out:
    for (k = 0; k < kMaxSeats; ++k)
        free(cards[k]);
    room_close(&r);
}

/* Upload superseded by the host's reply (the gbarecomp startup shape, now
 * with several guests): each guest's MEMCARD upload, then the host's BOOT to
 * all. A guest whose final upload ACK is lost sees BOOT replace its upload
 * (single-receiver supersede) and must still get BOOT. The host's BOOT has
 * several receivers and must NOT be replaced by anyone's BEGIN. */
static void multi_seat_upload_then_boot_test(int n, rnet_u32 seed)
{
    Room r;
    rnet_u8 card[6000];
    rnet_u8 boot[40000];
    int host_got[kMaxSeats] = { 0 };
    int boot_ready[kMaxSeats] = { 0 };
    int upload_done[kMaxSeats] = { 0 };
    int host_boot_done = 0;
    int begun = 0;
    int failures_before = g_failures;
    int i, k;

    if (!room_open(&r, n, seed))
        goto out;
    if (!room_drive(&r, 10))
        goto out;
    fill_pattern(boot, sizeof(boot), 99);
    r.state_loss_pct = 15;
    r.jitter_ms = 3;
    /* Seat 1 never hears an ACK for its upload: only BOOT superseding it can
     * close its outbound transfer. */
    r.drop_all_acks_to[1] = 1;
    for (k = 1; k < n; ++k)
    {
        fill_pattern(card, sizeof(card), (unsigned)k);
        if (rnet_session_state_begin(r.s[k], RNET_STATE_OP_MEMCARD, (rnet_u8)k, card, sizeof(card)) != 0)
            fail("upload/boot: guest begin");
    }
    for (i = 0; i < kRoomPumpBudget; ++i)
    {
        int all = 1;
        int from;
        rnet_u8 op, slot;
        room_pump(&r);
        while (rnet_session_state_take_ready_from(r.s[0], &from, &op, &slot, NULL, NULL))
        {
            if (from == 0)
            {
                if (op != RNET_STATE_OP_BOOT)
                    fail("upload/boot: host outbound completion is not BOOT");
                host_boot_done = 1;
                rnet_session_state_finish_from(r.s[0], 0, 0);
                continue;
            }
            if (op != RNET_STATE_OP_MEMCARD)
                fail("upload/boot: host inbound is not MEMCARD");
            host_got[from]++;
            rnet_session_state_finish_from(r.s[0], from, 0);
        }
        if (!begun)
        {
            int have_all = 1;
            for (k = 1; k < n; ++k)
                have_all = have_all && host_got[k];
            if (have_all && rnet_session_state_begin(r.s[0], RNET_STATE_OP_BOOT, 0, boot, sizeof(boot)) == 0)
                begun = 1;
        }
        for (k = 1; k < n; ++k)
        {
            while (rnet_session_state_take_ready_from(r.s[k], &from, &op, &slot, NULL, NULL))
            {
                if (from == k && op == RNET_STATE_OP_MEMCARD)
                {
                    upload_done[k]++;
                    rnet_session_state_finish(r.s[k], 0);
                    continue;
                }
                if (from != 0 || op != RNET_STATE_OP_BOOT)
                    fail("upload/boot: guest got an unexpected transfer");
                boot_ready[k] = 1;
                rnet_session_state_finish(r.s[k], 0);
            }
            all = all && boot_ready[k];
        }
        if (all && host_boot_done)
            break;
    }
    for (k = 1; k < n; ++k)
    {
        if (host_got[k] != 1)
            fail("upload/boot: host did not get exactly one card per guest");
        if (!boot_ready[k])
            fail("upload/boot: a guest never got BOOT");
    }
    if (!host_boot_done)
        fail("upload/boot: host BOOT never completed");
    if (upload_done[1] != 0)
        fail("upload/boot: seat 1 saw an upload completion it never got an ACK for");
    for (k = 0; k < n; ++k)
    {
        if (rnet_session_state_busy(r.s[k]))
            fail("upload/boot: a seat is still busy afterwards");
    }
    r.state_loss_pct = 0;
    (void)room_drive(&r, 20);
    printf("session_state_test: %d-seat MEMCARD uploads -> BOOT %s\n", n,
           g_failures != failures_before ? "FAILED" : "ok");
out:
    room_close(&r);
}

/* N-party ready barrier (LOAD size==0) with staggered answers, then a hash
 * probe with mixed answers. The host's barrier opens only when every seat
 * answered; a seat that answered is not handed the retransmits again. */
static void multi_seat_probe_test(int n, int loss_pct, rnet_u32 seed)
{
    Room r;
    int replied[kMaxSeats] = { 0 };
    int raised[kMaxSeats] = { 0 };
    int first_seen = -1;
    int opened = 0;
    int failures_before = g_failures;
    int i, k;
    rnet_u32 expect = 0, got = 0, match = 0;
    int m;
    char what[128];

    if (!room_open(&r, n, seed))
        goto out;
    if (!room_drive(&r, 20))
        goto out;
    r.state_loss_pct = loss_pct;
    r.drop_replies = 1;
    r.jitter_ms = 3;
    if (rnet_session_state_probe(r.s[0], RNET_STATE_OP_LOAD, 0, 0, 0x52454459u) != 0)
    {
        fail("probe: host probe start");
        goto out;
    }
    if (!rnet_session_state_probe_replies(r.s[0], &expect, &got, &match) ||
        expect != ((1u << n) - 2u) || got != 0u)
        fail("probe: expected-seat mask is not every guest");
    for (i = 0; i < kRoomPumpBudget && !opened; ++i)
    {
        room_pump(&r);
        for (k = 1; k < n; ++k)
        {
            rnet_u8 op, slot;
            rnet_u32 size, crc;
            if (!rnet_session_state_probe_pending(r.s[k], &op, &slot, &size, &crc))
                continue;
            if (op != RNET_STATE_OP_LOAD || size != 0 || crc != 0x52454459u)
                fail("probe: guest saw wrong probe metadata");
            if (first_seen < 0)
                first_seen = i;
            /* Seat k answers (k-1)*60 ms after the first seat saw the probe. */
            if (i - first_seen < (k - 1) * 60)
                continue;
            raised[k]++;
            replied[k] = 1;
            if (rnet_session_state_probe_reply(r.s[k], 1) != 0)
                fail("probe: guest reply");
        }
        if (rnet_session_state_probe_take_reply(r.s[0], &m))
        {
            opened = 1;
            for (k = 1; k < n; ++k)
            {
                if (!replied[k])
                {
                    snprintf(what, sizeof(what), "probe %d seats: barrier opened before seat %d answered", n, k);
                    fail(what);
                }
            }
            if (m != 1)
                fail("probe: all-match barrier reported a miss");
        }
    }
    if (!opened)
    {
        snprintf(what, sizeof(what), "probe %d seats: ready barrier never opened", n);
        fail(what);
        goto out;
    }
    for (k = 1; k < n; ++k)
    {
        if (!rnet_session_state_probe_take_reply_from(r.s[0], k, &m) || m != 1)
            fail("probe: per-seat reply missing");
    }
    /* Keep the host probe open a while: it must stop retransmitting, and no
     * seat may be handed the answered probe again. */
    for (i = 0; i < 200; ++i)
    {
        room_pump(&r);
        for (k = 1; k < n; ++k)
        {
            if (rnet_session_state_probe_pending(r.s[k], NULL, NULL, NULL, NULL))
            {
                raised[k]++;
                (void)rnet_session_state_probe_reply(r.s[k], 1);
            }
        }
    }
    for (k = 1; k < n; ++k)
    {
        /* One raise, plus at most one per reply the relay dropped and a
         * couple for probes already in flight when the reply landed. Without
         * the replied mask a fast seat is re-raised every 8 ms until the
         * slowest one answers. */
        if (raised[k] > 3 + r.dropped_reply_from[k])
        {
            snprintf(what, sizeof(what), "probe %d seats: seat %d raised %d times (%d replies dropped)", n, k,
                     raised[k], r.dropped_reply_from[k]);
            fail(what);
        }
    }
    rnet_session_state_probe_finish(r.s[0]);

    /* Hash probe, mixed answers: seat 1 has the blob, the rest do not. */
    r.state_loss_pct = 0;
    if (rnet_session_state_probe(r.s[0], RNET_STATE_OP_SRAM, 0, 5000u, 0xabcdef01u) != 0)
    {
        fail("probe: hash probe start");
        goto out;
    }
    memset(replied, 0, sizeof(replied));
    opened = 0;
    for (i = 0; i < kRoomPumpBudget && !opened; ++i)
    {
        room_pump(&r);
        for (k = 1; k < n; ++k)
        {
            if (!replied[k] && rnet_session_state_probe_pending(r.s[k], NULL, NULL, NULL, NULL))
            {
                replied[k] = 1;
                (void)rnet_session_state_probe_reply(r.s[k], k == 1 ? 1 : 0);
            }
        }
        if (rnet_session_state_probe_take_reply(r.s[0], &m))
        {
            opened = 1;
            if (n > 2 && m != 0)
                fail("probe: a miss on one seat must make the barrier a miss");
            if (n == 2 && m != 1)
                fail("probe: two-seat all-match reported a miss");
        }
        if (rnet_session_try_admit(r.s[0], rnet_session_sim_tick(r.s[0])))
        {
            fail("probe: hash probe did not stall the host");
            rnet_session_advance(r.s[0]);
        }
    }
    if (!opened || !rnet_session_state_probe_replies(r.s[0], &expect, &got, &match) || got != expect ||
        match != 2u)
        fail("probe: per-seat match mask wrong");
    if (!rnet_session_state_probe_take_reply_from(r.s[0], 1, &m) || m != 1)
        fail("probe: seat 1 match lost");
    if (n > 2 && (!rnet_session_state_probe_take_reply_from(r.s[0], 2, &m) || m != 0))
        fail("probe: seat 2 miss lost");
    {
        /* Hash-miss path: the transfer that follows reaches every seat
         * (including the one that matched) and unstalls all of them. */
        rnet_u8 blob[5000];
        int ready[kMaxSeats] = { 0 };
        int all = 0;
        fill_pattern(blob, sizeof(blob), 7);
        if (rnet_session_state_begin(r.s[0], RNET_STATE_OP_SRAM, 0, blob, sizeof(blob)) != 0)
            fail("probe: transfer after miss");
        for (i = 0; i < kRoomPumpBudget && !all; ++i)
        {
            room_pump(&r);
            all = 1;
            for (k = 0; k < n; ++k)
            {
                if (!ready[k] && rnet_session_state_take_ready(r.s[k], NULL, NULL, NULL, NULL))
                {
                    ready[k] = 1;
                    rnet_session_state_finish(r.s[k], 0);
                }
                all = all && ready[k];
            }
        }
        if (!all)
            fail("probe: transfer after miss did not complete");
        (void)room_drive(&r, 20);
    }
    printf("session_state_test: %d-seat probe barrier (%d%% loss incl. replies) %s\n", n, loss_pct,
           g_failures != failures_before ? "FAILED" : "ok");
out:
    room_close(&r);
}

/* Per-seat liveness and dropping a vanished seat from a transfer / probe. */
static void multi_seat_liveness_test(void)
{
    Room r;
    rnet_u8 blob[30000];
    int failures_before = g_failures;
    int i, k, m;
    rnet_u32 acked = 0, total = 0;
    RNetSessionStats st;

    if (!room_open(&r, 4, 0x1234u))
        goto out;
    if (!room_drive(&r, 20))
        goto out;
    for (k = 1; k < 4; ++k)
    {
        if (rnet_session_peer_rx_age_ms(r.s[0], k) > 10u)
            fail("liveness: live seat has a stale rx age");
    }
    if (rnet_session_peer_rx_age_ms(r.s[0], 0) != RNET_PEER_RX_NEVER)
        fail("liveness: own seat must answer NEVER");
    /* Seat 2 vanishes: nothing from it, nothing to it. */
    r.vanished[2] = 1;
    for (i = 0; i < 200; ++i)
        room_pump(&r);
    if (rnet_session_peer_rx_age_ms(r.s[0], 2) < 190u)
        fail("liveness: vanished seat's rx age did not grow");
    if (rnet_session_peer_rx_age_ms(r.s[0], 1) > 10u || rnet_session_peer_rx_age_ms(r.s[0], 3) > 10u)
        fail("liveness: live seats' rx age grew with the vanished one");
    if (!rnet_session_peer_slot_disconnected(r.s[0], 2, 100) ||
        rnet_session_peer_slot_disconnected(r.s[0], 1, 100) ||
        rnet_session_disconnected_peers(r.s[0], 100) != (1u << 2) ||
        rnet_session_disconnected_peers(r.s[1], 100) != (1u << 2))
        fail("liveness: per-seat silence timeout wrong");
    rnet_session_get_stats(r.s[0], &st);
    if (st.peer_rx_age_ms[2] < 190u || st.peer_rx_age_ms[1] > 10u)
        fail("liveness: stats per-seat rx age wrong");

    /* A transfer waits on the vanished seat until the host drops it. */
    fill_pattern(blob, sizeof(blob), 3);
    if (rnet_session_state_begin(r.s[0], RNET_STATE_OP_BOOT, 0, blob, sizeof(blob)) != 0)
        fail("liveness: begin");
    for (i = 0; i < 400; ++i)
    {
        room_pump(&r);
        for (k = 1; k < 4; ++k)
            if (k != 2 && rnet_session_state_take_ready(r.s[k], NULL, NULL, NULL, NULL))
                rnet_session_state_finish(r.s[k], 0);
    }
    if (rnet_session_state_take_ready(r.s[0], NULL, NULL, NULL, NULL))
        fail("liveness: transfer completed without the vanished seat");
    if (rnet_session_state_pending_receivers(r.s[0]) != (1u << 2))
        fail("liveness: pending receivers should be the vanished seat alone");
    if (!rnet_session_state_progress(r.s[0], 2, &acked, &total) || acked != 0 || total != sizeof(blob))
        fail("liveness: vanished seat's progress wrong");
    if (!rnet_session_state_progress(r.s[0], 1, &acked, &total) || acked != sizeof(blob))
        fail("liveness: live seat's progress wrong");
    rnet_session_get_stats(r.s[0], &st);
    if (st.state_expect_mask != 0x0eu || st.state_done_mask != 0x0au || st.state_bytes_acked != 0)
        fail("liveness: stats state masks wrong");
    if (!rnet_session_state_drop_peer(r.s[0], 2) ||
        !rnet_session_state_take_ready(r.s[0], NULL, NULL, NULL, NULL))
        fail("liveness: dropping the vanished seat did not complete the transfer");
    rnet_session_state_finish(r.s[0], 0);

    /* Same for a probe barrier. */
    if (rnet_session_state_probe(r.s[0], RNET_STATE_OP_BOOT, 0, 0, 0x424f4f54u) != 0)
        fail("liveness: probe start");
    for (i = 0; i < 200; ++i)
    {
        room_pump(&r);
        for (k = 1; k < 4; ++k)
            if (k != 2 && rnet_session_state_probe_pending(r.s[k], NULL, NULL, NULL, NULL))
                (void)rnet_session_state_probe_reply(r.s[k], 1);
    }
    if (rnet_session_state_probe_take_reply(r.s[0], &m))
        fail("liveness: barrier opened without the vanished seat");
    if (!rnet_session_state_drop_peer(r.s[0], 2) || !rnet_session_state_probe_take_reply(r.s[0], &m) || m != 1)
        fail("liveness: dropping the vanished seat did not open the barrier");
    rnet_session_state_probe_finish(r.s[0]);

    /* Seat 3 leaves with BYE: it alone is gone, and everyone sees who. */
    if (rnet_session_send_bye(r.s[3]) != 0)
        fail("liveness: bye");
    for (i = 0; i < 20; ++i)
        room_pump(&r);
    if (!rnet_session_peer_gone(r.s[0], 3) || rnet_session_peer_gone(r.s[0], 1) ||
        rnet_session_peer_gone(r.s[0], 2) || rnet_session_peer_gone_mask(r.s[0]) != (1u << 3) ||
        !rnet_session_peer_gone(r.s[1], 3) || rnet_session_peer_gone(r.s[1], 0))
        fail("liveness: per-seat BYE wrong");
    if (!rnet_session_peer_disconnected(r.s[0], 0))
        fail("liveness: aggregate BYE must still trip");
    if (!rnet_session_peer_slot_disconnected(r.s[0], 3, 0) || rnet_session_peer_slot_disconnected(r.s[0], 1, 0))
        fail("liveness: BYE-only per-seat check wrong");
    /* touch stamps every seat that has not left. */
    rnet_session_touch_peer_liveness(r.s[0]);
    if (rnet_session_peer_rx_age_ms(r.s[0], 2) != 0u)
        fail("liveness: touch did not stamp the silent seat");
    rnet_session_get_stats(r.s[0], &st);
    if (st.peer_gone_mask != (1u << 3))
        fail("liveness: stats gone mask wrong");
    printf("session_state_test: 4-seat liveness + drop_peer %s\n",
           g_failures != failures_before ? "FAILED" : "ok");
out:
    room_close(&r);
}

/* An observer receives the fan-out but is never a receiver the host waits
 * on: its ACK must not complete a transfer, its reply must not open a
 * barrier, and it cannot start one. (The single-peer sender took any ACK, so
 * a spectator that got the blob first completed the transfer for the seat.) */
static void multi_seat_observer_test(void)
{
    Room r;
    rnet_u8 blob[30000];
    int failures_before = g_failures;
    int obs_ready = 0, seat_ready = 0, host_ready = 0;
    int i, m = 0;
    rnet_u32 expect = 0, replied = 0;
    RNetSession *host, *seat, *obs;

    if (!room_open_ex(&r, 2, 0x0b5eu, 0u, 1))
        goto out;
    host = r.s[0];
    seat = r.s[1];
    obs = r.s[2];
    if (!rnet_session_is_observer(obs))
    {
        fail("observer: harness did not build an observer");
        goto out;
    }
    if (!room_drive(&r, 20))
        goto out;
    fill_pattern(blob, sizeof(blob), 0x0bu);
    r.vanished[1] = 1; /* the one seat hears nothing for a while */
    if (rnet_session_state_begin(host, RNET_STATE_OP_BOOT, 0, blob, sizeof(blob)) != 0)
        fail("observer: begin");
    for (i = 0; i < 400; ++i)
    {
        room_pump(&r);
        if (!obs_ready && rnet_session_state_take_ready(obs, NULL, NULL, NULL, NULL))
            obs_ready = 1;
        if (rnet_session_state_take_ready(host, NULL, NULL, NULL, NULL))
        {
            fail("observer: the observer's ACK completed the host's transfer");
            break;
        }
    }
    if (!obs_ready)
        fail("observer: the observer did not receive the fan-out");
    if (rnet_session_state_pending_receivers(host) != (1u << 1))
        fail("observer: the transfer must wait on the seat alone");
    r.vanished[1] = 0;
    for (i = 0; i < kRoomPumpBudget && !(seat_ready && host_ready); ++i)
    {
        room_pump(&r);
        if (!seat_ready && rnet_session_state_take_ready(seat, NULL, NULL, NULL, NULL))
            seat_ready = 1;
        if (!host_ready && rnet_session_state_take_ready(host, NULL, NULL, NULL, NULL))
        {
            host_ready = 1;
            if (!seat_ready)
                fail("observer: host completed before the seat had the blob");
        }
    }
    if (!seat_ready || !host_ready)
        fail("observer: the seat's transfer never completed");
    rnet_session_state_finish(host, 0);
    rnet_session_state_finish(seat, 0);
    rnet_session_state_finish(obs, 0);

    /* Barrier: only the seat's reply counts. */
    r.vanished[1] = 1;
    if (rnet_session_state_probe(host, RNET_STATE_OP_LOAD, 0, 0, 0x4f425356u) != 0)
        fail("observer: probe start");
    for (i = 0; i < 200; ++i)
    {
        room_pump(&r);
        if (rnet_session_state_probe_pending(obs, NULL, NULL, NULL, NULL))
            (void)rnet_session_state_probe_reply(obs, 1);
    }
    if (rnet_session_state_probe_take_reply(host, &m))
        fail("observer: the observer's reply opened the barrier");
    if (!rnet_session_state_probe_replies(host, &expect, &replied, NULL) || expect != (1u << 1) || replied != 0u)
        fail("observer: the barrier counted a non-seat reply");
    r.vanished[1] = 0;
    for (i = 0; i < kRoomPumpBudget; ++i)
    {
        room_pump(&r);
        if (rnet_session_state_probe_pending(seat, NULL, NULL, NULL, NULL))
            (void)rnet_session_state_probe_reply(seat, 1);
        if (rnet_session_state_probe_take_reply(host, &m))
            break;
    }
    if (i == kRoomPumpBudget || m != 1)
        fail("observer: the seat's reply did not open the barrier");
    rnet_session_state_probe_finish(host);
    if (rnet_session_state_begin(obs, RNET_STATE_OP_MEMCARD, 2, blob, 100) != -1)
        fail("observer: an observer must not start a transfer");
    if (rnet_session_disconnected_peers(host, 100) != 0u)
        fail("observer: an observer must never read as a disconnected seat");
    (void)room_drive(&r, 20);
    printf("session_state_test: 2 seats + observer: ACK/reply filtering %s\n",
           g_failures != failures_before ? "FAILED" : "ok");
out:
    room_close(&r);
}

/* A fresh BEGIN from one receiver of a MULTI-receiver outbound transfer must
 * not supersede it (the others still need it): the host keeps sending to the
 * rest while it receives the upload. Once the host has dropped every other
 * receiver, the same BEGIN supersedes, exactly as with two seats. */
static void multi_seat_supersede_test(void)
{
    Room r;
    rnet_u8 boot[40000];
    rnet_u8 card[7000];
    int ready[kMaxSeats] = { 0 };
    int failures_before = g_failures;
    int i, k, from = -1;
    int got_card = 0, host_done = 0;
    rnet_u8 op = 0;

    if (!room_open(&r, 4, 0x5e5eu))
        goto out;
    if (!room_drive(&r, 20))
        goto out;
    fill_pattern(boot, sizeof(boot), 0x51u);
    fill_pattern(card, sizeof(card), 0x52u);
    r.drop_chunks_to[3] = 1000000; /* seat 3 cannot finish yet */
    if (rnet_session_state_begin(r.s[0], RNET_STATE_OP_BOOT, 0, boot, sizeof(boot)) != 0)
        fail("supersede: begin");
    for (i = 0; i < kRoomPumpBudget && !(ready[1] && ready[2]); ++i)
    {
        room_pump(&r);
        for (k = 1; k < 3; ++k)
            if (!ready[k] && rnet_session_state_take_ready(r.s[k], NULL, NULL, NULL, NULL))
            {
                ready[k] = 1;
                rnet_session_state_finish(r.s[k], 0);
            }
    }
    if (!ready[1] || !ready[2])
        fail("supersede: seats 1 and 2 did not get BOOT");
    /* Seat 1 starts its own transfer while the host still owes seat 3. */
    if (rnet_session_state_begin(r.s[1], RNET_STATE_OP_MEMCARD, 1, card, sizeof(card)) != 0)
        fail("supersede: seat 1 begin");
    for (i = 0; i < kRoomPumpBudget && !got_card; ++i)
    {
        room_pump(&r);
        if (rnet_session_state_take_ready_from(r.s[0], &from, &op, NULL, NULL, NULL))
        {
            if (from != 1 || op != RNET_STATE_OP_MEMCARD)
                fail("supersede: host reported the wrong transfer first");
            got_card = 1;
            rnet_session_state_finish_from(r.s[0], 1, 0);
        }
    }
    if (!got_card)
        fail("supersede: host never received seat 1's upload");
    if (rnet_session_state_pending_receivers(r.s[0]) != (1u << 3))
        fail("supersede: seat 1's BEGIN superseded a transfer seat 3 still needs");
    r.drop_chunks_to[3] = 0;
    for (i = 0; i < kRoomPumpBudget && !(host_done && ready[3]); ++i)
    {
        room_pump(&r);
        if (!ready[3] && rnet_session_state_take_ready(r.s[3], NULL, NULL, NULL, NULL))
        {
            ready[3] = 1;
            rnet_session_state_finish(r.s[3], 0);
        }
        if (!host_done && rnet_session_state_take_ready_from(r.s[0], &from, &op, NULL, NULL, NULL))
        {
            if (from != 0 || op != RNET_STATE_OP_BOOT)
                fail("supersede: host outbound completion wrong");
            host_done = 1;
            rnet_session_state_finish_from(r.s[0], 0, 0);
        }
        if (!rnet_session_state_take_ready_from(r.s[1], &from, &op, NULL, NULL, NULL))
            continue;
        if (from == 1 && op == RNET_STATE_OP_MEMCARD)
            rnet_session_state_finish(r.s[1], 0);
    }
    if (!host_done || !ready[3])
        fail("supersede: the transfer to seat 3 never completed");
    for (i = 0; i < 200; ++i)
    {
        room_pump(&r);
        if (rnet_session_state_take_ready_from(r.s[1], &from, &op, NULL, NULL, NULL) && from == 1)
            rnet_session_state_finish(r.s[1], 0);
    }

    /* Now narrow the receivers to seat 1 alone; its BEGIN supersedes. */
    memset(ready, 0, sizeof(ready));
    r.vanished[2] = 1;
    r.vanished[3] = 1;
    if (rnet_session_state_begin(r.s[0], RNET_STATE_OP_BOOT, 0, boot, sizeof(boot)) != 0)
        fail("supersede: second begin");
    for (i = 0; i < kRoomPumpBudget && !ready[1]; ++i)
    {
        room_pump(&r);
        if (rnet_session_state_take_ready(r.s[1], NULL, NULL, NULL, NULL))
        {
            ready[1] = 1;
            rnet_session_state_finish(r.s[1], 0);
        }
    }
    if (!rnet_session_state_drop_peer(r.s[0], 2) || !rnet_session_state_drop_peer(r.s[0], 3))
        fail("supersede: drop_peer");
    /* Deliberately not taken: seat 1's upload must replace it. */
    if (rnet_session_state_begin(r.s[1], RNET_STATE_OP_MEMCARD, 1, card, sizeof(card)) != 0)
        fail("supersede: seat 1 second begin");
    got_card = 0;
    for (i = 0; i < kRoomPumpBudget && !got_card; ++i)
    {
        room_pump(&r);
        while (rnet_session_state_take_ready_from(r.s[0], &from, &op, NULL, NULL, NULL))
        {
            if (from != 1 || op != RNET_STATE_OP_MEMCARD)
                fail("supersede: the superseded outbound transfer was still reported");
            else
                got_card = 1;
            rnet_session_state_finish_from(r.s[0], from, 0);
        }
    }
    if (!got_card || rnet_session_state_pending_receivers(r.s[0]) != 0u ||
        rnet_session_state_progress(r.s[0], 1, NULL, NULL))
        fail("supersede: single-receiver BEGIN did not supersede the outbound transfer");
    for (i = 0; i < 200; ++i)
    {
        room_pump(&r);
        if (rnet_session_state_take_ready_from(r.s[1], &from, &op, NULL, NULL, NULL) && from == 1)
            rnet_session_state_finish(r.s[1], 0);
    }
    r.vanished[2] = 0;
    r.vanished[3] = 0;
    (void)room_drive(&r, 20);
    printf("session_state_test: 4-seat supersede rules %s\n", g_failures != failures_before ? "FAILED" : "ok");
out:
    room_close(&r);
}

/* A receiver that finished (take_ready + finish) before the sender heard its
 * final ACK must answer the sender's retransmitted chunks with a full ACK,
 * or the sender -- which has already seen part of that receiver's progress,
 * so it no longer retransmits BEGIN -- retransmits forever. */
static void multi_seat_finished_reack_test(int n)
{
    Room r;
    rnet_u8 blob[40000];
    int ready[kMaxSeats] = { 0 };
    int failures_before = g_failures;
    int host_done = 0;
    int i, k;

    if (!room_open(&r, n, 0x7e7eu + (rnet_u32)n))
        goto out;
    if (!room_drive(&r, 20))
        goto out;
    fill_pattern(blob, sizeof(blob), 0x61u);
    /* The host never hears seat 1's full ACK; 10 ms of jitter spreads the
     * chunks so seat 1's coalesced ACKs report partial progress first. */
    r.jitter_ms = 10;
    r.drop_acks_from_at[1] = (rnet_u32)sizeof(blob);
    if (rnet_session_state_begin(r.s[0], RNET_STATE_OP_BOOT, 0, blob, sizeof(blob)) != 0)
        fail("reack: begin");
    for (i = 0; i < 600; ++i)
    {
        room_pump(&r);
        for (k = 1; k < n; ++k)
            if (!ready[k] && rnet_session_state_take_ready(r.s[k], NULL, NULL, NULL, NULL))
            {
                ready[k] = 1;
                rnet_session_state_finish(r.s[k], 0);
            }
        if (rnet_session_state_take_ready(r.s[0], NULL, NULL, NULL, NULL))
        {
            fail("reack: host completed without seat 1's full ACK");
            break;
        }
    }
    for (k = 1; k < n; ++k)
        if (!ready[k])
            fail("reack: a guest did not receive the blob");
    {
        rnet_u32 acked = 0;
        if (!rnet_session_state_progress(r.s[0], 1, &acked, NULL) || acked == 0 || acked >= sizeof(blob))
            fail("reack: setup did not leave seat 1 part-acknowledged");
    }
    r.drop_acks_from_at[1] = 0u;
    for (i = 0; i < kRoomPumpBudget && !host_done; ++i)
    {
        room_pump(&r);
        if (rnet_session_state_take_ready(r.s[0], NULL, NULL, NULL, NULL))
            host_done = 1;
    }
    if (!host_done)
        fail("reack: finished receiver never re-ACKed; sender retransmits forever");
    rnet_session_state_finish(r.s[0], 0);
    (void)room_drive(&r, 20);
    printf("session_state_test: %d-seat finished-transfer re-ACK %s\n", n,
           g_failures != failures_before ? "FAILED" : "ok");
out:
    room_close(&r);
}

/* Sparse room: 4 slots, seats {0,1,3}. Nothing may wait on empty seat 2. */
static void multi_seat_sparse_test(void)
{
    Room r;
    rnet_u8 blob[50000];
    rnet_u8 card[9000];
    RNetSessionStats st;
    int ready[kMaxSeats] = { 0 };
    int got[kMaxSeats] = { 0 };
    int failures_before = g_failures;
    int i, k, m = 0, from = -1;
    rnet_u32 expect = 0;
    const rnet_u32 occupied = 0x0bu;

    if (!room_open_mask(&r, 4, 0x5a5au, occupied))
        goto out;
    if (!room_drive(&r, 20))
        goto out;
    r.state_loss_pct = 20;
    r.jitter_ms = 3;
    fill_pattern(blob, sizeof(blob), 5);
    if (rnet_session_state_begin(r.s[0], RNET_STATE_OP_BOOT, 0, blob, sizeof(blob)) != 0)
        fail("sparse: begin");
    rnet_session_get_stats(r.s[0], &st);
    if (st.state_expect_mask != 0x0au)
        fail("sparse: transfer must expect seats 1 and 3 only");
    for (i = 0; i < kRoomPumpBudget && !ready[0]; ++i)
    {
        room_pump(&r);
        for (k = 0; k < 4; ++k)
        {
            const void *data;
            size_t size;
            if (r.s[k] == NULL || ready[k])
                continue;
            if (rnet_session_state_take_ready(r.s[k], NULL, NULL, &data, &size))
            {
                ready[k] = 1;
                if (size != sizeof(blob) || memcmp(data, blob, size) != 0)
                    fail("sparse: payload mismatch");
                if (k == 0 && (!ready[1] || !ready[3]))
                    fail("sparse: host completed before both guests had the blob");
            }
        }
    }
    if (!ready[0])
        fail("sparse: host transfer never completed (waiting on the empty seat?)");
    for (k = 0; k < 4; ++k)
        if (r.s[k] != NULL)
            rnet_session_state_finish(r.s[k], 0);

    if (rnet_session_state_probe(r.s[0], RNET_STATE_OP_LOAD, 0, 0, 0x53505253u) != 0 ||
        !rnet_session_state_probe_replies(r.s[0], &expect, NULL, NULL) || expect != 0x0au)
        fail("sparse: probe must expect seats 1 and 3 only");
    for (i = 0; i < kRoomPumpBudget; ++i)
    {
        room_pump(&r);
        for (k = 1; k < 4; ++k)
            if (r.s[k] != NULL && rnet_session_state_probe_pending(r.s[k], NULL, NULL, NULL, NULL))
                (void)rnet_session_state_probe_reply(r.s[k], 1);
        if (rnet_session_state_probe_take_reply(r.s[0], &m))
            break;
    }
    if (i == kRoomPumpBudget || m != 1)
        fail("sparse: barrier never opened");
    rnet_session_state_probe_finish(r.s[0]);

    /* Uploads from seats 1 and 3 at once. */
    for (k = 1; k < 4; k += 2)
    {
        fill_pattern(card, sizeof(card), 0x80u + (unsigned)k);
        if (rnet_session_state_begin(r.s[k], RNET_STATE_OP_MEMCARD, (rnet_u8)k, card, sizeof(card)) != 0)
            fail("sparse: guest upload begin");
    }
    memset(ready, 0, sizeof(ready));
    for (i = 0; i < kRoomPumpBudget && !(got[1] && got[3] && ready[1] && ready[3]); ++i)
    {
        rnet_u8 op, slot;
        const void *data;
        size_t size;
        room_pump(&r);
        while (rnet_session_state_take_ready_from(r.s[0], &from, &op, &slot, &data, &size))
        {
            fill_pattern(card, sizeof(card), 0x80u + (unsigned)from);
            if ((from != 1 && from != 3) || op != RNET_STATE_OP_MEMCARD || size != sizeof(card) ||
                memcmp(data, card, size) != 0)
                fail("sparse: host received a wrong upload");
            else
                got[from]++;
            rnet_session_state_finish_from(r.s[0], from, 0);
        }
        for (k = 1; k < 4; k += 2)
        {
            if (!ready[k] && rnet_session_state_take_ready_from(r.s[k], &from, &op, NULL, NULL, NULL))
            {
                if (from != k || op != RNET_STATE_OP_MEMCARD)
                    fail("sparse: guest completion reported the wrong transfer");
                ready[k] = 1;
                rnet_session_state_finish(r.s[k], 0);
            }
        }
    }
    if (got[1] != 1 || got[3] != 1)
        fail("sparse: host did not collect one upload per occupied guest");
    if (!ready[1] || !ready[3])
        fail("sparse: a guest upload never completed");
    if (rnet_session_disconnected_peers(r.s[0], 100) != 0u || rnet_session_peer_slot_disconnected(r.s[0], 2, 1))
        fail("sparse: the empty seat must never read as disconnected");
    r.state_loss_pct = 0;
    (void)room_drive(&r, 20);
    printf("session_state_test: sparse 4-slot room {0,1,3} BOOT + barrier + uploads %s\n",
           g_failures != failures_before ? "FAILED" : "ok");
out:
    room_close(&r);
}

#endif /* !RNET_STATE_TEST_LEGACY_ONLY */

int main(void)
{
    two_seat_load_test();
    /* Two seats through the relay: the single-peer behaviour, unchanged. */
    multi_seat_broadcast_test(2, 20, RNET_STATE_OP_BOOT, 11u);
    multi_seat_broadcast_test(3, 20, RNET_STATE_OP_BOOT, 12u);
    multi_seat_broadcast_test(4, 20, RNET_STATE_OP_BOOT, 13u);
    multi_seat_broadcast_test(4, 35, RNET_STATE_OP_SRAM, 14u);
#ifndef RNET_STATE_TEST_LEGACY_ONLY
    multi_seat_input_hole_test(3, 0x316a5u);
    multi_seat_input_hole_test(4, 0x4b127u);
    multi_seat_memcard_test(2, 20, 21u);
    multi_seat_memcard_test(3, 20, 22u);
    multi_seat_memcard_test(4, 25, 23u);
    multi_seat_upload_then_boot_test(2, 41u);
    multi_seat_upload_then_boot_test(4, 42u);
    multi_seat_probe_test(2, 20, 31u);
    multi_seat_probe_test(3, 20, 32u);
    multi_seat_probe_test(4, 25, 33u);
    multi_seat_liveness_test();
    multi_seat_sparse_test();
    multi_seat_observer_test();
    multi_seat_supersede_test();
    multi_seat_finished_reack_test(2);
    multi_seat_finished_reack_test(3);
#endif
    if (g_failures == 0)
    {
        printf("session_state_test: ok\n");
        return 0;
    }
    fprintf(stderr, "session_state_test: %d failure(s)\n", g_failures);
    return 1;
}
