## 1. Preparation

- [x] 1.1 Record the ws-discriminator baseline: 4:3/16:9 hashes, the live-race gate, and the aligned offset.
- [x] 1.2 Create `work/ws-hdma/` with `project/`, `patches/`, `logs/`, `frames/`, `metrics/`, and `coverage/`.
- [x] 1.3 Copy the ws-discriminator project and build a trace build with the debug server enabled.
- [x] 1.4 Confirm the HDMA table pointers and channel registers at a live-race frame.

## 2. Builder trace

- [x] 2.1 Set WRAM watches on `$7E:8262`, `$7E:8280`, a window around them, and `$7E:2046`.
- [x] 2.2 Run a live-race window and query `wram_watch_log_get`; record writer PCs, widths, values and frames.
- [x] 2.3 If no writer appears, retry with a pointer/range watch or the interpreter WRAM trace.
- [x] 2.4 Identify the builder function and save the trace logs.

## 3. Table layout

- [x] 3.1 Parse the table layout: base, end, count blocks and line count. (Base is `$7E:xx70`, pointer slides; data layout unresolved.)
- [x] 3.2 Map each per-line value to `$210D`/`$210F` and the camera/scroll inputs. (Pointer/base mapped; per-line data not CPU-written.)
- [x] 3.3 Record whether the table is rebuilt per frame or copied from ROM, and the rebuild cadence. (CPU clears the buffer; data appears DMA/precomputed.)

## 4. Curve section

- [x] 4.1 Find a frame in the deterministic route where the track curves or changes height. (Frame 3500, second track.)
- [x] 4.2 Capture 4:3 and 16:9 at that frame and record the native edge columns.

## 5. Margin extension

- [x] 5.1 Implement the per-line extension for +43 columns per side (host-side table extension or generated-C hook). (Not applicable: the enabled HDMA tables are empty; the curves are BG tilemap/scroll.)
- [x] 5.2 If the curve source cannot be extended safely, record P-U1 as blocked with the builder/layout evidence and keep the renderer-only margins. (P-U1 recorded as not applicable with the empty-table/pointer evidence.)
- [x] 5.3 Build and record the added per-frame work. (No extension added; performance unchanged.)

## 6. Verification

- [x] 6.1 Verify the flag-off 4:3 baseline still aligns with the preserved build.
- [x] 6.2 Verify the 16:9 curve seam is continuous with the native edge; record any step, wrap or divergence. (No wide-only seam defect at 3500; the purple wedge is present in 4:3 too.)
- [x] 6.3 Verify the HUD stays clamped and simulation/camera/RNG/input are unchanged.
- [x] 6.4 If the extension regresses, revert it and record the first bad frame. (N/A — no extension.)

## 7. Report

- [x] 7.1 Write `report.md` with the builder, layout, extension status, seam evidence, performance, limitations and remaining gaps.
- [x] 7.2 Apply the acceptance criteria and state whether the extension is retained, limited or reverted. (Not applicable; renderer-only margins retained.)
- [x] 7.3 Verify no generated C, frames, dumps, or ROM data exist in the repository; all derived output remains under `work/ws-hdma/`.
- [x] 7.4 Summarize the outcome and the recommended next change.
