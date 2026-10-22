## 1. Preparation

- [x] 1.1 Record toolchain versions (`python3`, `rustc`, `cargo`, `cmake`, `ninja`, `gcc`) in `/tmp/opencode/uniracers-baseline/logs/toolchain.txt`.
- [x] 1.2 Create the scratch layout: `snesrecomp/`, `gen-a/`, `gen-b/`, and `logs/` under `/tmp/opencode/uniracers-baseline/`.
- [x] 1.3 Clone SNESRecomp and check out `9c6afcbfd4106e4613e522741d667047a48ded3b`; verify the resolved commit with `git rev-parse HEAD`.
- [x] 1.4 Build the native analyzer and verify `recompiler-rs/target/release/snesrecomp-analyze` exists; save the build log.

## 2. ROM identity

- [x] 2.1 Compute the ROM size, CRC32, and SHA-256, and compare them against the pinned values; abort on any mismatch.
- [x] 2.2 Run `tools/new_project/probe_rom.py` on the ROM and save the output; confirm `mapping=lorom`, `coprocessor=none`, `checksum_valid=True`, and `sram_size_kb=8`.

## 3. Generation

- [x] 3.1 Run `snesrecomp_cli.py build` into `gen-a` with the starter `bank00.cfg`, capturing stdout to `logs/emit-a.log` and timing the run.
- [x] 3.2 Stop the run and record a budget finding if generation exceeds 30 minutes.
- [x] 3.3 Record `roots`, `emitted_variants`, `lle_variants`, and `banks` from the emit log.
- [x] 3.4 Generate a second time into `gen-b` and compare the SHA-256 of both `generated/program_manifest.json` files; save both digests.
- [x] 3.5 Confirm no generated C or manifest exists under the project root.

## 4. Build

- [x] 4.1 Configure `gen-a` with CMake, Ninja, and `CMAKE_BUILD_TYPE=Release`; save the configure log.
- [x] 4.2 Build and confirm `libsnesrecomp_game.a` exists; record its path, size, and build duration.
- [x] 4.3 If configuration or compilation fails, classify the failure (toolchain, missing dependency, or generated-code error) and capture the first error verbatim. (N/A — build succeeded.)

## 5. Metrics

- [x] 5.1 Parse `generated/program_manifest.json` for per-bank variant counts and verify they sum to the reported totals.
- [x] 5.2 Build the LLE reason histogram and identify the three most frequent reasons with counts.
- [x] 5.3 Mark any missing manifest or emit field as unavailable; do not infer or default it to zero. (No fields were missing.)

## 6. Report

- [x] 6.1 Write `report.md` with ROM identity, toolchain, pinned commit, timings, build result, metrics tables, top LLE reasons, limitations, and a "what this does not prove" section.
- [x] 6.2 Apply the go/no-go criteria: recommend Phase B only if generation succeeded, the library built, and both manifests match; otherwise recommend remediation.
- [x] 6.3 Verify the report contains no generated C, no ROM bytes, and no game data, and that all derived output remains outside the repository.
- [x] 6.4 Summarize the outcome and the next step (Phase B or remediation).
