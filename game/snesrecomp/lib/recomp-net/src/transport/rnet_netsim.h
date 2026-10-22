/*
 * rnet_netsim.h — receive-side link simulator (added latency, jitter).
 *
 * WHY THIS EXISTS
 *
 * Rollback paths are reachable only at particular link timings, and a
 * loopback soak has none. Measured: with the forced-mispredict injector at
 * every 5 ticks over 505 episodes, the host's TipHold tip-extend branch fired
 * ZERO times on both peers — the peer's OP_COMMIT returns at ~0 RTT, so
 * TipHold is cleared before a late edge can ever land in it. Every soak we
 * had was silently exercising a subset of the machine and reporting PASS.
 *
 * A test that cannot reach the code is not evidence about the code. This is
 * the missing instrument.
 *
 * SHAPE
 *
 * Datagrams are held on RECEIVE, not send. Receive-side needs no clock shared
 * with the peer and no cooperation from it: each process delays what it takes
 * off its own socket. The consequence is that added RTT is the SUM of both
 * peers' settings — RNET_SIM_LATENCY_MS=25 on both is ~50 ms of added
 * round trip, not 25.
 *
 * Jitter reorders: a datagram given a shorter delay overtakes one already
 * held. That is the point (UDP reorders), and it is why delivery pops the
 * earliest DUE entry rather than the oldest.
 *
 * ENVIRONMENT (read once per transport, at first receive)
 *
 *   RNET_SIM_LATENCY_MS   one-way delay added on this side. Unset or 0
 *                         disables the simulator entirely — nothing is
 *                         allocated and the receive path is untouched.
 *   RNET_SIM_JITTER_MS    uniform +/- spread around the latency. Clamped so a
 *                         delay never goes negative. Default 0.
 *   RNET_SIM_SEED         xorshift seed, so a jittered run repeats. Default
 *                         fixed, NOT time-derived: a nondeterministic test
 *                         instrument cannot be used to confirm a fix.
 *   RNET_SIM_LOSS_PCT     percentage of arriving datagrams to drop, 0-90.
 *                         Latency and jitter delay and reorder; only loss
 *                         reaches the paths that must survive a message never
 *                         arriving at all -- a seal row that is never sent
 *                         back, a POST that never lands. Independent of
 *                         latency, so loss can be tested on a fast link.
 *                         Drops are counted and reported with the banner, so a
 *                         run's actual loss is in the log rather than assumed
 *                         from the setting.
 *
 * SAFETY
 *
 * This is compiled into every build, exactly like SNES_RB_FORCE_MISPREDICT,
 * because a harness that needs a special build stops testing the thing that
 * ships. It cannot engage without the environment variable, and when it does
 * engage it says so on stderr at startup AND every 10 seconds thereafter, so
 * a log from a degraded run can never be read as a clean one.
 */

#ifndef RNET_NETSIM_H
#define RNET_NETSIM_H

#include "recomp_net/types.h"
#include "platform/rnet_platform.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RNetNetSim RNetNetSim;

/* Returns NULL when RNET_SIM_LATENCY_MS is unset/0 — the caller then uses the
 * plain receive path and pays nothing. */
RNetNetSim *rnet_netsim_create(void);
void rnet_netsim_destroy(RNetNetSim *sim);

/* Hold an arrived datagram until its delay elapses. `src` is remembered so
 * peer learning happens against the datagram actually delivered, not against
 * whatever arrived while earlier ones were still held.
 * Returns 1 when taken, 0 when the hold queue is full (caller must deliver it
 * immediately rather than lose it; the simulator says so loudly). */
int rnet_netsim_hold(RNetNetSim *sim, const rnet_u8 *buf, size_t len,
                     const struct sockaddr_in *src);

/* Deliver the earliest datagram whose delay has elapsed. Returns bytes
 * written, or 0 when nothing is due yet. `out_src` may be NULL. */
int rnet_netsim_due(RNetNetSim *sim, rnet_u8 *buf, size_t cap,
                    struct sockaddr_in *out_src);

/* Re-announce the banner if the interval has passed. Cheap; call per receive. */
void rnet_netsim_heartbeat(RNetNetSim *sim);

#ifdef __cplusplus
}
#endif

#endif /* RNET_NETSIM_H */
