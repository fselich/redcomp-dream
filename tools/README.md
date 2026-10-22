# Research tools

Durable scripts, routes and patches copied from the local research workshop.
Raw captures, frames, logs and dumps stay outside the repository because they
are derived from the ROM.

| Path | Contents |
|---|---|
| `scripts/ws-native-map-spike/` | frame/debug helpers, deterministic menu/race routes, upload/stage probes |
| `scripts/ws-offwindow/` | `world_key_check.py`, BG1 probe/scoring helpers |
| `scripts/ws-leading-edge/` | ring/ghost/margin scoring, pixel check, disassembly, route set |
| `scripts/ws-map-decode/` | RNC unpacker, track table/cell/shape/render tools |
| `scripts/ws-ghost-lane/`, `ws-ghost-xhigh/`, `ws-radar/`, `ws-source*`, `ws-ultrawide/` | phase-specific verification scripts |
| `patches/` | local framework and probe patches, preserved per research phase |

The in-game routes assume the build is launched from `game/build-local/` with
the ROM path supplied by the host. See the root `README.md` and the phase
reports in `openspec/` for the capture windows and expected values.
