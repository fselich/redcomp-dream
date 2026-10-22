## 1. Preparation

- [x] 1.1 Re-read the Phase A report and record the pinned ROM digest, framework commit, and the 2 LLE-only / 9 indirect targets.
- [x] 1.2 Verify the toolchain and SDL3 (`python3`, `rustc`, `cargo`, `cmake`, `ninja`, `gcc`, `pkg-config --modversion sdl3`); save the output.
- [x] 1.3 Create `work/boot-spike/` with `project/`, `logs/`, `shots/`, `coverage/`, and `metrics/`.
- [x] 1.4 Confirm the pinned framework commit `9c6afcbfd4106e4613e522741d667047a48ded3b` is reachable from the public remote.

## 2. Scaffold

- [x] 2.1 Run `setup_project.sh --rom … --dir … --name Uniracers --no-recomp-ui --no-github --no-ci --yes` without generate or build; save the scaffold log.
- [x] 2.2 Check out the pinned commit inside `project/snesrecomp`, stage it, and verify `git rev-parse HEAD` matches.
- [x] 2.3 Verify `project/rom_identity.txt` matches the pinned SHA-256 and CRC32.

## 3. Generate and build

- [x] 3.1 Run `tools/regen.sh` with the pinned ROM; capture the log and generation duration.
- [x] 3.2 Configure and build the project with CMake, Ninja, and Release; record duration, executable path, and size.
- [x] 3.3 If generation or build fails, classify the failure (toolchain, dependency, or generated-code error) and capture the first error verbatim. (N/A — generation and build succeeded.)

## 4. Headless run

- [x] 4.1 Run with `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, `--no-launcher`, `SNESRECOMP_RUN_FRAMES=600`, and a 5-minute wall-clock timeout; save stdout/stderr and exit status.
- [x] 4.2 Record the frame budget reached, presenter/renderer used, and whether the launcher was bypassed.
- [x] 4.3 If the run hangs or crashes, record the last log line and the guest-frame count. (N/A — exit 0 at 600 frames.)

## 5. Frame evidence

- [x] 5.1 Capture a small deterministic frame set (frame 1 plus frames 598–600, or the `--framedump` fallback with a narrow range).
- [x] 5.2 Compute non-black pixel fraction, unique color count, mean luminance, and frame-to-frame difference; save as metrics JSON.
- [x] 5.3 Classify at least one frame as non-trivial (≥1% non-black) or black, or mark frame evidence unavailable.

## 6. Coverage

- [x] 6.1 Rerun the boot route with `SNESRECOMP_TIER2_CAPTURE=1` and `SNESRECOMP_COVERAGE_DIR`; collect the checkpoint and journal.
- [x] 6.2 Ingest with `tools/tier2_ingest.py` against the program manifest from the same build; record interpreted entries and fallback reasons.
- [x] 6.3 Check the 2 LLE-only variants and the 9 unresolved indirect PCs from Phase A for hit/not-hit, with transfer kind and reason when hit.

## 7. Report

- [x] 7.1 Write `report.md` with the boot outcome, evidence, first obstacle, coverage results, limitations, and a "what this does not prove" section.
- [x] 7.2 Apply the outcome classification and state the next step: targeted RTL work, a harness fix, or remediation of a failed stage.
- [x] 7.3 Verify no generated C, frames, coverage journals, or ROM data exist in the repository; all derived output remains under the scratch root.
- [x] 7.4 Summarize the outcome and the recommended next change.
