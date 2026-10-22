#include "recomp_net/sched.h"
#include "platform/rnet_platform.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL: %s\n", msg);                                         \
            failures++;                                                        \
        } else {                                                               \
            printf("ok:   %s\n", msg);                                         \
        }                                                                      \
    } while (0)

static uint32_t now_ms(void *ctx)
{
    (void)ctx;
    return (uint32_t)rnet_os_monotonic_ms();
}

int main(void)
{
    RNetSchedBridge br;
    int delay = 4;
    int pred = 8;
    int slot = 0;
    RNetSessionStats st;
    const char *reason = NULL;
    int stall;

    memset(&br, 0, sizeof(br));
    br.session = NULL;
    br.input_delay = &delay;
    br.input_prediction = &pred;
    br.local_slot = &slot;
    br.gates.now_ms = now_ms;

    rnet_sched_bind(&br);
    CHECK(rnet_sched_real_delay_enabled(), "real-delay default");
    CHECK(rnet_sched_wire_for_sim(42) == 42u, "real-delay wire==sim");

    memset(&st, 0, sizeof(st));
    st.sim_tick = 10;
    st.highest_remote_wire = 10;
    st.remote_lead = 4;
    stall = rnet_sched_pre_admit(10, 10, &st);
    CHECK(!stall, "pre_admit proceed with healthy lead");

    /* Past P → pcap freeze stall. */
    st.highest_remote_wire = 10;
    stall = rnet_sched_on_remote_miss(1, 20, 30, &st, pred, &reason);
    CHECK(stall == 1, "pcap freeze stalls");
    CHECK(reason && strcmp(reason, "pcap_freeze") == 0, "pcap reason");

    rnet_sched_set_admit_stall("test");
    CHECK(strcmp(rnet_sched_admit_stall_tag(), "test") == 0, "stall tag");
    rnet_sched_clear_admit_stall();
    CHECK(rnet_sched_admit_stall_tag()[0] == '\0', "stall cleared");

    rnet_sched_note_episode_boundary();
    rnet_sched_arm_absurd_invent_catchup();
    rnet_sched_post_admit(0);

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ALL PASS\n");
    return 0;
}
