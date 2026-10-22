# Proposal: Uniracers boot spike (Phase B)

## Why

Phase A proved that the SNESRecomp toolchain works on Linux and produced a
reproducible static baseline for *Uniracers (USA)*: 9 roots, 54 variants
(52 AOT-eligible, 2 LLE-only) across 5 banks, with 9 unresolved indirect jumps.
Nothing was executed.

The next uncertainty is runtime, not compilation: whether the scaffolded desktop
host reaches a visible frame, and where execution first leaves generated code.
A boot spike is the cheapest experiment that answers that and produces runtime
coverage evidence. Without it, deciding whether to invest in per-game RTL would
still be guesswork.

## What Changes

- Scaffold a desktop project from the pinned ROM with `setup_project.sh`
  (`--no-recomp-ui`), pinned to the same SNESRecomp commit, under
  `work/boot-spike/`.
- Build a Linux executable using the framework's desktop host and beam frame
  driver; record build cost.
- Run headless with dummy SDL video/audio, a fixed guest-frame budget, and frame
  capture; classify the boot outcome.
- Enable tier2 coverage capture, ingest it with `tier2_ingest.py`, and check
  whether Phase A's 9 unresolved indirect edges and 2 LLE-only variants are hit.
- Produce a report with the boot verdict, the first concrete obstacle, coverage
  evidence, and a scoped recommendation for per-game RTL work.
- Commit no ROM-derived output. Add no playability, audio, input, widescreen,
  netplay, or mods claims.

## Non-goals

- A playable port, input tuning, audio fidelity, widescreen, save states,
  netplay, or mods.
- Full symbol/disassembly work. Only targeted dispatch investigation justified
  by captured evidence.
- Redistributing generated C, the ROM, or any ROM-derived data.

## Capabilities

### New Capabilities

- `recomp-boot-spike`: headless boot execution and evidence capture for a
  scaffolded SNESRecomp project, including frame evidence, coverage ingestion,
  and a classified boot verdict.

### Modified Capabilities

None.

## Impact

- Adds a runtime experiment; no product code.
- The scaffolded project, generated C, frames, and coverage journals live under
  `work/boot-spike/` (gitignored); the repository holds only the report
  and non-derived metrics.
- Dependencies: SNESRecomp at `9c6afcbfd4106e4613e522741d667047a48ded3b`,
  SDL3, CMake, Ninja, GCC.
- Runtime is bounded by `SNESRECOMP_RUN_FRAMES` and a wall-clock timeout.
- Inputs are pinned: *Uniracers (USA)*, CRC32 `383858c7`, SHA-256
  `859ec99fdc25dd9b239d9085bf656e4f49c93a32faa5bb248da83efd68ebd478`.
