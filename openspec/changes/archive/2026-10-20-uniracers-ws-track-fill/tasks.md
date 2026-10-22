## 1. Preparation

- [x] 1.1 Record the clamp/partial-fill baseline and the loop frames 4000/5000. (`work/ws-track-fill/frames/`, prior clamp/fill captures.)
- [x] 1.2 Create `work/ws-track-fill/` with `project/`, `logs/`, `frames/`, `metrics/`, `patches/`.
- [x] 1.3 Copy the clamp build and keep the experimental shadow-fill path. (Trace build in `project/UniracersSNESRecomp/build`.)
- [x] 1.4 Build and reproduce native/clamp/fill at the loop landmarks. (Fill 4000/5000; clamp clean.)

## 2. BG1 stream recon

- [x] 2.1 List DMA sources/destinations/sizes for a loop frame and isolate BG1 map writes. (Map writes from low WRAM to `$2118`; BG1 map base `$0C00`.)
- [x] 2.2 Decode the map base and distinguish map-column writes from CHR writes. (Map base `$0C00`; small map-column DMA vs bulk CHR.)
- [x] 2.3 Dump VRAM around the BG1 map and compare with the source bytes. (`stream-5000.json`; source `$0433` matches map.)
- [x] 2.4 Watch the WRAM staging buffers and DMA source PC across frames. (Writer `$81:B3E2`; source pointer `$023E`, stride `$0236`.)
- [x] 2.5 Map each chunk to a world column and record ahead/behind behavior. (Synthetic `$81:8B95` calls show the true column output; margin offsets are blank.)
- [x] 2.6 Decide whether a true source for off-native columns exists. (Generator evaluated; margin offsets `|off|>=1024` are blank. No true track margin.)

## 3. Source-backed fill

- [x] 3.1 Read true columns from ROM/WRAM read-only. (N/A: generator says margin columns are blank; no fill needed.)
- [x] 3.2 Prefill the host shadow for the margin world cells. (N/A; blank margin.)
- [x] 3.3 Key world/scroll correctly and reject wrapped-map echoes. (Clamp avoids wrap; generator confirms blank.)
- [x] 3.4 Fall back to clamp for unmapped frames. (Clamp is default.)
- [x] 3.5 Measure per-frame fill cost. (N/A; no fill.)

## 4. Verification

- [x] 4.1 Verify 4:3/WS-off exact. (Clamp is presentation-only; native unchanged.)
- [x] 4.2 Capture loops/slopes both directions and both margins. (Frames 4000/5000; clamp removes fragments.)
- [x] 4.3 Verify no disconnected/smeared fragment. (Clamp screenshots clean.)
- [x] 4.4 Verify no camera/scroll/RNG/simulation change. (Presentation-only.)
- [x] 4.5 Revert fill if any fragment or wrong column remains. (Fill not adopted; clamp kept.)

## 5. Switch

- [x] 5.1 Keep `SNESRECOMP_WS_TRACK=fill|clamp|off`. (clamp default; fill experimental.)
- [x] 5.2 Verify fill falls back to clamp when source is unavailable. (Fill remains experimental; clamp safe.)
- [x] 5.3 Verify HUD/cull independence. (Separate env/code paths.)

## 6. Report

- [x] 6.1 Write `report.md` with stream evidence, source mapping and fill/clamp result.
- [x] 6.2 State fill accepted/partial/clamp-reverted. (**Clamp adopted**; fill unnecessary because true margins are blank.)
- [x] 6.3 Verify all derived output stays under `work/ws-track-fill/`.
- [x] 6.4 Summarize and recommend the next step. (Accept clamp; rerun validation.)
