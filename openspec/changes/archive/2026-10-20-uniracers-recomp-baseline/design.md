# Design: Uniracers recompilation baseline (Phase A)

## Context

*Uniracers (USA)* is a favorable cartridge for SNESRecomp: standard LoROM with
FastROM, 2 MiB, 8 KiB battery-backed SRAM, no coprocessor, valid checksum,
CRC32 `383858c7`, SHA-256 `859ec99f…d478`. The framework supports this mapping,
but there is no existing Uniracers game project and therefore no analysis
configuration beyond the hardware vectors.

SNESRecomp is LLE-first: only nodes the analyzer can prove become ahead-of-time
(AOT) C, and everything else stays reachable through the interpreter. A fresh
project therefore produces a small AOT frontier even for well-supported games
(the framework's own figures are 29–85 variants from conservative roots). The
baseline must measure that frontier and, more importantly, explain why the rest
is LLE-only.

The available Linux toolchain is Python 3.15, Rust 1.97, CMake 3.28, Ninja, GCC
13.3, and SDL3. The published CLI release is Windows x86_64 only.

## Goals / Non-Goals

**Goals:**

- Prove the SNESRecomp generation and compilation path works for this ROM on
  Linux, from a pinned revision.
- Produce reproducible static-coverage numbers and an LLE-reason histogram.
- Leave a decision-ready report that states whether Phase B is justified and
  what its first obstacle would be.
- Keep every ROM-derived artifact outside the repository.

**Non-Goals:**

- Booting, rendering, audio, input, or playability claims.
- Desktop host, SDL3, launcher, `recomp-ui`, netplay, widescreen, or mods.
- Symbol/disassembly work, dispatch-table discovery, or per-game RTL.
- Distributing generated C or any ROM-derived data.

## Decisions

### D1 — Build from a source checkout, not the released CLI

Use `python3 snesrecomp_cli.py` from a checkout instead of the published
Windows binary. The release asset is Windows x86_64 only; the source entry
point is the same generation path and runs on Linux.

*Alternatives:* run the Windows CLI under Wine (adds an indirection and an
unverifiable failure surface); build a PyInstaller bundle on Linux (unnecessary
for a local experiment). Both rejected.

### D2 — Pin the framework commit

Pin `9c6afcbfd4106e4613e522741d667047a48ded3b` (main as of 2026-10-20) and
record it in the report. Reproducibility matters more than tracking main during
a measurement run.

*Alternative:* the `v0.3.0-alpha` tag. It is older and its packaged asset is
Windows-only; the source path is what we exercise either way. A tag pin is
acceptable only if the report records it explicitly.

### D3 — Use the CLI `build` path, not the project scaffold

The CLI produces generated C plus a CMake project that builds
`libsnesrecomp_game.a`. The scaffold (`setup_project.sh`) produces a desktop
executable and pulls in the host, SDL, and optionally `recomp-ui`.

Phase A is a measurement, so mixing host failures into the result would make
the outcome ambiguous. The scaffold is Phase B's starting point.

*Alternative:* scaffold now and attempt a boot. Rejected: a black window would
not distinguish an analyzer gap from a host gap, which is exactly the
distinction this phase exists to establish.

### D4 — One conservative analysis configuration

Use the CLI's starter `bank00.cfg` (`auto_vectors`). For this ROM it expands to
`I_RESET=$8858`, `I_NMI=$8588`, and `I_IRQ=$2DA4`. Adding those by hand would
duplicate entries.

*Alternative:* hand-authored roots or disassembly-derived symbols. Deferred to
Phase B; inventing roots now would bias the baseline and is the kind of work
this phase is meant to size, not perform.

### D5 — Metrics from the manifest and emit output

Read totals from the emit log (`roots`, `emitted_variants`, `lle_variants`,
`banks`) and detail from `generated/program_manifest.json`, whose nodes carry
classification and `reasons`. Do not parse generated C: the framework treats
generated code as output, never as an analysis input.

*Alternative:* count functions in the emitted C. Rejected: fragile, and it
cannot produce the reason histogram.

### D6 — Determinism by a second generation

Generate twice into sibling directories and compare the SHA-256 of
`program_manifest.json`. The framework has caches; a single run cannot prove
determinism.

*Alternative:* trust the cache or compare emitted C text. Rejected: caches can
mask nondeterminism, and C formatting is not the contract.

### D7 — Scratch layout outside the repository

```
/tmp/opencode/uniracers-baseline/
├── snesrecomp/        # pinned framework checkout + built analyzer
├── gen-a/             # first generation (generated C, manifest)
├── gen-b/             # second generation (manifest comparison)
└── logs/              # emit/build logs, timing
```

`/tmp/opencode` is the approved external scratch location. It is ephemeral: only
non-derived metrics and the report survive in the change directory.

*Alternative:* a gitignored directory in the project. Rejected: the repository
is not initialized as git, and the safest default is that ROM-derived output
never shares a tree with the project.

### D8 — The report contains metrics, not game data

`report.md` lives in the change directory and contains counts, digests, timings,
toolchain versions, and reason labels. It contains no generated C, no ROM
bytes, and no excerpts of game assets.

### D9 — Build diagnostics are recorded as emitted

The generated CMake adds `-w`, so the build suppresses warnings by design.
The baseline records the build log verbatim and states that warnings are
suppressed; it does not claim a meaningful warning count.

*Alternative:* force `-Wall` and count warnings. Deferred: it would measure code
the framework already ships, not this ROM's analysis, and a flood of warnings
would not change the go/no-go decision.

### D10 — Go/no-go criteria

Phase B is recommended only if generation succeeds, the static library builds,
and both manifests match. The report then names the three most frequent LLE
reasons and the first concrete obstacle Phase B would face. If generation or
the build fails, the recommendation is remediation, not Phase B. If the baseline
is green but the AOT frontier is minimal, the report still permits Phase B but
scopes it as a boot spike with explicit symbol work.

## Risks / Trade-offs

- **Python 3.15 breaks framework tooling** → record the traceback, isolate it
  from the analyzer result, and do not patch the framework during the baseline.
- **Rust analyzer build is slow or fails** → capture the full build log; if it
  fails, that is a toolchain blocker, not a ROM verdict.
- **Analysis blows up on a 2 MiB ROM** → record elapsed time and peak behavior;
  abort past a 30-minute budget and report the budget hit as a finding.
- **Manifest schema drift at HEAD** → parse defensively; missing fields are
  reported as unavailable, never inferred.
- **Green baseline mistaken for a playable port** → the report carries an
  explicit "what this does not prove" section, and the spec forbids boot or
  playability claims.
- **Licensing** → PolyForm Noncommercial 1.0.0; generated C stays in scratch and
  is never committed.
- **Ephemeral scratch** → only metrics and the report are kept; the C can be
  regenerated from the ROM.

## Migration Plan

No production deployment. Rollback is deleting the scratch directory and the
change artifacts. If the procedure proves repeatable, turning it into a script
is a separate change.

## Open Questions

- Is the AOT-variant count a useful Phase B signal, or is the reason histogram
  the only output that changes a decision? The first run should answer this.
- Should Phase B scaffold with `--no-recomp-ui`, or include the launcher from
  the start? Deferred until the baseline shows the analyzer is healthy.
- What analysis duration should count as a blocker rather than a slow run? The
  30-minute budget above is provisional.
