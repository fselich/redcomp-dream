# Uniracers widescreen spike (Phase E1) — report

Date: 2026-10-20
Change: `uniracers-widescreen-spike`
Outcome: **16:9 preview works** (342×224, genuinely wider, HUD clamped, 4:3
preserved). P5/P6 discriminator not proven; P-U1 HDMA builder still blocked;
objects/cull/spawn deferred.

## Verdict

- 4:3 baseline is intact: frame 2400 matches the dispatch-fix baseline exactly
  (`0bd41144…`).
- 16:9 preview is enabled by `SNESRECOMP_WS=1` (or `[Graphics] Widescreen`):
  **342×224**, with new world columns on both sides, not a stretched picture.
- BG3 HUD is clamped to the native 256 columns and stays readable.
- The wide frame is presentation-only; no camera, collision, RNG or input
  changes were made.
- The HDMA table builder was not found; the margins are natural PPU rendering,
  not extended per-line curves (P-U1 blocked).
- No live-race WRAM discriminator was proven; the wide mode is a developer
  flag, not a gameplay-gated feature.

## P16 baseline

| Capture | Size | SHA-256 | Non-black | Colors | Luminance |
|---|---|---:|---:|---:|---:|
| 4:3 (`SNESRECOMP_WS` unset) | 256×224 | `0bd41144…` | 97.04% | 43 | 145.27 |
| 16:9 (`SNESRECOMP_WS=1`) | 342×224 | `f69307eb…` | 97.71% | 43 | 146.84 |

The 4:3 hash matches the preserved dispatch-fix baseline; the wide build does
not change the authentic frame.

## Wide-field evidence

The wide capture adds **43 columns per side** (342 = 256 + 43 + 43). The centre
content is the same scale; the extra columns show more of the BG background and
the checkered track. The HUD (`RACE`, timer) remains inside the native 256 and
is legible.

Implementation:

- `src/main.c`: `native_widescreen = 1`, `widescreen_supported = 1`,
  `prepare_frame` returns 342×224 only when wide mode is enabled.
- `src/game_rtl.c`: `PpuSetWidescreenLayerClamp(g_ppu, 1u << 2)` clamps BG3 (the
  HUD) to the native columns before the frame is drawn.
- Wide mode: env `SNESRECOMP_WS=1` or the config `[Graphics] Widescreen`; 4:3
  remains the default.

Patch: `work/ws-spike/patches/widescreen-preview.diff`.

## P5/P6 discriminator

Menu and race WRAM dumps were compared (frames 600 and 2015). There are 65
addresses where the menu byte is 0 and the race byte is 1, but none was proven
to mean "live race" across pause/cutscene states. Examples:
`$000B4`, `$00CE6`, `$00D0B`, `$0119F`, `$0121F`.

The wide mode is therefore gated by the developer flag, not by a gameplay
state. Risk: enabling it globally also widens menus and transitions. A follow-up
must find/verify the discriminator before this becomes a player-facing option.

## P-U1 / HDMA builder

The HDMA path is confirmed: ch3/ch4 write `$210F`/`$210D` per line. Current
A-bus pointers are `$7E:8262`/`$7E:8280`; the per-frame block loads the ch4
pointer from `$7E:2046`. The WRAM at the current pointers was zero at the dump
boundary, so the table is consumed/rebuilt per frame.

The builder was not identified in this spike. The wide margins are the PPU's
natural rendering of the existing BG tilemaps, not per-line scroll values
extended beyond the native 256. **P-U1 remains blocked**; on curves/hills the
margins are expected to diverge.

## Deferred object gaps (P7–P13)

Cull, OAM widening, spawn/activation and staging are not implemented. At the
captured frame (2400, mostly flat track) no obvious object pop-in was visible,
but that is not evidence that the wide view is correct on curves or with
opponents entering the margins. The next object work is P7/P8 (cull + OAM
reject) followed by P9–P13 (spawn/staging).

## Simulation

Wide mode changes presentation only: the 4:3 frame is unchanged, and the
project touches only `prepare_frame`, the host descriptor and the BG3 clamp.
No camera, collision, RNG, input or save-state code was modified.

## What this does not prove

- That 16:9 is playable or correct on curves, hills, or with opponents visible.
- That the HUD placement is final (clamp only; no anchor/elastic).
- That objects are culled/spawned correctly in the margins.
- That the HDMA per-line curves extend beyond 256.
- That the wide mode can be gated to live race (P5/P6 unproven).

## Recommendation

1. **`uniracers-ws-hdma`** — find the HDMA table builder (trace `$7E:8262/$8280`,
   `$2046`) and extend the per-line scroll for the margins (P-U1).
2. **`uniracers-ws-discriminator`** — prove the live-race byte/word across menu,
   race and pause (P5/P6).
3. **`uniracers-ws-objects`** — P7/P8 cull + OAM, then P9–P13 spawn/staging.

The 16:9 preview is a successful spike, not a shippable widescreen mode.

## Artifacts

- Workspace: `work/ws-spike/` (gitignored).
- Patch: `patches/widescreen-preview.diff`.
- Frames: `frames/ws-off-2400.ppm` (4:3), `frames/ws-on-2400.ppm` (16:9),
  PNG previews.
- Dumps: `dumps/menu.wram.bin`, `dumps/race.wram.bin`.
- Metrics: `metrics/hashes.txt`, `metrics/frame-metrics.txt`,
  `metrics/menu-race-diff.txt`.
- This report contains no generated C, no ROM bytes, and no game data.
