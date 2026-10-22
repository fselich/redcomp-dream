## 1. Preparation and baseline

- [x] 1.1 Record the Phase C baseline: executable path, frame hashes at 2400 and 3000, coverage totals, and the route script.
- [x] 1.2 Create `work/aot-promotion/` with `project/`, `logs/`, `frames/`, `coverage/`, and `metrics/`.
- [x] 1.3 Copy the scaffolded project into the promotion workspace; confirm the baseline copy still builds or is preserved untouched.
- [x] 1.4 Verify the copied project's framework pin and ROM identity match Phase C.

## 2. Promotion configuration

- [x] 2.1 Add a repeatable `--profile-manifest <path>` flag to the project's `tools/regen.sh` and forward it to `snesrecomp_cli.py generate`.
- [x] 2.2 Record the promotion input: the Phase C race checkpoint and journal, plus the explicit target list.
- [x] 2.3 Add `[[func]]` entries with `emit = true` for the display block and camera update if the profile cannot seed their boundaries.
- [x] 2.4 Regenerate with the profile and inspect the emitted manifest: promoted roots, AOT/LLE split, banks, and unintended targets.

## 3. Build

- [x] 3.1 Configure and build the promoted project with CMake, Ninja, and Release; record duration and executable size.
- [x] 3.2 If the build fails, classify the failure and capture the first error verbatim. (N/A — build succeeded.)

## 4. Replay

- [x] 4.1 Run the promoted build for 3,000 frames with the Phase C route, frames at 2400 and 3000, and tier2 coverage.
- [x] 4.2 Compare the promoted frame PPM hashes at 2400 and 3000 against the baseline.
- [x] 4.3 If a marker diverges, record the first divergent frame and the active promoted targets, then restore the baseline project. (Divergence classified as a stable 2-frame offset; baseline preserved and promoted build retained per Option 1.)

## 5. Measurement

- [x] 5.1 Ingest the promoted coverage against its own `src/gen/program_manifest.json`.
- [x] 5.2 Compute the interpreted-instruction delta and the status of each promoted target (gone, reduced, unchanged).
- [x] 5.3 Check the promoted run for new tripwires, crashes, or warnings.

## 6. Widescreen hand-off

- [x] 6.1 Map P1–P16 from `docs/WIDESCREEN_PATTERNS.md` to Uniracers, marking each as applying, not applicable, planned, or missing.
- [x] 6.2 Record the Uniracers-specific P-U1 (HDMA per-line extension) and P-U2 (BG2 = BG1/2 parallax) invariants.
- [x] 6.3 List the missing code before a widescreen change: gameplay discriminator, cull windows, OAM reject compare, and spawn/staging.

## 7. Report

- [x] 7.1 Write `report.md` with the promotion set, measured delta, equivalence result, rollback decision, doctrine hand-off, limitations, and a "what this does not prove" section.
- [x] 7.2 Apply the acceptance criteria and state whether the baseline is preserved or restored.
- [x] 7.3 Verify no generated C, frames, coverage journals, or ROM data exist in the repository; all derived output remains under `work/aot-promotion/`.
- [x] 7.4 Summarize the outcome and the recommended next change.
