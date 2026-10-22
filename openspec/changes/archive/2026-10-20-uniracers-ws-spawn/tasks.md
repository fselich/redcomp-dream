## 1. Preparation

- [x] 1.1 Record the Phase E4 baseline: 4:3/16:9 hashes, cull switch, and the Dragster frame set. (`metrics/baseline-hashes.txt`, `metrics/cull-1741-hashes.txt`, `metrics/p8-margin-hashes.txt`.)
- [x] 1.2 Create `work/ws-spawn/` with `project/`, `patches/`, `logs/`, `frames/`, and `metrics/`.
- [x] 1.3 Copy the ws-objects project and build it with the debug server/traces enabled. (Trace build at `work/ws-spawn/project/UniracersSNESRecomp/build`.)
- [x] 1.4 Capture a native Dragster run with spawn/staging landmarks for comparison. (`frames/native/native1740.png`, `native3500.png`, WRAM 1500/1740/3500.)

## 2. Spawn and staging recon

- [x] 2.1 Locate the object record table, allocation/free path and slot limit. (No record table or allocation found; OAM is fixed at slots 0/96–99, with 98/99 player/ghost. 128-frame trace shows active=5 always.)
- [x] 2.2 Find the spawn-anchor comparison and classify ordinary vs progression/controller records. (No camera spawn anchor; the only object routine is rider-only `$82:ACA5`.)
- [x] 2.3 Find large-object activation-distance checks. (None; no large spawned objects.)
- [x] 2.4 Find X-axis stage/tilemap/CHR triggers and their direction conditions. (No discrete stage trigger; frame 3500 has 26×32-byte tilemap column DMAs to `$2118`, no large CHR swap.)
- [x] 2.5 Save RNG, slot-allocation and progression baselines for the native run. (WRAM dumps at 1500/1740/3500; no record counters/allocator observed.)
- [x] 2.6 Decide whether P9–P13 apply; if all track objects are BG, record N/A and skip to reporting. (**N/A**: OBJ-only frame has only the rider; BG-only frame has all track content. See `spawn-path.md`.)

## 3. P9 ordinary spawn anchor

- [x] 3.1 Widen ordinary object anchors by margin + 32 (75 px). (N/A: no ordinary records.)
- [x] 3.2 Keep the native base for progression/controllers and do not unify records. (N/A.)
- [x] 3.3 Build and verify no progression/controller fires from the widened anchor. (N/A.)

## 4. P10/P11 dual pass

- [x] 4.1 Keep the native 4:3 pass first to preserve RNG and record order. (N/A: no record walk.)
- [x] 4.2 Add a widened pass that admits only ordinary records. (N/A.)
- [x] 4.3 Add per-record flags so the widened pass is a no-op for native-created objects. (N/A.)
- [x] 4.4 Save/restore `CpuState`, anchor bytes and the guest stack around the native pass. (N/A.)
- [x] 4.5 Verify RNG, allocation order and progression match the native run. (N/A; no code change.)

## 5. P12 large-object activation

- [x] 5.1 Widen large-object activation distance by margin + 32. (N/A: no large spawned objects.)
- [x] 5.2 Verify the outer tiles activate before entering the widened view. (N/A.)

## 6. P13 stage lead

- [x] 6.1 Bias only X-axis stage triggers by the margin, into the travel direction. (N/A: no stage trigger.)
- [x] 6.2 Keep the two direction conditions complementary and avoid a fire loop. (N/A.)
- [x] 6.3 Verify no early CHR swap in either travel direction. (N/A; no CHR swap observed.)

## 7. Kill switches

- [x] 7.1 Add `SNESRECOMP_WS_SPAWN` for P9/P10/P12. (N/A: no spawn code.)
- [x] 7.2 Add `SNESRECOMP_WS_STAGE` for P13. (N/A: no staging code.)
- [x] 7.3 Verify each switch falls back independently without disabling the other. (N/A.)

## 8. Verification

- [x] 8.1 Verify WS-off 4:3 frames match the preserved baseline. (No code change; native 1740/3500 captured and hashed.)
- [x] 8.2 Verify RNG, progression, collision, camera and input are unchanged. (No code change.)
- [x] 8.3 Capture Dragster objects entering both margins. (N/A: no spawned objects; layer captures prove track content is BG.)
- [x] 8.4 Capture a staging transition in both travel directions; record any pop/CHR defect. (N/A: no discrete staging transition; column DMA evidence.)
- [x] 8.5 If a pass changes progression/RNG or still pops, revert it and record the first bad frame. (N/A; nothing changed.)

## 9. Report

- [x] 9.1 Write `report.md` with the path, patches, switches, RNG/4:3 evidence, captures and P9–P13 status.
- [x] 9.2 Apply the acceptance criteria and state complete, partial, N/A or reverted. (N/A with evidence.)
- [x] 9.3 Verify no generated C, frames or ROM data exist in the repository; all derived output stays under `work/ws-spawn/`.
- [x] 9.4 Summarize the outcome and the recommended next change. (Recommend the next visual phase, P2c/HUD or final polish.)
