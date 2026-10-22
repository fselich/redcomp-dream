# Proposal: Uniracers recompilation baseline (Phase A)

## Why

Before committing to a native Linux port of *Uniracers*, we need to know two
things that are currently guesswork: whether the SNESRecomp toolchain works
end to end on this machine, and how much of this specific ROM the analyzer can
prove statically. Today there is no generated project, no coverage numbers, and
no evidence about where the interpreter fallback (LLE) concentrates. Without
that baseline, deciding to invest in a playable port would be speculation.

This change is deliberately the cheapest experiment that produces a
decision-grade answer.

## What Changes

- Build the SNESRecomp native analyzer and CLI path from a pinned revision on
  Linux.
- Generate a recompilation project for the pinned *Uniracers (USA)* ROM into an
  out-of-tree scratch directory.
- Compile the generated C into `libsnesrecomp_game.a` and record build cost.
- Capture a baseline from the generated program manifest: analysis roots, AOT
  variants, LLE variants, per-bank distribution, and the reason histogram for
  LLE-only nodes. Verify that two generation runs produce the same manifest.
- Produce a written report with a go/no-go recommendation for Phase B
  (bootable desktop host).
- Commit no ROM-derived output and add no game host, SDL, launcher, netplay, or
  widescreen integration.

## Non-goals

- A playable native port, or any claim that the game boots correctly.
- Host integration (`src/game_rtl.c`), SDL3, launcher, input, audio.
- Symbol/disassembly work, dispatch tables, widescreen, mods, netplay.
- Redistributing generated C, the ROM, or any ROM-derived data.

## Capabilities

### New Capabilities

- `recomp-baseline`: reproducible, Linux-hosted generation and measurement of
  SNESRecomp static coverage for a single pinned ROM, with ROM-derived
  artifacts kept outside the repository and a decision-ready report.

### Modified Capabilities

None.

## Impact

- Adds a documented, repeatable measurement procedure; no product code.
- Dependencies: SNESRecomp at a pinned commit, Rust toolchain, Python 3,
  CMake/Ninja/GCC. SDL is not required for this phase.
- Writes ROM-derived output only to `/tmp/opencode/uniracers-baseline/`; the
  change directory holds only the report and non-derived metrics.
- Licensing: SNESRecomp is PolyForm Noncommercial 1.0.0. The generated C derives
  from copyrighted ROM data and must not be committed or distributed.
- Inputs are pinned: *Uniracers (USA)*, 2 MiB, LoROM/FastROM, no coprocessor,
  8 KiB SRAM, CRC32 `383858c7`, SHA-256
  `859ec99fdc25dd9b239d9085bf656e4f49c93a32faa5bb248da83efd68ebd478`.
