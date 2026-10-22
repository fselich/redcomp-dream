## 1. Preparation

- [x] 1.1 Create `work/ws-offwindow/{project,logs,frames,metrics,patches}/`.
- [x] 1.2 Reuse the current spike build (symlinked) and keep clamp default.
- [x] 1.3 Remove `saves/save.srm`/`.bak` and verify normal pacing.
- [x] 1.4 Reproduce Dragster 2500/3500 and Zoom Zoo 3504 native/clamp.
- [x] 1.5 Rebuild in `build-local/` (old `build/` records another machine's paths) and confirm native frame 2500 SHA-256 `3417f91a…df44` = baseline.

## 2. Ring formula

- [x] 2.1 Capture same-run stage/DMA/VRAM at frame 3505.
- [x] 2.2 Record the anchor: stage = BG1 row 19, x=7..23.
- [x] 2.3 Record `hscroll`, `vscroll`, map base and BG1 map size.
- [x] 2.4 Test `map_x = (hscroll/8 + screen_x/8) mod 32`. *(Failed: 28,080 conflicts. Cause: BG1 is 16×16 in race (`BGMODE=0x19`); correct form is `((hscroll + x) >> 4) & 31`.)*
- [x] 2.5 Test `map_y = ((vscroll + y) >> 4) & 31` (16×16). *(Renderer probe.)*
- [x] 2.6 Record the exact tile phase and off-by-one/base offset. *(No offset; frame 3505 hscroll 121 → cell 7, phase 9; screen samples cells 7..23 = stage chunk.)*
- [x] 2.7 Validate the formula against native 4:3 cells. *(0 conflicts: Dragster 2,895 cells, Zoom Zoo 830.)*
- [x] 2.8 Reject the formula if native cells do not match. *(Not rejected; negative controls conflict.)*
- [x] 2.9 Add the test-only renderer pixel-source probe (`SNESRECOMP_BGPROBE_*`, 8×8 and 16×16 paths); confirm native output unchanged. *(`work/ws-offwindow/patches/bgprobe.diff`.)*
- [x] 2.10 Add `work/ws-offwindow/scripts/world_key_check.py` (world-key conflicts + margin scoring).

## 3. Margin source

- [x] 3.1 Measure what the unclamped 16:9 margins hold, per side. *(Dragster: trailing 100 %, leading 47 %; Zoom Zoo 99.9 % / 97.7 %.)*
- [x] 3.2 Record leading-edge accuracy per cell past the edge. *(Dragster +0/+1/+2: 70/36/36 %.)*
- [x] 3.3 Repeat 3.1 on Bowl, Switcher and Monster, and on leftward motion. *(Done in `uniracers-ws-leading-edge`; 0 wrong served cells on all five tracks and leftward Dragster.)*

> 3.4–3.7 are carried out in `uniracers-ws-leading-edge` (Option C, section 4).

- [x] 3.4 Trace how the leading column is written during horizontal motion (row chunk vs column path). *(Only the 16/17-word row chunk; it starts at the last native cell, not at +1/+2.)*
- [x] 3.5 Test whether the `$81:B3D0` / `$7F` segment lookup yields valid cells for `start-3..start-1` and `start+17..start+19` (score with `world_key_check.py`). *(Blocked: the segment pointer list is undecoded; Option C no-go in `uniracers-ws-leading-edge`.)*
- [x] 3.6 Record exact matches and diffs. *(See `uniracers-ws-leading-edge/report.md` §6.)*
- [x] 3.7 Fall back to clamp (leading side only) where the source is unavailable. *(Per-cell mask to background; never worse than the clamp.)*

## 4. Topology classification

- [x] 4.1 Use `maps/` to label Dragster as horizontal.
- [x] 4.2 Label Zoom Zoo loop as vertical/background.
- [x] 4.3 Label Bowl, Switcher and Monster sections.
- [x] 4.4 Record expected margin content per section.
- [x] 4.5 Do not use line-art for tile-level claims.

## 5. Test-only prototype

> Sections 5 and 6 are implemented and verified in `uniracers-ws-leading-edge`
> (Option A: verified ring cells; Option C: leading source). Tick here when that
> change reports.

- [x] 5.1 Add a dedicated `SNESRECOMP_WS_OFFWINDOW` switch.
- [x] 5.2 Keep it independent of clamp, HUD, cull and repeat.
- [x] 5.3 Build a host shadow (`ws_shadow`, `tileShift = 4`) or renderer-side substitution; never write the guest VRAM ring. *(Renderer-side substitution.)*
- [x] 5.4 Serve the trailing margin from ring cells; fill the leading margin only from source-backed cells. *(Option A; leading remains background.)*
- [x] 5.5 Clamp only the leading side on any unmapped cell (motion side from scroll delta, with hysteresis). *(Superseded by per-cell verification: no direction heuristic.)*
- [x] 5.6 Do not modify guest camera/scroll/RNG/input. *(WRAM/VRAM/CPU/DMA identical to clamp.)*

## 6. Verification

- [x] 6.1 Verify native 4:3 bit-identical.
- [x] 6.2 Verify camera/scroll/RNG input unchanged.
- [x] 6.3 Verify Dragster horizontal margins show track. *(Trailing authentic; leading masked.)*
- [x] 6.4 Verify Zoom Zoo vertical margins remain background. *(Unverified cells masked; authentic cells served.)*
- [x] 6.5 Verify Bowl/Switcher/Monster.
- [x] 6.6 Verify no wrap/duplicate/smear (0 wrong served cells in `world_key_check.py`).
- [x] 6.7 Measure frame cost and memory. *(≈0.07 ms/frame; no new large buffers.)*
- [x] 6.8 Keep all derived captures under `work/`.

## 7. Report and decision

- [x] 7.1 Write `report.md` with formula, evidence and captures. *(Interim plus addendum; full results in `uniracers-ws-leading-edge/report.md`.)*
- [x] 7.2 State go/no-go. *(Option A go; Option C no-go.)*
- [x] 7.3 Update durable docs with the result. *(Ring findings: `docs/UNIRACERS_BG1_RING_FINDINGS.md`; correction banners in the other docs.)*
- [x] 7.4 If no-go, keep clamp and record the blocker. *(Clamp stays default; Option C blocker recorded.)*
- [x] 7.5 Recommend the next change. *(Promote `ring` in a follow-up; decode the segment list for C.)*
