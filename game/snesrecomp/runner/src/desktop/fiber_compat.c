/*
 * fiber_compat.c — POSIX implementation of the Win32 Fiber API used by
 * cooperative game task schedulers. See fiber_compat.h. Compiled to nothing
 * on Windows, where the native Fiber API is used directly.
 *
 * Two backends:
 *  - ucontext (macOS/glibc): true stack switching, zero threads.
 *  - pthread (Android): bionic ships the ucontext types but NOT
 *    getcontext/makecontext/swapcontext, so each fiber is a parked thread
 *    and SwitchToFiber is a condvar handoff. A frame-model host switches
 *    fibers a couple of times per frame, so the handoff cost does not
 *    matter; a scheduler that switches per guest task should measure it.
 *
 * The Android backend came from SuperMetroidRecomp, which needed it to run
 * its single-fiber WaitForNMI frame model on arm64. It was a private copy of
 * this file with one extra #ifdef -- the shape that, repeated per port,
 * cannot inherit a fix (recomp-ai-rules/PRINCIPLES.md).
 */
#ifndef _WIN32
#ifdef __ANDROID__

#include "fiber_compat.h"
#include <pthread.h>
#include <stdlib.h>

typedef struct Fiber {
    pthread_t       thread;
    pthread_cond_t  cv;
    int             scheduled;  /* protected by g_fiber_lock */
    FiberProc       entry;
    void           *param;
    int             started;    /* thread created (CreateFiber fibers only) */
    size_t          stack_size;
} Fiber;

static pthread_mutex_t g_fiber_lock = PTHREAD_MUTEX_INITIALIZER;

/* The fiber currently executing on this thread group. Only ever mutated with
 * g_fiber_lock held, inside SwitchToFiber. */
static Fiber *g_current_fiber = NULL;

static void *fiber_thread_main(void *arg) {
    Fiber *self = (Fiber *)arg;
    pthread_mutex_lock(&g_fiber_lock);
    while (!self->scheduled)
        pthread_cond_wait(&self->cv, &g_fiber_lock);
    pthread_mutex_unlock(&g_fiber_lock);
    self->entry(self->param);
    /* Fiber entries never fall off the end (they loop SwitchToFiber back to
     * the scheduler). If one ever does, there is no safe continuation. */
    abort();
    return NULL;
}

void *ConvertThreadToFiber(void *param) {
    (void)param;
    Fiber *f = (Fiber *)calloc(1, sizeof(Fiber));
    if (!f) return NULL;
    pthread_cond_init(&f->cv, NULL);
    f->scheduled = 1;   /* it is running right now */
    f->started = 1;
    g_current_fiber = f;
    return f;
}

void *CreateFiber(size_t stack_size, FiberProc entry, void *param) {
    Fiber *f = (Fiber *)calloc(1, sizeof(Fiber));
    if (!f) return NULL;
    pthread_cond_init(&f->cv, NULL);
    f->entry = entry;
    f->param = param;
    f->stack_size = stack_size < 256 * 1024 ? 256 * 1024 : stack_size;
    /* Thread creation is deferred to the first switch so a fiber that is
     * created but never entered costs nothing but this struct. */
    return f;
}

void SwitchToFiber(void *fiber) {
    Fiber *target = (Fiber *)fiber;
    if (!target || target == g_current_fiber) return;
    Fiber *self = g_current_fiber;

    pthread_mutex_lock(&g_fiber_lock);
    if (!target->started) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, target->stack_size);
        if (pthread_create(&target->thread, &attr, fiber_thread_main,
                           target) != 0) {
            pthread_attr_destroy(&attr);
            pthread_mutex_unlock(&g_fiber_lock);
            abort();    /* mirrors the desktop backends: switch cannot fail */
        }
        pthread_attr_destroy(&attr);
        target->started = 1;
    }
    g_current_fiber = target;
    self->scheduled = 0;
    target->scheduled = 1;
    pthread_cond_signal(&target->cv);
    while (!self->scheduled)
        pthread_cond_wait(&self->cv, &g_fiber_lock);
    pthread_mutex_unlock(&g_fiber_lock);
}

void DeleteFiber(void *fiber) {
    Fiber *f = (Fiber *)fiber;
    if (!f) return;
    /* Deleting a parked fiber's thread safely would need a cancellation
     * handshake; SM only deletes fibers at process teardown, so leak the
     * parked thread and free the bookkeeping. */
    pthread_cond_destroy(&f->cv);
    free(f);
}

unsigned long GetLastError(void) { return 0; }

/* A fiber here is a parked pthread: its stack belongs to the kernel and its
 * registers live in a thread that is blocked on a condvar. Nothing this file
 * can copy out and put back. */
int    FiberSnapshotSupported(void) { return 0; }
size_t FiberSnapshotBound(void *fiber) { (void)fiber; return 0; }
size_t FiberSnapshotSave(void *fiber, void *out, size_t capacity) {
    (void)fiber; (void)out; (void)capacity; return 0;
}
int    FiberSnapshotLoad(void *fiber, const void *in, size_t size) {
    (void)fiber; (void)in; (void)size; return 0;
}

#else /* !__ANDROID__: ucontext backend */

#define _XOPEN_SOURCE 600   /* expose ucontext on macOS/glibc — must precede includes */

#include "fiber_compat.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

typedef struct Fiber {
    ucontext_t ctx;
    FiberProc  entry;
    void      *param;
    void      *stack;     /* NULL for a ConvertThreadToFiber handle */
    size_t     stack_size;
    /* Lowest stack address that was live when this fiber was last suspended.
     * Recorded by SwitchToFiber on the way out, so a snapshot copies the live
     * part of the stack and not the whole (often 8 MiB) allocation. */
    void      *suspended_low;
} Fiber;

/* Slack below the address SwitchToFiber sees for its own frame, to cover that
 * frame, swapcontext's, and any red zone. Copying a few KB more of a stack the
 * fiber owns is free; copying too few would lose live bytes. */
#define FIBER_SNAPSHOT_SLACK 4096u

/* One saved fiber. The stack bytes follow the header, low address first. */
typedef struct FiberSnapshotHeader {
    uint32_t   magic;
    uint32_t   version;
    void      *stack;        /* identity check: same fiber, same allocation */
    size_t     stack_bytes;  /* how many bytes follow, from `low` upward */
    void      *low;          /* where they go back */
    ucontext_t ctx;
} FiberSnapshotHeader;

#define FIBER_SNAPSHOT_MAGIC   0x46425253u /* 'FBRS' */
#define FIBER_SNAPSHOT_VERSION 1u

/* The fiber currently executing on this thread. Updated by SwitchToFiber
 * before the context swap, so a freshly-started fiber's trampoline sees
 * itself here. */
static Fiber *g_current_fiber = NULL;

static void fiber_trampoline(void) {
    Fiber *self = g_current_fiber;
    if (self && self->entry)
        self->entry(self->param);
    /* MMX fiber entries never fall off the end (they loop SwitchToFiber back
     * to the scheduler). If one ever does, there is no safe continuation. */
    abort();
}

void *ConvertThreadToFiber(void *param) {
    (void)param;
    Fiber *f = (Fiber *)calloc(1, sizeof(Fiber));
    if (!f) return NULL;
    /* No stack of its own — it rides the thread's existing stack. The ctx is
     * populated by the first swapcontext that switches away from it. */
    g_current_fiber = f;
    return f;
}

void *CreateFiber(size_t stack_size, FiberProc entry, void *param) {
    /* volatile: getcontext() below is a setjmp-class function, and a local
     * held live across one is what -Wclobbered exists to flag. The pointer is
     * reloaded from memory rather than kept in a call-saved register. */
    Fiber *volatile f = (Fiber *)calloc(1, sizeof(Fiber));
    if (!f) return NULL;
    f->stack = malloc(stack_size);
    if (!f->stack) { free(f); return NULL; }
    f->stack_size = stack_size;
    f->entry = entry;
    f->param = param;
    if (getcontext(&f->ctx) != 0) { free(f->stack); free(f); return NULL; }
    f->ctx.uc_stack.ss_sp   = f->stack;
    f->ctx.uc_stack.ss_size = stack_size;
    f->ctx.uc_link          = NULL;
    makecontext(&f->ctx, fiber_trampoline, 0);
    return f;
}

void SwitchToFiber(void *fiber) {
    Fiber *target = (Fiber *)fiber;
    char here;    /* an address in THIS frame, i.e. on the outgoing stack */
    if (!target || target == g_current_fiber) return;
    Fiber *prev = g_current_fiber;
    prev->suspended_low = &here;
    g_current_fiber = target;
    swapcontext(&prev->ctx, &target->ctx);
}

int FiberSnapshotSupported(void) { return 1; }

/* The live extent of a suspended fiber's stack: [low, stack + stack_size).
 * Stacks grow down on every target this backend builds for. Returns 0 when
 * the fiber has no stack of its own, has never been suspended, or is the one
 * running now -- none of which can be snapshotted. */
static size_t fiber_live_extent(Fiber *f, char **low_out) {
    char *base, *top, *low;
    if (!f || !f->stack || !f->stack_size || f == g_current_fiber) return 0;
    if (!f->suspended_low) return 0;
    base = (char *)f->stack;
    top  = base + f->stack_size;
    low  = (char *)f->suspended_low;
    if (low < base || low >= top) return 0;      /* not this fiber's stack */
    low = low - FIBER_SNAPSHOT_SLACK < base ? base : low - FIBER_SNAPSHOT_SLACK;
    *low_out = low;
    return (size_t)(top - low);
}

size_t FiberSnapshotBound(void *fiber) {
    char *low;
    size_t live = fiber_live_extent((Fiber *)fiber, &low);
    return live ? live + sizeof(FiberSnapshotHeader) : 0;
}

size_t FiberSnapshotSave(void *fiber, void *out, size_t capacity) {
    Fiber *f = (Fiber *)fiber;
    FiberSnapshotHeader hdr;
    char *low;
    size_t live = fiber_live_extent(f, &low);
    if (!live || !out) return 0;
    if (capacity < live + sizeof(hdr)) return 0;
    hdr.magic       = FIBER_SNAPSHOT_MAGIC;
    hdr.version     = FIBER_SNAPSHOT_VERSION;
    hdr.stack       = f->stack;
    hdr.stack_bytes = live;
    hdr.low         = low;
    hdr.ctx         = f->ctx;
    memcpy(out, &hdr, sizeof(hdr));
    memcpy((char *)out + sizeof(hdr), low, live);
    return live + sizeof(hdr);
}

int FiberSnapshotLoad(void *fiber, const void *in, size_t size) {
    Fiber *f = (Fiber *)fiber;
    FiberSnapshotHeader hdr;
    if (!f || !in || size < sizeof(hdr)) return 0;
    if (f == g_current_fiber) return 0;          /* cannot rewind ourselves */
    memcpy(&hdr, in, sizeof(hdr));
    if (hdr.magic != FIBER_SNAPSHOT_MAGIC ||
        hdr.version != FIBER_SNAPSHOT_VERSION) return 0;
    /* Same fiber, same allocation: the stack bytes are restored to the very
     * addresses they were captured from, which is what keeps every pointer
     * into the stack (saved frame pointers, pointers to locals) valid. */
    if (hdr.stack != f->stack) return 0;
    if (size < sizeof(hdr) + hdr.stack_bytes) return 0;
    if ((char *)hdr.low < (char *)f->stack ||
        (char *)hdr.low + hdr.stack_bytes > (char *)f->stack + f->stack_size)
        return 0;
    memcpy(hdr.low, (const char *)in + sizeof(hdr), hdr.stack_bytes);
    f->ctx = hdr.ctx;
    f->suspended_low = hdr.low;
    return 1;
}

void DeleteFiber(void *fiber) {
    Fiber *f = (Fiber *)fiber;
    if (!f) return;
    free(f->stack);   /* free(NULL) is fine for thread-origin fibers */
    free(f);
}

unsigned long GetLastError(void) { return 0; }

#endif /* __ANDROID__ */
#else  /* _WIN32 */

/* A Win32 fiber is an opaque kernel object: its stack extent and saved
 * register file are not reachable through the documented API, so there is no
 * honest way to copy one out and put it back. Reported unsupported, and
 * run-ahead declines rather than rewinding the machine out from under a fiber
 * that stays where the speculation left it. */
#include "fiber_compat.h"
int    FiberSnapshotSupported(void) { return 0; }
size_t FiberSnapshotBound(void *fiber) { (void)fiber; return 0; }
size_t FiberSnapshotSave(void *fiber, void *out, size_t capacity) {
    (void)fiber; (void)out; (void)capacity; return 0;
}
int    FiberSnapshotLoad(void *fiber, const void *in, size_t size) {
    (void)fiber; (void)in; (void)size; return 0;
}

#endif /* !_WIN32 */
