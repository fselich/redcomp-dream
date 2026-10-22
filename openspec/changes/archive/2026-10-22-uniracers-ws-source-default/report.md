# uniracers-ws-source-default — report

Date: 2026-10-21
Status: **done**. With widescreen on and no `SNESRECOMP_WS_*` environment, BG1
margins now use `source`.

## Finding that motivated it

Before this change, normal play (`Widescreen = 1`, no environment) showed the
**raw ring wrap** in the margins. `WsOffwindowMode()` returned 0, and the clamp
only applied when `SNESRECOMP_WS_TRACK_CLAMP` was set by hand. The
"clamp is the default" statements in older docs do not hold for this tree.

## Change

`src/game_rtl.c` (patch `work/ws-source-default/patches/ws-source-default.diff`):

| `SNESRECOMP_WS_OFFWINDOW` | Policy |
|---|---|
| unset | `source`, or `clamp` if the legacy `SNESRECOMP_WS_TRACK_CLAMP` is set |
| `source` / `ring` / `ghost` | as before |
| `clamp` | global BG1 clamp (new name for the old opt-in) |
| `wrap` / `off` / `0` | raw ring (pre-fix look) |
| other | `source` + one warning |

- Named modes: `WS_OW_WRAP/RING/SOURCE/GHOST/CLAMP`.
- Margin hooks run only for `ring`/`source`/`ghost`.
- The edge-repeat experiment only applies under `wrap`.
- `[ws-ow]` stderr lines need `SNESRECOMP_WS_OFFWINDOW_LOG` or
  `SNESRECOMP_WS_SOURCE_LOG`.

## Verification (Dragster 2500 unless noted; `work/ws-source-default/`)

| Run | SHA-256 (16) | Notes |
|---|---|---|
| native (WS off) | `3417f91afc8f6d47` | unchanged |
| default (`SNESRECOMP_WS=1` only) | `4b138431bdc04382` | = explicit `source` = the `uniracers-ws-source` capture |
| `clamp` | `e09fd18df8bb22be` | = legacy `SNESRECOMP_WS_TRACK_CLAMP=1` |
| `wrap` | `4b138431bdc04382` | equal to `source` here only because Dragster's strip is periodic; Monster 2300 differs (`b682d84a…` vs `30af0020…`) |
| `ring` | `7abf7e41e321cc3f` | leading side masked, as expected |

- Probe-scored default run, Dragster 2300–2500 (`logs/validation-default.txt`):
  leading 140,000 correct / 0 wrong / 0 masked; trailing 139,104 / 0 / 0.
- No `[ws-ow]` lines in any run without a log switch.
