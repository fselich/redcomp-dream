# uniracers-ws-source — report

Date: 2026-10-21
Status: **go**. `SNESRECOMP_WS_OFFWINDOW=source` fills both 16:9 margins with
the authentic track: 0 wrong cells, no measurable cost, guest state identical.
Recommended as the 16:9 default in place of `clamp`.

## 1. What was built

`src/game_rtl.c` (shared tree), block "Track-table source". Patch:
`work/ws-source/patches/ws-source.diff`; pre-change copy:
`work/ws-source/patches/game_rtl.c.before`.

- **Reader.** It reads live WRAM `$7F`:
  - header `000D/000E` gives the grid (must be 0x8000 bytes);
  - `000B-000C` gives the library end, used to build a word-presence set;
  - `WsSrcCell(tx, ty)` returns the piece word, or `0x0000` outside the grid.
- **Anchor search** (`WsSrcSearch`), after each drawn frame while no anchor is
  live:
  - samples are every verified cell in `s_ow_seen` (all frames, not only the
    current one) whose word is non-zero and occurs in the piece library;
  - seed = rarest sampled word; candidate positions come from a per-piece mask
    of that word;
  - a candidate survives at ≥ 95 % agreement; up to 8 distinct survivors are
    kept.
- **Self-check** (`WsSrcFrame`), every frame: this frame's verified cells
  against every live anchor. Anchors below 95 % are pruned. With none left, the
  margins mask and the search restarts. Scene reset or a non-live race
  invalidates everything.
- **Serving** (`WsOffwindowTile`, mode 2): ring-verified cell → table word if
  all live anchors give the same word → mask.
- Diagnostics: `SNESRECOMP_WS_SOURCE_LOG=<path>` (anchor found / retry / wait
  / pruned / dropped / partial self-checks).

### Deviations from the design, found while testing

1. **Ring cells reading `0x0000` and words absent from the piece library are not
   evidence.** At race start the ring shows overlay words (`3030`, `7070`,
   `6E6E`, `7878`) and unwritten `0000` cells for ~100 frames.
2. **The search uses the whole seen table, not just the current frame.** The map
   is static, so older observations stay true. This cut Zoom Zoo latency from
   350 to 104 frames.
3. **Ambiguous anchors are kept (≤ 8) and served only where they agree,
   instead of waiting for a unique one.** Switcher's striped sections gave three
   anchors (83/84/85, 10). Latency went from 310 to 105 frames, and the self-check
   pruned the two false ones at frame 2055–2056.

`design.md` D2–D4 and the spec were updated to match.

## 2. Validation

Probe windows and routes from `uniracers-ws-leading-edge` report §3. Script:
`work/ws-source/scripts/validate_all.sh`; results:
`work/ws-source/logs/validation-source.txt`.

| Track (frames) | Anchor (frame, offset, latency) | Leading correct / wrong / masked | Trailing correct / wrong / masked |
|---|---|---|---|
| Dragster → (2300–2500) | 1535, (52,34), 104 | 140,000 / 0 / 0 | 139,104 / 0 / 0 |
| Dragster ← (3300–3600) | 1535, (52,34), 104 | 205,184 / 0 / 0 | 195,328 / 0 / 0 |
| Zoom Zoo (3300–3505) | 1615, (559,77), 104 | 75,929 / 0 / 0 | 113,836 / 0 / 0 |
| Bowl (2000–2300) | 1666, (345,73), 101 | 206,785 / 0 / 0 | 189,416 / 0 / 0 |
| Switcher (2000–2300) | 1853, (83,10) ×3 → 1 at 2056, 105 | 223,152 / 0 / 704 | 174,180 / 0 / 2,448 |
| Monster (2000–2300) | 1931, (274,10), 102 | 238,197 / 0 / 0 | 119,972 / 0 / 0 |

(Leading/trailing follow the camera direction; on Dragster ← the west side
leads.)

Compared with `ring` (leading-edge report): Dragster's leading side goes from
118,720 masked to 0. Every track now shows track on both sides.

- **Never-seen cells.** Served cells without native ground truth
  ("unverifiable", up to 88 k on Monster) were compared with the offline ROM
  decode (`work/ws-map-decode/scripts/rnc.py` + `track_cell.py`) at the logged
  anchor: **173,567 served cells, 0 differences** (Dragster, Zoom Zoo, Bowl,
  Switcher, Monster).
- **Anchor latency.** It is ~100 frames on every track. During that time the
  ring shows the race-start overlay with no usable track word
  (`anchor wait samples=0`), so there is nothing to anchor to. The margins are
  masked exactly as in `ring`, and the native view shows no track either.
- **Self-check.** No partial or failed self-check on any track after
  anchoring, apart from the two pruned Switcher candidates.
- **Visual check.** `work/ws-source/frames/source-{dragster-2500,monster-2300,switcher-2300}.png`:
  the track runs to both edges of the 342-px frame.

## 3. Invariants

| Check | Result |
|---|---|
| Native 4:3, switch set, WS off | `3417f91a…df44` |
| Per-frame state trace (`SNESRECOMP_STATE_TRACE`, WRAM CRC + CPU regs), `ring` vs `source`, 2,500 Dragster frames | identical |
| Guest writes | none (code reads `g_ram` only) |

## 4. Cost

Process CPU seconds, 2,500 Dragster frames, two runs each:
`ring` 15.621 / 15.670 s; `source` 15.661 / 15.605 s. The difference is within
noise (< 0.01 ms/frame). Anchor search: 0.015–0.6 ms, once per race (plus
cheap retries while waiting). Compare `ghost`: +2.3 ms/frame mean, p99 9.6 ms.

## 5. Decision

- **Go.** Recommend `source` as the default 16:9 BG1 policy in place of
  `clamp`. That needs a small follow-up: default the switch to `source` when
  widescreen is on, and keep `clamp`/`ring` selectable.
- `ghost` stays available but parked (cost).
- Next work it unblocks: ultrawide (BG1 is solved at any width; BG2, object
  cull and HUD remain), and track tooling (`docs/UNIRACERS_TRACK_FORMAT.md`).

Not done: the optional `ghost` cross-check (5.5). Ghost is parked, and the
offline ROM decode served as an independent oracle instead.

## 6. Artifacts (`work/ws-source/`)

| Path | Content |
|---|---|
| `patches/ws-source.diff`, `patches/game_rtl.c.before` | the change |
| `scripts/validate_all.sh [mode]` | six-capture validation |
| `logs/validation-source.txt` | scores |
| `logs/src-source-<track>.log` | anchor logs |
| `logs/ow-source-<track>-mask.csv` | margin visit logs (served words) |
| `logs/run-source-<track>.log`, `logs/inv-*.log` | run logs (CPU) |
| `metrics/state-{ring,source}-{1,2}.csv` | state traces |
| `frames/source-*.png/.ppm` | wide captures |
| `logs/iter1/` | first iteration (current-frame samples only), superseded |
