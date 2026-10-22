#ifndef RETCOMM_RBENGINE_H
#define RETCOMM_RBENGINE_H

/*
 * retcomm-rbengine — the offline rollback core for recomp engines.
 *
 * No dependencies, and nothing here knows a peer exists:
 *   - opaque tick-keyed snapshot ring
 *   - portable monotonic clock
 *
 * Rewind and run-ahead can link it with netplay compiled out. Everything that
 * compares against a peer (admission scheduler, hash-confirm watermark, input
 * history with invent/promote, RB_POST tip filter) lives in recomp-net.
 *
 * Engines supply serialize/deserialize; this library never advances sim.
 */

#include "retcomm_rbengine/mono_ms.h"
#include "retcomm_rbengine/snap_ring.h"

#endif /* RETCOMM_RBENGINE_H */
