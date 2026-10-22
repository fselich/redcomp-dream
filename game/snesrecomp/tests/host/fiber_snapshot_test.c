/* Rewinding a suspended fiber's execution position.
 *
 * A game whose guest runs on a fiber keeps its position in that fiber's C call
 * chain, and no guest-state snapshot contains it. Run-ahead speculates forward
 * and rewinds; without this, the machine goes back and the fiber stays where
 * the speculation left it. Measured in Super Metroid before the fiber snapshot
 * existed: the two drifted apart around scene changes, where the call depth
 * differs from frame to frame.
 *
 * The fiber here counts, yields, and recurses, so a snapshot has to carry both
 * a value and a call chain several frames deep to put anything back.
 */
#include "desktop/fiber_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
static void expect(int cond, const char *what) {
    if (cond) { printf("ok: %s\n", what); return; }
    fprintf(stderr, "FAIL: %s\n", what);
    ++fails;
}

static void *g_host;
static void *g_worker;
static int   g_ticks;         /* visible "guest state" the fiber advances */
static int   g_depth_seen;

/* Recurses to `depth`, yielding once at the bottom of every level, so the
 * suspended stack is genuinely several frames deep and carries locals that
 * must come back with it. */
static void descend(int depth, int tag);
/* Called through a pointer so the compiler cannot see the self-call: the
 * recursion is bounded by `depth`, but the innermost frame never returns
 * (it is a fiber body), and -Winfinite-recursion reads the pair as endless. */
static void (*g_descend)(int, int) = &descend;

static void descend(int depth, int tag) {
    volatile int marker = tag * 1000 + depth;   /* a local to verify */
    if (depth > 0) g_descend(depth - 1, tag);
    for (;;) {
        g_ticks++;
        g_depth_seen = (int)marker;
        SwitchToFiber(g_host);
    }
}

static void CALLBACK worker(void *param) {
    (void)param;
    descend(6, 7);
}

int main(void) {
    void *blob;
    size_t bound, saved;
    int ticks_at_save;

    if (!FiberSnapshotSupported()) {
        /* Win32 fibers are opaque, an Android fiber is a real thread. Both
         * report unsupported, and run-ahead declines on those builds rather
         * than rewinding the machine out from under a fiber that stays put. */
        puts("fiber_snapshot_test: unsupported backend, skipped");
        return 0;
    }

    g_host = ConvertThreadToFiber(NULL);
    expect(g_host != NULL, "the thread becomes a fiber");
    g_worker = CreateFiber(256 * 1024, worker, NULL);
    expect(g_worker != NULL, "the worker fiber is created");

    /* Run it a few times so it is suspended deep inside its recursion. */
    for (int i = 0; i < 3; ++i) SwitchToFiber(g_worker);
    expect(g_ticks == 3, "the worker ran three times");
    expect(g_depth_seen == 7000, "and is suspended at the bottom of its stack");

    bound = FiberSnapshotBound(g_worker);
    expect(bound > 0, "a suspended fiber can be bounded");
    blob = malloc(bound);
    expect(blob != NULL, "the snapshot buffer allocates");

    saved = FiberSnapshotSave(g_worker, blob, bound);
    expect(saved > 0 && saved <= bound, "and saved within that bound");
    ticks_at_save = g_ticks;

    /* Speculate: run it forward, as run-ahead does. */
    for (int i = 0; i < 5; ++i) SwitchToFiber(g_worker);
    expect(g_ticks == ticks_at_save + 5, "the worker advanced five more times");

    /* Rewind. The fiber must resume exactly where it was, and go on from
     * there: the tick after the restore is the one that followed the save. */
    expect(FiberSnapshotLoad(g_worker, blob, saved), "the snapshot loads");
    g_ticks = ticks_at_save;         /* the caller rewinds its own state */
    SwitchToFiber(g_worker);
    expect(g_ticks == ticks_at_save + 1,
           "and the fiber carries on from the restored point");
    expect(g_depth_seen == 7000, "still inside the same call chain");

    /* A blob from a different fiber must be refused, not applied. */
    {
        void *other = CreateFiber(256 * 1024, worker, NULL);
        expect(other != NULL, "a second fiber is created");
        expect(!FiberSnapshotLoad(other, blob, saved),
               "a snapshot is refused by a fiber it did not come from");
        DeleteFiber(other);
    }
    expect(!FiberSnapshotLoad(g_worker, blob, sizeof(size_t)),
           "and a truncated snapshot is refused");

    free(blob);
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("fiber_snapshot_test: ok");
    return 0;
}
