## 1. Preparation and P16 baseline

- [x] 1.1 Record the dispatch-fix 4:3 baseline: `k=3`, frame hashes, camera AOT status, and project paths.
- [x] 1.2 Create `work/ws-spike/` with `project/`, `patches/`, `logs/`, `frames/`, `metrics/`, and `coverage/`.
- [x] 1.3 Copy the dispatch-fix project into the spike workspace and verify the pin and ROM identity.
- [x] 1.4 Re-run the flag-off 4:3 baseline on the copy and confirm alignment at `k=3`.

## 2. Live-race discriminator (P5/P6)

- [x] 2.1 List candidate WRAM bytes/words for live race: race state, timer, player/vehicle state.
- [x] 2.2 Watch the candidates across menu, race and pause using the debug server/watchpoints or interpreter traces.
- [x] 2.3 Record the proven discriminator, its values and frame ranges; if none is proven, document the fallback gate and risk. (No discriminator proven; wide mode is a developer flag and the risk is documented.)

## 3. Widescreen enablement

- [x] 3.1 Add a runtime wide-mode flag (config/env) and set `native_widescreen`, `widescreen_supported` and `frame_width=342` only when it is on.
- [x] 3.2 Verify the default flag-off build is still 256×224 and unchanged.
- [x] 3.3 Verify flag-on produces a 342×224 frame and reports the widened margin.

## 4. Renderer policies and evidence

- [x] 4.1 Apply BG1/BG2 margin policies (start natural/clamp) and a BG3 HUD clamp.
- [x] 4.2 Capture 4:3 and 16:9 at the same aligned race state.
- [x] 4.3 Classify the wide frame: genuinely wider vs stretched vs blank margins; record HUD placement and readability.
- [x] 4.4 Verify 4:3 alignment and unchanged simulation state (camera, collision, RNG, input).
- [x] 4.5 If 4:3 diverges or simulation changes, revert the wide change and record the divergence. (N/A — 4:3 is unchanged and simulation is untouched.)

## 5. HDMA builder and P-U1

- [x] 5.1 Trace writes to the HDMA source tables `$7E:8262`/`$7E:8280` and the pointer `$7E:2046`.
- [x] 5.2 Identify the builder and document the table layout.
- [x] 5.3 Implement the per-line margin extension if safe, or record P-U1 as blocked with evidence and use renderer-only margins. (Builder not found; P-U1 recorded as blocked.)

## 6. Deferred object gaps

- [x] 6.1 Review the wide capture for objects disappearing at the native edge, spawn pop-in or cull artifacts.
- [x] 6.2 Record the P7–P13 symptoms and name the follow-up change; do not implement cull/OAM/spawn here.

## 7. Report

- [x] 7.1 Write `report.md` with P16 evidence, the discriminator, wide-frame evidence, HUD status, P-U1 status, object gaps, limitations, and a "what this does not prove" section.
- [x] 7.2 Apply the acceptance criteria and state whether the wide flag is retained, limited or reverted.
- [x] 7.3 Verify no generated C, frames, coverage journals, or ROM data exist in the repository; all derived output remains under `work/ws-spike/`.
- [x] 7.4 Summarize the outcome and the recommended next change.
