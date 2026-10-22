#pragma once
/*
 * fiber_compat.h — cross-platform fiber API for cooperative game task
 * scheduler (mmx_rtl.c).
 *
 * The scheduler cooperatively switches between a "scheduler" fiber and one
 * fiber per task slot, each with its own C stack so a task can yield mid-call
 * and resume later. On Windows this is the native Fiber API. On POSIX there is
 * no Fiber API, so we provide an equivalent backed by ucontext_t — same five
 * entry points, same semantics — so mmx_rtl.c is identical on both platforms.
 */
#include <stddef.h>   /* size_t, for the snapshot API below (both platforms) */

#ifdef _WIN32
#  include <windows.h>
#else
#  include <stddef.h>

#  ifndef CALLBACK
#    define CALLBACK   /* no calling-convention decoration on POSIX */
#  endif

typedef void (*FiberProc)(void *);

/* Promote the current thread to a fiber; returns its handle (or NULL). */
void *ConvertThreadToFiber(void *param);

/* Create a fiber with its own stack that will run entry(param) when first
 * switched to. Returns a handle (or NULL on failure). */
void *CreateFiber(size_t stack_size, FiberProc entry, void *param);

/* Cooperatively switch execution to the given fiber. */
void  SwitchToFiber(void *fiber);

/* Free a fiber created by CreateFiber. */
void  DeleteFiber(void *fiber);

/* Win32 parity for the scheduler's error logging; always 0 on POSIX. */
unsigned long GetLastError(void);
#endif

/*
 * Snapshotting a SUSPENDED fiber's execution position.
 *
 * A game whose guest runs on a fiber keeps its execution position -- the whole
 * C call chain -- in that fiber's stack, and no guest-state snapshot contains
 * it. Rewinding the machine without rewinding the fiber leaves the two out of
 * step: the frame after the rewind resumes where the speculation left off
 * while the RAM says otherwise. Run-ahead needs both to move together.
 *
 * Only the ucontext backend can do this, and it is honest about it: a fiber
 * there is a context plus a stack this file allocated, both of which can be
 * copied out and put back at the same addresses, so every interior pointer
 * stays valid. A Win32 fiber is opaque and an Android fiber is a real thread;
 * both report unsupported rather than pretend.
 *
 * Save/load only a fiber that is SUSPENDED (not the one currently running).
 */
int    FiberSnapshotSupported(void);
/* Upper bound for FiberSnapshotSave on this fiber right now. 0 = cannot. */
size_t FiberSnapshotBound(void *fiber);
/* Bytes written, or 0 on failure (unsupported, running, or capacity short). */
size_t FiberSnapshotSave(void *fiber, void *out, size_t capacity);
/* Non-zero on success. The blob must come from this same fiber, this run. */
int    FiberSnapshotLoad(void *fiber, const void *in, size_t size);
