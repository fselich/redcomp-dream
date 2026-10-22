## Context

Read first:

- `docs/UNIRACERS_BG1_RING_FINDINGS.md`: ring formula, 16×16 tiles, margin
  measurements.
- `openspec/changes/uniracers-ws-leading-edge/report.md`: Option A
  implementation, invariants, why the host VRAM bracket was rejected, Option C
  no-go.

State this change starts from (verified 2026-10-21):

- BG1 ring: 32×32 cells × 16 px at map base `0x0C00`;
  `map_x = ((hscroll + x) >> 4) & 31`. World cell =
  `(unwrapped_scroll + screen) >> 4`, 0 conflicts on all five tracks, so the
  level map is static per world cell.
- Option A lives in `src/game_rtl.c`:
  - `s_ow_seen[32][32]` is filled per visible line by `WsOffwindowLine` through
    `g_snes_beam_line_hook`.
  - `WsOffwindowTile` is the renderer margin hook (`g_ppu_margin_tile_hook`,
    called in `PpuDrawBackgroundBig` before the probe records `served`).
  - Unverified margin cells get the transparent character found by
    `WsOwFindBlankChar`.
- Build: `build-local/` (Release, `SNESRECOMP_ENABLE_TRACE=ON`), carrying the
  local patches `work/ws-offwindow/patches/bgprobe.diff` and
  `work/ws-leading-edge/patches/ws-leading-edge.diff`.

### Evidence for the ghost lane

**Coverage.** Offline, same-run upper bound
(`work/ws-leading-edge/scripts/lookahead_coverage.py`, on
`work/ws-offwindow/logs/bgprobe-wide-*.csv`). Share of leading-margin cells
whose world cell becomes natively visible within K frames:

| K | 0 | 1 | 2 | 3 | 4 | 5–8 |
|---|---|---|---|---|---|---|
| Dragster E (→ ~14 px/frame) | 0 % | 28.2 % | 56.4 % | 84.3 % | **100 %** | 100 % |

On Zoom Zoo the coverage stays low (W 15–32 %, E 57–62 % for K = 0..8). The side
cells of a vertical loop never enter the native view horizontally. The line-art
says they are background, so the Option A mask is the right result there. The
ghost lane is not expected to change Zoom Zoo much.

**Cost of the naive approach.** The framework's generic run-ahead
(`SNESRECOMP_RUNAHEAD=4`, Dragster route, 2,500 frames, headless) gave
`fps=26.4`, `cpu_seconds=48.1` against `15.0` with it off: +13.2 ms/frame, about
3.3 ms per speculative frame including the snapshot. Rollback snapshot size:
**1,067,148 bytes**. Logs: `work/ws-leading-edge/logs/runahead-cost-{0,4}.log`.
A full N-frame run-ahead every frame is therefore too expensive. Hence the
"lane" design below.

### Framework facts the implementer needs

| Fact | Where |
|---|---|
| `after_run_frame` host hook runs after `RtlRunFrame`, before `CaptureSimulationFrame`/draw | `snesrecomp/runner/src/desktop/host_main.h:121`; main loop `host_main.c` ~l.4336 (the ~l.4078 site is the netplay loop) |
| The hook gets no inputs; the frame's pad words are in `g_snes->input1_currentState` / `input2_currentState` (packed word = `in1 \| in2 << 12`) | `common_rtl.c` `RtlRunFrame` ~l.720 |
| Rollback snapshot pair | `RtlRollbackSaveToMemory` / `RtlRollbackLoadFromMemory` / `RtlRollbackSnapshotBound`, `common_rtl.h` ~l.381 |
| Speculation helpers | `RtlSetSpeculativeFrame`, `RtlAudioProducerCursor`, `RtlAudioRewindProducer`, `common_rtl.h` ~l.411; reference use in `snes_runahead.c` `snes_runahead_run_frame` |
| `snes_frame_counter` is incremented inside `RtlRunFrame` and is **not** in the snapshot | `common_rtl.c:835` |
| **Not in the snapshot:** beam driver `s_resume_pc`, `s_wai_halted` (file statics) | `beam_frame_driver.c:42,45` |
| Draw = replay of the field's raster journal + HDMA, no guest code; per-line hook `g_snes_beam_line_hook` runs before each line is rasterized | `beam_frame_driver.c` `snes_beam_frame_driver_draw_ppu_frame_observed` |
| Host statics advanced inside `run_frame`: widescreen cull `s_cull_on`, `s_cull_base_l/r`, `s_cull_applied_l/r` | `src/game_rtl.c` ~l.141 |
| Probe gates on `snes_frame_counter` | `PpuBgProbeActive` in `ppu.c` |

## Goals / Non-Goals

**Goals:**

- Leading-margin cells sourced from the game's own writes in a speculative
  future, served only for an exact world-cell match.
- Real frames byte-identical to `ring` mode (guest state and native pixels).
- Steady-state cost ≤ 5 ms/frame on the reference machine, measured.
- 0 wrong served margin cells on the five tracks.

**Non-Goals:**

- Netplay, 2P, or coexistence with the framework run-ahead.
- Decoding `$7F` / stage 2 (Option C).
- Changing the default presentation in this change.

## Decisions

### D1 — Ghost lane, not per-frame run-ahead

Keep one persistent ghost snapshot that runs **ahead** of the real machine and
advances about one frame per real frame:

```text
after_run_frame(real frame N):
  if !eligible(): invalidate ghost; return       # not wide race, mode != ghost, netplay, RunAhead>0
  save host side state (D3)
  real_blob = RollbackSave()
  if ghost invalid or resync due:
      ghost starts from the current real state (no load), lead = 0
  else:
      RollbackLoad(ghost_blob); lead -= 1        # real advanced one frame
  steps = lead < K ? min(MAXSTEPS, K - lead) : 1  # catch up gradually, never burst
  repeat steps:
      RtlRunFrame(current real inputs)           # speculative, see D3
      harvest (D2); lead += 1
  ghost_blob = RollbackSave()
  RollbackLoad(real_blob)
  restore host side state (D3)
```

Parameters (env, test-only):

- `SNESRECOMP_WS_GHOST_AHEAD` = K, default 4 (measured 100 % at K = 4).
- `SNESRECOMP_WS_GHOST_MAXSTEPS`, default 2.
- `SNESRECOMP_WS_GHOST_RESYNC` = frames between forced resyncs, default 600.
  (The planned default 60 produced a 25 ms p99 from resync snapshots; 600
  measured 9.6 ms with equal or better coverage. Input/scene changes still
  resync immediately.)

Resync also happens when the packed input word changes, on an Option A scene
reset (map base / BGMODE change), and whenever eligibility toggles.

Why: this is about 1 emulated frame per real frame in steady state (≈3.3 ms
measured per speculative frame), instead of K. Catch-up after a resync is
spread over frames (MAXSTEPS) to avoid a K-frame spike.

Alternatives rejected:

- Generic run-ahead: 26 fps measured.
- Burst every M frames: periodic 13 ms spikes.
- Delaying presentation by K frames: adds input lag.

### D2 — Harvest = Option A observation on the ghost

After each ghost `RtlRunFrame`, replay its field with
`snes_beam_frame_driver_draw_ppu_frame()` and a ghost-only
`g_snes_beam_line_hook`. That hook computes per line exactly what
`WsOwObserveLine` computes (line scroll, world cell, ring cell, VRAM map word)
and writes it into a separate table:

```c
typedef struct { int32_t wx, wy; uint16_t tile; uint8_t valid; } WsGhostCell;
static WsGhostCell s_gh[32][32];     /* ring-indexed, like s_ow_seen */
```

- Ghost unwrap: start from the real lane's last unwrapped scroll
  (`s_ow_unwrapped_h/v`, `s_ow_prev_h/v`) at resync. Continue with the same
  shortest-delta rule inside the ghost. Keep a separate accumulator; never touch
  `s_ow_*` from the ghost.
- Apply the same scene guard as Option A: if the ghost's line-1 map base or
  BGMODE differ from the real race scene, drop the harvest for that frame.
- Do **not** install `g_ppu_margin_tile_hook` during the ghost draw.
- Suppress the probe during ghost draws (it gates on `snes_frame_counter`): set
  the counter to a sentinel such as `INT_MIN / 2` around the ghost draw, then
  restore it (D3).
- Rasterizing the ghost field costs time. If the cost budget fails, the
  optimisation is a journal walk without `ppu_runLine`. Verify first that it
  leaves the PPU/DMA state identical to a full draw.

### D3 — What must be saved/restored around the lane

| State | Why | How |
|---|---|---|
| Whole machine | guest | `RtlRollbackSaveToMemory` / `LoadFromMemory` (≈1.07 MB, allocate once with `RtlRollbackSnapshotBound()`) |
| `s_resume_pc`, `s_wai_halted` | not in the snapshot; the ghost run changes them | **framework patch:** add `snes_beam_frame_driver_get_exec(uint32_t *pc, bool *wai)` / `set_exec(...)` to `beam_frame_driver.{h,c}`. Save before, restore after loading the real blob. Also save/restore them into the ghost lane (the ghost has its own pair). |
| `snes_frame_counter` | current rollback residue already carries it; the lane still uses a sentinel around ghost draws to mute the probe | rely on the blob; restore via the real load |
| Speculative flag | mod hooks, perf log | `RtlSetSpeculativeFrame(true)` around ghost frames |
| Audio | ghost produces samples | `RtlAudioProducerCursor` before, `RtlAudioRewindProducer` after |
| Cull statics `s_cull_*` | `WsApplyCull` runs in `run_frame` | copy into a struct and restore; keep the ghost's own copy with the ghost blob |
| `g_snes->disableRender`, line hook, margin hook | host | save/restore |
| Raster journal (`s_field[2]`, cursors, OAM port) | the real draw replays it; the ghost run overwrites it | `ppu_rasterStateSize/Save/Load`; save before, restore after |
| Host `Ppu` fields outside the guest saveload (caches, overscan/interlace, render pointers/flags, ports) | the real draw uses them; without them the native image is corrupted | save/restore the whole `Ppu` struct around the lane |
| Interp bridge / other runner statics | **unknown** | covered by the D5 invariant test; if it fails, find the static with a binary search on frame hashes |

### D4 — Serving rule in the margin hook

Extend `WsOffwindowTile` for mode `ghost`. Order:

1. If the `s_ow_seen` cell is verified, serve the ring tile (Option A, unchanged).
2. Else, if `s_gh[cy][cx]` is valid with `wx`/`wy` equal to the needed world
   cell, serve `s_gh[cy][cx].tile`. Log it with `masked = 0` and the served word.
3. Else, mask (Option A, unchanged).

The served value is a map word. Its pixels come from the **real** VRAM character
data. If BG1 characters (CHR) are streamed during the race, a ghost map word
could point at a character that is not loaded in real VRAM yet. Check this
before trusting the map-word score (task 1.4).

### D6 — Corrections found in implementation

- The raster journal and the host `Ppu` fields are outside the rollback blob.
  Saving only the blob and the beam exec corrupted the real native image (a
  large misplaced/missing background region) while the CPU/WRAM trace stayed
  identical. The final lane saves/restores both. With K = 0 the image matched
  `ring` exactly, which isolated the defect.
- BG1 CHR is not static from boot. It changes at race entry (Dragster
  1381–1430, Zoom Zoo 1404–1510), then is stable. The lane hashes the
  `PPU_bgTileAdr(ppu,0)` region and only serves ghost cells after 30 stable
  frames; `SNESRECOMP_WS_GHOST_CHR_LOG` records the hash changes.
- `snes_frame_counter` is in the current rollback residue, contrary to the
  planning note; the real load restores it.
- A full ghost raster is fast enough; the journal-walk optimisation was not
  needed. The resync period, not `MAXSTEPS`, dominated the p99.

### D5 — Acceptance

1. **Real-lane invariance:** with `ghost`, Dragster frame 2500 WRAM/VRAM/CPU/DMA
   dumps and the native-window pixels equal the `ring` run. Native 4:3 with the
   switch unset stays `3417f91a…df44`. Also run a per-frame state trace,
   `ring` vs `ghost`, over 2,500 frames: it must be identical.
   `SNESRECOMP_STATE_TRACE=<path>` writes `frame, crc32(WRAM), A, X, Y, S, D,
   DB, PB, P` per frame. It is written **after** `after_run_frame`, so it sees
   the restored real state.
2. **0 wrong served cells:** `world_key_check.py --served --mask-log` on
   Dragster →/←, Zoom Zoo, Bowl, Switcher, Monster (routes and windows from
   `uniracers-ws-leading-edge/report.md` §3).
3. **Coverage:** the leading-side masked count drops against `ring`. Target on
   Dragster →: ≥ 95 % of verifiable leading cells served.
4. **Pixels:** for a sample of ghost-served cells, the 16×16 rendered block
   equals the same world cell's native block when it becomes visible (guards
   CHR streaming).
5. **Cost:** process CPU seconds (`frame service ... cpu_seconds` line in the
   run log) over 2,500 frames against `ring`: ≤ 5 ms/frame on average. Also
   report p99/worst from `SNESRECOMP_FRAME_TIMING=<csv>` (column `hook_ms` =
   time in `after_run_frame`).
6. **Input divergence:** a route that changes input often (bounces, reversal,
   `route-dragster-left`) still gives 0 wrong cells. Coverage may drop.

## Risks / Trade-offs

- [Runner state outside the snapshot besides the beam driver] → D5.1 per-frame
  trace; fix by adding accessors; abort the change if it is not tractable.
- [CHR streamed during race] → D5.4. If it fails, serve only cells whose
  character data also matches, or harvest the CHR range as well.
- [Cost over budget] → journal walk without rasterizing (D2); lower K
  dynamically from the measured scroll speed (K = ceil(59 / speed_px) + 1);
  skip ghost steps when no needed leading cell is missing.
- [Ghost diverges from reality (input changes)] → harvested cells are still
  world-keyed and written by the game, so they stay correct; only coverage
  drops. Resync on input change.
- [Snapshot memory] → 2 × ~1.07 MB, acceptable.
- [Interaction with Option A scene reset] → resync the ghost on every reset.

## Migration Plan

Test switch only (`SNESRECOMP_WS_OFFWINDOW=ghost`). Rollback = use `ring` or
unset. Promotion to default is a later change and needs the D5 numbers.

## Open Questions

- Is any other runner/bridge static outside the rollback snapshot (D5.1 will
  tell)?
- Are BG1 characters static during a race?
- Can the ghost field be harvested without full rasterization at identical
  PPU/DMA state?
