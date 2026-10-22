#include "recomp_net/recomp_net.h"
#include "recomp_net/startup_profiles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

static int failures;
static unsigned sequence;
static rnet_u64 now = 1000;
static void check(int ok, const char *why) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); ++failures; }
}
static rnet_u64 clock_ms(void *ctx) { (void)ctx; return now; }
static void sample(rnet_u32 tick, RNetInputSample *out, void *ctx) {
    (void)ctx; memset(out, 0, sizeof(*out)); out->tick = tick; out->valid = 1; out->size = 1;
}
static void publish(rnet_u32 t, const RNetInputSample *in, int n, void *ctx) {
    (void)t; (void)in; (void)n; (void)ctx;
}

/* These are synthetic qualified namespaces: the transport must allow different
 * architectures implementing the SAME qualified profile. It must not infer
 * compatibility from the CPU feature or architecture numbers. */
static void run_case(int seats, rnet_u32 occupied, const rnet_u32 *profiles,
                     rnet_u32 expected, int fault) {
    RNetSession *sessions[4] = {0};
    RNetProfileCaps caps[4];
    char bind[4][48];
    unsigned port = 30000u + ((unsigned)getpid() % 4000u) * 4u + (++sequence * 4u);
    int i, step, complete = 0;
    rnet_u32 transcript = 0;
    const rnet_u32 all = occupied ? occupied : (1u << seats) - 1u;
    for (i = 0; i < seats; ++i) {
        RNetConfig cfg;
        RNetHostVTable vt;
        if (!(all & (1u << i))) continue;
        rnet_config_init_defaults(&cfg);
        cfg.slot_count = (rnet_u8)seats; cfg.local_slot = (rnet_u8)i;
        cfg.occupied_mask = occupied; cfg.session_id = sequence + 0x123400u;
        memset(&vt, 0, sizeof(vt));
        vt.sample_local = sample; vt.publish = publish; vt.now_ms = clock_ms;
        sessions[i] = rnet_session_create(&cfg, &vt);
        check(sessions[i] != NULL, "create");
        if (!sessions[i]) goto done;
        caps[i].cpu_features = (rnet_u32)(10 + i);
        caps[i].backends = (rnet_u32)(20 + i);
        caps[i].profiles = profiles[i]; caps[i].architecture = (rnet_u32)i;
        if (!(fault == 2 && i == seats - 1)) {
            check(rnet_session_profiles_begin(sessions[i], fault == 1 && i == 1 ? 101u : 100u, &caps[i]) == 0, "begin");
            check(rnet_session_profiles_begin(sessions[i], 100u, &caps[i]) == -1, "repeated begin refused");
        }
        snprintf(bind[i], sizeof(bind[i]), "127.0.0.1:%u", port + (unsigned)i);
    }
    for (i = 0; i < seats; ++i) {
        if (!sessions[i]) continue;
        check((i == 0 && seats > 2 ? rnet_session_start_lan_hub(sessions[i], bind[i]) :
            rnet_session_start_lan(sessions[i], bind[i], i == 0 ? "" : bind[0])) == 0, "open wire");
    }
    for (step = 0; step < (fault == 2 ? 500 : 6000); ++step) {
        int ready = 0, refused = 0;
        ++now;
        for (i = seats - 1; i >= 0; --i) {
            int status;
            if (!sessions[i]) continue;
            /* Hold the final peer initially: no other seat may run ahead. */
            if (i != seats - 1 || step >= 40) rnet_session_pump(sessions[i]);
            status = rnet_session_profiles_status(sessions[i], NULL, NULL);
            if (status == 1) ++ready;
            else if (status < 0) ++refused;
            else check(!rnet_session_try_admit(sessions[i], 0), "no admission before agreement");
        }
        if (fault == 1 && refused) { complete = 1; break; }
        if (fault != 1 && fault != 2 && ready == (int)((all & 1u) + ((all >> 1) & 1u) + ((all >> 2) & 1u) + ((all >> 3) & 1u))) {
            complete = 1; break;
        }
        rnet_session_wait_recv(sessions[0], 1);
    }
    if (fault == 2) {
        check(rnet_session_profiles_status(sessions[0], NULL, NULL) == 0, "legacy peer never silently downgrades");
        check(!rnet_session_try_admit(sessions[0], 0), "legacy peer cannot release barrier");
        goto done;
    }
    check(complete, "startup completed or explicitly refused within bound");
    if (fault == 1 || !complete) goto done;
    for (i = 0; i < seats; ++i) {
        rnet_u32 selected = 99, h = 0;
        int peer;
        if (!sessions[i]) continue;
        check(rnet_session_profiles_status(sessions[i], &selected, &h) == 1, "agreed");
        check(selected == expected, "highest common qualified profile selected");
        if (!i) transcript = h;
        check(h == transcript, "canonical capability transcript matches");
        for (peer = 0; peer < seats; ++peer) {
            RNetProfileCaps copy;
            if (!sessions[peer]) continue;
            check(rnet_session_profiles_peer(sessions[i], peer, &copy) == 1 &&
                memcmp(&copy, &caps[peer], sizeof(copy)) == 0, "actual peer capabilities preserved");
        }
    }
    /* Late/reordered duplicates deliver the fixed ACK without stealing episode
     * messages or reopening selection. A changed offer instead refuses it. */
    for (step = 0; step < 250; ++step) {
        ++now;
        for (i = 0; i < seats; ++i) if (sessions[i]) rnet_session_pump(sessions[i]);
    }
    check(rnet_session_profiles_status(sessions[0], NULL, NULL) == 1, "agreement remains fixed");
    if (fault == 3)
        rnet_session_send_rb_sync(sessions[seats - 1], 100u, expected ^ 1u,
            transcript, all, 0u, 8u, 0u);
    else
        rnet_session_send_rb_sync(sessions[seats - 1], 100u, 999u,
            caps[seats - 1].backends, caps[seats - 1].profiles,
            (rnet_u8)caps[seats - 1].architecture, 7u, 0u);
    for (step = 0; step < 100; ++step) { ++now; rnet_session_pump(sessions[0]); rnet_session_wait_recv(sessions[0], 1); }
    /* The loss run may drop this deliberate single corrupt packet. */
    if (!getenv("RNET_SIM_LOSS_PCT"))
        check(rnet_session_profiles_status(sessions[0], NULL, NULL) == -1, "changed offer or invalid ACK refuses, never switches");
done:
    for (i = 0; i < seats; ++i) if (sessions[i]) rnet_session_destroy(sessions[i]);
}

int main(void) {
    const rnet_u32 full[4] = {15,15,15,15}, partial[4] = {15,3,15,15};
    const rnet_u32 mixed[4] = {15,15,5,1}, equivalent[4] = {5,5,5,5};
    run_case(2, 0, full, 3, 0);
    run_case(2, 0, partial, 1, 0);
    run_case(3, 0, equivalent, 2, 0);
    run_case(4, 13, mixed, 0, 0);
    run_case(2, 0, full, 0, 1);
    run_case(2, 0, full, 0, 2);
    run_case(2, 0, full, 3, 3);
    printf("startup profiles: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
