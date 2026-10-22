## 1. Preparation

- [x] 1.1 Create `work/ws-leading-edge/{logs,frames,metrics,patches,scripts}/`.
- [x] 1.2 Build in `build-local/` with the probe patch; confirm native Dragster 2500 SHA-256 `3417f91a…df44`.
- [x] 1.3 Check whether `PpuRenderVram` reads live VRAM or a snapshot during `GameDrawPpuFrame` (decides D2 bracket vs renderer hook). *(Live, synchronous; but the bracket still leaked into the HUD because its bands repoint BG1/BG3 map usage. Final mechanism is the renderer hook.)*
- [x] 1.4 Extend `world_key_check.py` to score served/masked/wrong per side and track, using the probe's `served` value. *(`--served`, `--mask-log`; visit log records every margin cell.)*
- [x] 1.5 Add (or locate) routes for Bowl, Switcher and Monster with a horizontal section; record frames. *(Dump at NOW PLAYING confirms each track; windows 2000–2300. Leftward route at 3300–3600.)*

## 2. Option A — verified ring cells

- [x] 2.1 Add `SNESRECOMP_WS_OFFWINDOW` (`ring`/`source`) in `game_rtl.c`; disable global BG1 clamp and repeat when set.
- [x] 2.2 Implement the 32×32 seen table (world key + tile word) updated from the native view each frame. *(Per line, from the line's journal+HDMA scroll.)*
- [x] 2.3 Pick and verify a transparent BG1 entry per track (character data empty). *(Runtime scan of the 16×16 character.)*
- [x] 2.4 Implement the draw bracket: mask unverified margin cells, rasterize, restore. *(Implemented; rejected because it overwrote HUD-band map entries. Kept out of the final path.)*
- [x] 2.5 Add a per-frame VRAM hash check around the bracket (debug build). *(Implemented; found the guest VRAM write and the bracket collision behaviour, which motivated the renderer hook.)*
- [x] 2.6 Fall back to a renderer-side hook in `PpuDrawBackgroundBig` if 1.3 shows a snapshot/race. *(Final mechanism: no guest VRAM writes, HUD untouched.)*

## 3. Option A — verification

- [x] 3.1 Native 4:3 bit-identical with switch unset and set. (`3417f91a…df44` at Dragster 2500.)
- [x] 3.2 Camera/scroll/RNG/input traces identical to the clamp run. *(WRAM/VRAM/CPU/DMA dumps identical at frame 2500, live 2600.)*
- [x] 3.3 0 wrong served cells on Dragster and Zoom Zoo; record coverage per side.
- [x] 3.4 Same on Bowl, Switcher, Monster and a leftward-motion section. *(0 wrong; leftward route 244/300 negative deltas.)*
- [x] 3.5 Measure served/masked toggle rate (seam flicker) and frame cost. *(0 state changes over 84,871 visible pairs; +0.0686 ms/frame.)*
- [x] 3.6 Capture wide frames (clamp vs `ring`) for visual review. *(`wide-clamp-vs-ring-dragster-2500.png`.)*

## 4. Option C — investigate the leading source

- [x] 4.1 Trace who writes ring cells `start+17..start+19` while moving right (row chunk vs column path). *(Only the 16/17-word stage chunk; it starts at the last native cell and crosses into the next map row. No clean pre-write.)*
- [x] 4.2 Check whether the +0 cell's 70 % comes from the game pre-writing the next column. *(No: +0 is the last native cell; +1/+2 are stale.)*
- [x] 4.3 Map `$81:B3D0` inputs (`$76`, `$023E`, `$0236`, `$0339`) and side effects. *(`$76` count, `$023E` source, `$0236` stride, `$0339` staging; multi-segment assembler.)*
- [x] 4.4 Test hook: run the builder widened by 3 cells per side into a scratch buffer; score extra cells against ground truth. *(Deferred: the segment pointer list was not decoded, Option C was a no-go, and `ws-source` later solved the leading fill.)*
- [x] 4.5 If 4.4 fails, prototype a synthetic resolver call with full save/restore; score and time it. *(Deferred: the caller segment selection was unknown, Option C was a no-go, and `ws-source` later solved the leading fill.)*
- [x] 4.6 Record a go/no-go for Option C with evidence. **No-go** for this change; see `report.md` §6.

## 5. Option C — prototype (only if 4.6 is go)

- [x] 5.1 Store sourced cells in a host store (`ws_shadow` `tileShift = 4` or a row buffer), never in guest memory. *(N/A: 4.6 no-go; no Option C prototype.)*
- [x] 5.2 In `source` mode, sourced cells override the Option A mask. *(N/A: 4.6 no-go; `ws-source` implemented this separately.)*
- [x] 5.3 CPU/WRAM/VRAM hashes unchanged around the hook. *(N/A: 4.6 no-go; no Option C prototype.)*
- [x] 5.4 0 wrong served cells on the five tracks; record leading-side coverage. *(N/A: 4.6 no-go; `ws-source` validated the five tracks.)*
- [x] 5.5 Frame cost ≤ 0.5 ms/frame. *(N/A: 4.6 no-go; `ws-source` measured no measurable cost.)*

## 6. Report and decision

- [x] 6.1 Write `report.md`: method, coverage/wrong/masked per track and side, cost, captures.
- [x] 6.2 State go/no-go for Option A and for Option C separately. *(A go, C no-go.)*
- [x] 6.3 Update `docs/UNIRACERS_BG1_RING_FINDINGS.md` and the handoff with the result.
- [x] 6.4 Recommend the default (clamp, `ring` or `source`) and the next change. *(Recommend `ring` in a follow-up; clamp stays default here. Next: decode the segment list for C.)*

## Closure

Closed on 2026-10-22. Option A shipped; Option C was a no-go and was later
superseded by `uniracers-ws-source`. The Option C prototype tasks (5.x) were
not applicable after the no-go; 4.4/4.5 were deferred and are not outstanding.
