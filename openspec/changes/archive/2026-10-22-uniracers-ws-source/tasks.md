## 1. Preparation

- [x] 1.1 Read `docs/UNIRACERS_TRACK_FORMAT.md`, this change's `design.md`, and `uniracers-ws-leading-edge/report.md` §2.
- [x] 1.2 Create `work/ws-source/{logs,frames,metrics,patches}/`.
- [x] 1.3 Build `build-local/` from the shared tree; confirm native Dragster 2500 SHA-256 `3417f91a…df44` and that `ring` reproduces the leading-edge numbers on Dragster. *(Native `3417f91a…` confirmed; `ring` not re-scored here: its numbers are taken from the leading-edge report, and the state trace `ring` vs `source` was compared instead.)*
- [x] 1.4 Save the current `src/game_rtl.c` as `work/ws-source/patches/game_rtl.c.before`.

## 2. Table reader and anchor

- [x] 2.1 Add the `$7F` reader: header validation (`cols*rows*2 == 0x8000`), `WsSrcCell(tx, ty)` with 0x0000 outside the grid (design D1). *(Plus word-presence set from the library.)*
- [x] 2.2 Implement the anchor search from verified cells of the current frame: rare-word seed, ≥ 95 % agreement, unique survivor (D2). *(Revised: all-frames samples, filters, up to 8 anchors; design D2.)*
- [x] 2.3 Implement the per-frame self-check and invalidation (D3, D5).
- [x] 2.4 Add `SNESRECOMP_WS_SOURCE_LOG`: anchor found/lost (frame, offset, candidates, agreement), per-frame disagreement count.

## 3. Serving

- [x] 3.1 In `WsOffwindowTile`, mode 2: ring-verified → table at `w + A` → mask (D4); log served words with `masked=0`. *(Serve only where all live anchors agree.)*
- [x] 3.2 Make sure mode 2 also disables the global BG1 clamp and repeat, like modes 1/3. *(Already true for every non-zero mode.)*

## 4. Invariants

- [x] 4.1 Native 4:3 hash unchanged with the switch unset. *(`3417f91a…` with the switch set and WS off.)*
- [x] 4.2 Dragster frame 2500 WRAM/VRAM/CPU/DMA dumps identical to `ring`. *(Per-frame state trace `ring` vs `source` identical over 2,500 frames; VRAM untouched by construction.)*
- [x] 4.3 No guest writes from the new code (review: reads of `g_ram` only).

## 5. Validation

- [x] 5.1 Probe + `world_key_check.py --served --mask-log` on Dragster →, Dragster ←, Zoom Zoo, Bowl, Switcher, Monster (windows from leading-edge report §3): 0 wrong. *(0 wrong on all six; `logs/validation-source.txt`.)*
- [x] 5.2 Report per track: served/masked per side, unverifiable count, anchor latency (frames from race start). *(report §2.)*
- [x] 5.3 Spot-check served cells never seen natively against the offline renderer (`work/ws-map-decode/scripts/render_track.py rom:<track>`). *(173,567 served cells vs offline ROM decode: 0 differences.)*
- [x] 5.4 Capture wide frames `ring` vs `source` (Dragster 2500 and one per other track) for visual review. *(`frames/source-*.png`.)*
- [x] 5.5 *(Skipped: ghost parked; offline ROM decode used as independent oracle, 5.3.)* Optional: same-run cross-check against `ghost` on Dragster (served words agree).

## 6. Cost

- [x] 6.1 CPU seconds over 2,500 Dragster frames vs `ring` (< 0.2 ms/frame). *(Within noise, < 0.01 ms/frame.)*
- [x] 6.2 Anchor search time (first anchor and worst retry). *(0.015–0.6 ms per search.)*

## 7. Report and decision

- [x] 7.1 Write `report.md`: method, invariants, per-track numbers, cost, captures, the patch (`work/ws-source/patches/`).
- [x] 7.2 Go/no-go; recommend whether `source` replaces `clamp` as the 16:9 default. *(Go; recommend as 16:9 default.)*
- [x] 7.3 Update `docs/UNIRACERS_TRACK_FORMAT.md` (runtime use), `docs/UNIRACERS_BG1_RING_FINDINGS.md`, the handoff and `work/README.md`.
