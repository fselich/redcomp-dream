# retcomm-rbengine

The **offline rollback core** for recomp engines: the pieces of rollback that
work with netplay compiled out. No dependencies.

| Module | Role |
|--------|------|
| `snap_ring` | Tick-keyed opaque snapshot ring (+ optional serialize vtable) |
| `mono_ms` | QPC / CLOCK_MONOTONIC milliseconds |

Rewind and run-ahead can link this library on its own. Before the split an
engine that wanted the snapshot ring without netplay had to compile
`rbe_snap_ring.c` as a bare source file, because the library linked
recomp-net and refused to configure without it.

## What moved to recomp-net

Everything that compares against a peer. These now live in
[`recomp-net`](https://github.com/RetroPortingToolKit/recomp-net) beside the
session and the episode FSM they serve:

| Was (rbengine) | Now (recomp-net) |
|--------|------|
| `retcomm_rbengine/sched.h`, `rbe_sched_*`, `RbeSchedBridge`, `RbeSchedGates` | `recomp_net/sched.h`, `rnet_sched_*`, `RNetSchedBridge`, `RNetSchedGates` |
| `retcomm_rbengine/hash_confirm.h`, `rbe_hc_*`, `RbeHashConfirm`, `RBE_HC_RING` | `recomp_net/hash_confirm.h`, `rnet_hc_*`, `RNetHashConfirm`, `RNET_HC_RING` |
| `retcomm_rbengine/input_hist.h`, `rbe_ih_*`, `RbeInputHist`, `RBE_INPUT_HIST_*` | `recomp_net/input_hist.h`, `rnet_ih_*`, `RNetInputHist`, `RNET_INPUT_HIST_*` |
| `retcomm_rbengine/rb_post.h`, `rbe_rb_peer_post_tip_ok` | `recomp_net/rb_post.h`, `rnet_rb_peer_post_tip_ok` |

The scheduler's `RBE_RB_*` environment knobs kept their names.

## Build

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

```cmake
add_subdirectory(path/to/retcomm-rbengine)
target_link_libraries(your_host PRIVATE retcomm_rbengine)
```

## Snapshot ring sketch

```c
#include "retcomm_rbengine/retcomm_rbengine.h"

RbeSnapRing *ring = rbe_snap_ring_create(RBE_SNAP_RING_DEFAULT_DEPTH);
RbeSnapVTable vt = {
    .ctx = host,
    .serialize = host_save_blob,   /* mallocs *out */
    .deserialize = host_load_blob,
};
rbe_snap_ring_save(ring, tick, &vt);
rbe_snap_ring_load(ring, load_tick, &vt);
```

Game-specific savestate serialize stays in the engine.

## MotK provenance

Lifted from `psxrecomp/runtime` (`netplay_snap_ring`) with PSX types removed.
The modules that moved to recomp-net came from `psx_netplay_sched`,
`netplay_hash_confirm`, `netplay_input_hist` and `netplay_rb_post`.

## License

MIT — see [LICENSE](LICENSE).
