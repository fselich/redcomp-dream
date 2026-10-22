#include "recomp_net/recomp_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <process.h>
static unsigned test_pid(void) { return (unsigned)_getpid(); }
#else
#include <unistd.h>
static unsigned test_pid(void) { return (unsigned)getpid(); }
#endif

enum { kSeats = 3, kTransferBytes = 64 * 1024 };

typedef struct TestCtx { rnet_u8 slot; } TestCtx;

static int g_failures;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    ++g_failures;
}

static void sample_local(rnet_u32 tick, RNetInputSample *out, void *opaque)
{
    const TestCtx *ctx = (const TestCtx *)opaque;
    memset(out, 0, sizeof(*out));
    out->size = 2;
    out->bytes[0] = ctx->slot;
    out->bytes[1] = (rnet_u8)tick;
    out->valid = 1;
}

static void publish(rnet_u32 tick, const RNetInputSample *by_slot, int slots, void *opaque)
{
    (void)tick;
    (void)by_slot;
    (void)slots;
    (void)opaque;
}

static void pump_peer(RNetSession *s)
{
    rnet_session_pump(s);
}

static void pump_all(RNetSession **s)
{
    int i;
    for (i = 0; i < kSeats; ++i)
        pump_peer(s[i]);
}

static void pause_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int wait_running(RNetSession **s)
{
    int i;
    for (i = 0; i < 5000; ++i)
    {
        pump_all(s);
        if (rnet_session_is_running(s[0]) && rnet_session_is_running(s[1]) &&
            rnet_session_is_running(s[2]))
            return 1;
        pause_ms(1);
    }
    return 0;
}

static int wait_guest_probes(RNetSession **s)
{
    int i;
    for (i = 0; i < 3000; ++i)
    {
        pump_all(s);
        if (rnet_session_state_probe_pending(s[1], NULL, NULL, NULL, NULL) &&
            rnet_session_state_probe_pending(s[2], NULL, NULL, NULL, NULL))
            return 1;
        pause_ms(1);
    }
    return 0;
}

static void finish_sessions(RNetSession **s)
{
    int i;
    for (i = 0; i < kSeats; ++i)
        if (s[i])
            rnet_session_destroy(s[i]);
}

int main(void)
{
    RNetConfig cfg[kSeats];
    RNetHostVTable vt[kSeats];
    TestCtx ctx[kSeats] = {{0}, {1}, {2}};
    RNetSession *s[kSeats] = {NULL, NULL, NULL};
    char bind[kSeats][64];
    unsigned base_port = 43000u + (test_pid() % 6000u) * 3u;
    rnet_u8 *blob = NULL;
    const void *data = NULL;
    size_t size = 0;
    int i, match = -1, host_ready = 0, guest1_ready = 0, guest2_ready = 0;

    for (i = 0; i < kSeats; ++i)
    {
        rnet_config_init_defaults(&cfg[i]);
        cfg[i].local_slot = (rnet_u8)i;
        cfg[i].slot_count = kSeats;
        cfg[i].session_id = 0x53544154u ^ test_pid();
        memset(&vt[i], 0, sizeof(vt[i]));
        vt[i].sample_local = sample_local;
        vt[i].publish = publish;
        vt[i].ctx = &ctx[i];
        s[i] = rnet_session_create(&cfg[i], &vt[i]);
        snprintf(bind[i], sizeof(bind[i]), "127.0.0.1:%u", base_port + (unsigned)i);
        if (!s[i])
        {
            fail("session create");
            finish_sessions(s);
            return 1;
        }
    }
    if (rnet_session_start_lan_hub(s[0], bind[0]) != 0 ||
        rnet_session_start_lan(s[1], bind[1], bind[0]) != 0 ||
        rnet_session_start_lan(s[2], bind[2], bind[0]) != 0)
    {
        fail("three-seat LAN hub start");
        finish_sessions(s);
        return 1;
    }
    if (!wait_running(s))
    {
        fail("three-seat sessions did not reach running");
        finish_sessions(s);
        return 1;
    }

    /* A fast matching peer cannot stand in for a slower occupied seat. */
    if (rnet_session_state_probe(s[0], RNET_STATE_OP_SAVE, 10, 1234, 0x12345678u) != 0 ||
        !wait_guest_probes(s))
    {
        fail("save hash probe did not reach every guest");
        finish_sessions(s);
        return 1;
    }
    if (rnet_session_state_probe_reply(s[2], 1) != 0)
        fail("slot 2 match reply");
    for (i = 0; i < 100; ++i)
    {
        pump_peer(s[0]);
        pump_peer(s[2]);
        pause_ms(1); /* slot 1 deliberately does not pump or answer */
    }
    if (rnet_session_state_probe_take_reply(s[0], &match))
        fail("host accepted a probe before every occupied guest replied");
    if (!rnet_session_state_busy(s[2]))
        fail("matching guest unstalled before the host resolved the group probe");
    if (!rnet_session_state_probe_pending(s[1], NULL, NULL, NULL, NULL))
        fail("delayed slot 1 lost its pending probe");
    if (rnet_session_state_probe_reply(s[1], 0) != 0)
        fail("slot 1 mismatch reply");
    for (i = 0; i < 1000 && !rnet_session_state_probe_take_reply(s[0], &match); ++i)
    {
        pump_all(s);
        pause_ms(1);
    }
    if (i == 1000 || match != 0)
        fail("host did not aggregate the mismatch from slot 1");
    rnet_session_state_probe_finish(s[0]); /* mismatch: guests remain parked for transfer */

    blob = (rnet_u8 *)malloc(kTransferBytes);
    if (!blob)
    {
        fail("transfer blob allocation");
        finish_sessions(s);
        return 1;
    }
    for (i = 0; i < kTransferBytes; ++i)
        blob[i] = (rnet_u8)(i * 37 + 11);
    if (rnet_session_state_begin(s[0], RNET_STATE_OP_SAVE, 10, blob, kTransferBytes) != 0)
    {
        fail("multi-peer save transfer start");
        free(blob);
        finish_sessions(s);
        return 1;
    }

    /* Let slot 1 finish while slot 2 is not pumping. The sender must still
     * wait for slot 2 rather than treating slot 1's ACK as a group ACK. */
    for (i = 0; i < 3000; ++i)
    {
        const void *peer_data = NULL;
        size_t peer_size = 0;
        pump_peer(s[0]);
        pump_peer(s[1]);
        if (rnet_session_state_take_ready(s[1], NULL, NULL, &peer_data, &peer_size))
        {
            guest1_ready = 1;
            if (peer_size != kTransferBytes || memcmp(peer_data, blob, kTransferBytes) != 0)
                fail("slot 1 transfer payload mismatch");
            break;
        }
        pause_ms(1);
    }
    if (!guest1_ready)
        fail("slot 1 transfer did not complete");
    if (rnet_session_state_take_ready(s[0], NULL, NULL, NULL, NULL))
        fail("host completed transfer before the non-pumping slot 2 ACK");

    for (i = 0; i < 5000 && !(host_ready && guest2_ready); ++i)
    {
        const void *peer_data = NULL;
        size_t peer_size = 0;
        pump_all(s);
        if (!guest2_ready && rnet_session_state_take_ready(s[2], NULL, NULL, &peer_data, &peer_size))
        {
            guest2_ready = 1;
            if (peer_size != kTransferBytes || memcmp(peer_data, blob, kTransferBytes) != 0)
                fail("slot 2 transfer payload mismatch");
        }
        if (!host_ready && rnet_session_state_take_ready(s[0], NULL, NULL, &data, &size))
        {
            host_ready = 1;
            if (size != kTransferBytes || memcmp(data, blob, kTransferBytes) != 0)
                fail("host transfer payload mismatch");
        }
        pause_ms(1);
    }
    if (!host_ready || !guest2_ready)
        fail("host did not wait for and finish the slow receiver");
    for (i = 0; i < kSeats; ++i)
        rnet_session_state_finish(s[i], 0);

    /* An all-match result is also held until the host releases that exact
     * probe generation to every guest. */
    match = -1;
    if (rnet_session_state_probe(s[0], RNET_STATE_OP_SAVE, 11, 5678, 0x87654321u) != 0 ||
        !wait_guest_probes(s))
        fail("second all-match probe did not reach every guest");
    if (rnet_session_state_probe_reply(s[1], 1) != 0 || rnet_session_state_probe_reply(s[2], 1) != 0)
        fail("all-match guest replies");
    for (i = 0; i < 1000 && !rnet_session_state_probe_take_reply(s[0], &match); ++i)
    {
        pump_all(s);
        pause_ms(1);
    }
    if (i == 1000 || match != 1)
        fail("host did not aggregate all matching replies");
    if (!rnet_session_state_busy(s[1]) || !rnet_session_state_busy(s[2]))
        fail("matching guests resumed before host release");
    rnet_session_state_probe_finish(s[0]);
    for (i = 0; i < 1000 && (rnet_session_state_busy(s[1]) || rnet_session_state_busy(s[2])); ++i)
    {
        pump_all(s);
        pause_ms(1);
    }
    if (i == 1000)
        fail("host probe release did not resume matching guests");

    free(blob);
    finish_sessions(s);
    if (g_failures)
    {
        fprintf(stderr, "session_state_multipeer_test: %d failure(s)\n", g_failures);
        return 1;
    }
    puts("session_state_multipeer_test: ok (seat-aggregated probes, release, and transfer ACKs)");
    return 0;
}
