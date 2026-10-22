# Host integration

## Layers

```text
Engine facade (snes_netplay / psx_netplay / …)
  ├── recomp-net         RNetSession tips + RNetRbSession episodes,
  │                      admission scheduler, input hist, hash_confirm
  └── retcomm-rbengine   snap ring, monotonic clock (no dependencies)
```

Offline rewind / run-ahead links `retcomm-rbengine` alone. Netplay links both.
Neither library depends on the other.

## Snap ring

```c
RbeSnapVTable vt = {
    .ctx = host,
    .serialize = host_save_blob,   /* mallocs *out */
    .deserialize = host_load_blob,
};
rbe_snap_ring_save(ring, tick, &vt);
rbe_snap_ring_load(ring, load_tick, &vt);
```

Or call `rbe_snap_ring_store` with a prebuilt blob.

## Scheduler clock

recomp-net's `RNetSchedGates.now_ms` is required. `rbe_mono_ms` is a suitable
source:

```c
static uint32_t host_now(void *ctx) { (void)ctx; return rbe_mono_ms(); }
```

## MotK migration aliases

When switching MotK off in-tree helpers, map:

| Old | New |
|-----|-----|
| `np_sched_*` / `PsxNpSchedBridge` | `rnet_sched_*` / `RNetSchedBridge` (recomp-net) |
| `netplay_hc_*` / `NetplayHashConfirm` | `rnet_hc_*` / `RNetHashConfirm` (recomp-net) |
| `netplay_ih_*` / `NetplayInputHist` | `rnet_ih_*` / `RNetInputHist` (recomp-net) |
| `netplay_snap_ring_*` | `rbe_snap_ring_*` (+ vtable save/load) |
| `netplay_rb_peer_post_tip_ok` | `rnet_rb_peer_post_tip_ok` (recomp-net) |
| `PSX_RB_*` env | `RBE_RB_*` (PSX_* still accepted as fallback) |

Keep MotK `psx_netplay_rb_*` episode driver in the PSX runtime.
