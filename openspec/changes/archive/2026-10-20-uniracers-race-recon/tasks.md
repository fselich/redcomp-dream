## 1. Preparation

- [x] 1.1 Re-read the Phase B report and record the executable path, pinned commit, ROM digest, and menu evidence.
- [x] 1.2 Sanity-check the existing executable with a 600-frame headless run; confirm exit 0 and a non-black frame.
- [x] 1.3 Create `work/race-recon/` with `scripts/`, `frames/`, `coverage/`, `metrics/`, and `logs/`.
- [x] 1.4 Ask the user for the manual 1P route (buttons and any character/track selection) or derive it from short trial runs.

## 2. Route discovery

- [x] 2.1 Write a first input script using `press`/`wait` for menu → 1P and capture frames at checkpoints.
- [x] 2.2 Iterate until a race frame is reached; record the frame where the menu changes and the final button sequence.
- [x] 2.3 Save the final script and a short route note under `work/race-recon/scripts/`.

## 3. Determinism

- [x] 3.1 Run the script twice to the race window and compare frame hashes at fixed markers.
- [x] 3.2 Record the first divergence frame, if any, and classify the route as deterministic or non-deterministic.

## 4. Race capture

- [x] 4.1 Run the bounded race route with selected frame captures, tier2 coverage, and PPU/VRAM/WRAM dumps; record budget, elapsed time, and exit status.
- [x] 4.2 Verify each artifact exists; mark any missing artifact as unavailable.

## 5. Coverage analysis

- [x] 5.1 Ingest the race capture with `tools/tier2_ingest.py` against `src/gen/program_manifest.json`.
- [x] 5.2 Extract interpreted-instruction counts, discoveries, and ranked hot PCs for the race window.
- [x] 5.3 Diff the race entries against the Phase B menu capture and list race-only hot paths.

## 6. Renderer mapping

- [x] 6.1 Inspect the top hot PCs and record writes to PPU mode/layer/scroll, Mode 7, and HDMA registers.
- [x] 6.2 Establish whether Mode 7 and HDMA are used during the race, with evidence, or mark the question unresolved.
- [x] 6.3 Build a table separating confirmed renderer sites from unresolved candidates.

## 7. Camera, viewport, and culling

- [x] 7.1 Rank candidate camera, viewport, and culling sites from hot PCs and state variables.
- [x] 7.2 Record at least one candidate with evidence and the expected effect of changing it, or explain why none could be identified.

## 8. Report

- [x] 8.1 Write `report.md` with the route, capture evidence, coverage results, renderer map, candidates, feasibility assessment, limitations, and a "what this does not prove" section.
- [x] 8.2 State whether true 16:9 requires AOT promotion for the hot functions and name the next change.
- [x] 8.3 Verify no generated C, frames, coverage journals, or ROM data exist in the repository; all derived output remains under `work/race-recon/`.
- [x] 8.4 Summarize the outcome and the recommended next change.
