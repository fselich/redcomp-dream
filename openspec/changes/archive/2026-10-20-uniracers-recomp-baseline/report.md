# Uniracers recompilation baseline (Phase A) — report

Date: 2026-10-20
Change: `uniracers-recomp-baseline`
Verdict: **GO for Phase B**, scoped as a boot spike. All three gates passed, but
the statically proven frontier is small and the first obstacle is dynamic
dispatch, not compilation.

## Input identity

| Field | Value |
|---|---|
| ROM | `Uniracers (USA).sfc` |
| Size | 2,097,152 bytes (2 MiB) |
| Mapping | LoROM / FastROM (`map_mode=$30`) |
| Region | USA |
| Coprocessor | none (`cart_type=$02`) |
| SRAM | 8 KiB, battery-backed |
| Checksum | valid |
| CRC32 | `383858c7` |
| SHA-256 | `859ec99fdc25dd9b239d9085bf656e4f49c93a32faa5bb248da83efd68ebd478` |
| Reset vector | `$8858` |

## Toolchain and pin

| Tool | Version |
|---|---|
| Python | 3.15.0 |
| rustc / cargo | 1.97.1 |
| CMake | 3.28.3 |
| Ninja | 1.11.1 |
| GCC | 13.3.0 |
| Git | 2.43.0 |
| OS | Linux 7.0.0-38-generic, x86_64 |

- SNESRecomp commit: `9c6afcbfd4106e4613e522741d667047a48ded3b`
  (2026-10-20, `Merge pull request #176 from blackerking/feat/wram-xref`).
- Native analyzer: `recompiler-rs/target/release/snesrecomp-analyze`
  (built from the same commit; 1.6 MB).
- Procedure: source checkout + `snesrecomp_cli.py build` (the published CLI
  asset is Windows-only).

## Procedure and artifacts

All ROM-derived output lives under `/tmp/opencode/uniracers-baseline/`
(`snesrecomp/`, `gen-a/`, `gen-b/`, `logs/`). The project tree contains no
generated C or manifest.

Generation used the starter `bank00.cfg` (`auto_vectors`), which seeded nine
roots: native NMI `$8588` (4 M/X states), native IRQ `$8584` (4 M/X states),
and RESET `$8858` (M1X1). No symbols, disassembly, or hand-authored roots were
added.

## Results

### Timing and build

| Step | Result |
|---|---|
| Generation `gen-a` | exit 0, 1 s total; analysis 0.021 s, emit 0.14 s |
| Generation `gen-b` | exit 0, <1 s; analysis 0.015 s, emit 0.12 s |
| CMake configure | exit 0, <1 s |
| Build | exit 0, 1 s |
| Generated C | 20,834 lines across 8 `.c` files (5 bank files + dispatch + module + stubs) |
| Static library | `build/libsnesrecomp_game.a`, 390,142 bytes |

The generated CMake passes `-w`; the build log therefore contains zero warning
lines by construction. The baseline does not treat that as a meaningful warning
count.

### Coverage totals

| Metric | Value |
|---|---|
| Analysis roots | 9 |
| Exact variants discovered | 54 |
| Unique PCs | 45 |
| AOT-eligible variants | 52 |
| LLE-only variants | 2 |
| Banks emitted | 5 |
| Demand edges | 121 |
| Direct calls + tail calls resolved AOT | 112 |
| Unresolved indirect edges (dynamic LLE) | 9 |

Variant counts are `(pc24, M, X)` entry states, not unique functions: 40 PCs
have one variant, 3 have two, and 2 have four.

### Per-bank distribution

| Bank | Variants | AOT | LLE-only | Unique PCs |
|---|---:|---:|---:|---:|
| `$00` | 9 | 9 | 0 | 3 |
| `$80` | 17 | 15 | 2 | 17 |
| `$81` | 5 | 5 | 0 | 5 |
| `$82` | 16 | 16 | 0 | 13 |
| `$83` | 7 | 7 | 0 | 7 |
| **Total** | **54** | **52** | **2** | **45** |

The per-bank counts sum to the 54 variants reported by the emitter.

### LLE reason histogram

| Reason | Nodes |
|---|---:|
| `has_lle_indirect_edge` | 9 |
| `truncated_call_continuation` | 2 |
| `unproven_callee_exit` | 2 |
| `unproven_call_at_80886D_to_838AF7_m1x0` | 1 |
| `unproven_call_at_8094F4_to_80C3AB_m1x0` | 1 |

The two LLE-only variants are both in bank `$80`:

| Variant | Instructions | Reasons |
|---|---:|---|
| `$8091DC:M1X0` | 119 | `truncated_call_continuation`, `unproven_callee_exit`, unproven call to `$838AF7` |
| `$8094EB:M1X0` | 4 | `truncated_call_continuation`, `unproven_callee_exit`, unproven call to `$80C3AB` |

The nine `has_lle_indirect_edge` nodes are AOT-eligible, but each contains an
unresolved indirect jump that resolves to `lle_dynamic` at runtime. They are
the first places execution can leave generated code even though the body was
emitted.

### Determinism

| Run | Manifest SHA-256 |
|---|---|
| `gen-a` | `d8b7b4a7c5932965216c97cfccab3b996fd4ce8f1f327f356f676a99bb06444d` |
| `gen-b` | `d8b7b4a7c5932965216c97cfccab3b996fd4ce8f1f327f356f676a99bb06444d` |

Both runs also produced the same totals (52 AOT, 2 LLE, 5 banks). The
per-output-directory cache did not carry over between `gen-a` and `gen-b`, so
the second run re-analyzed rather than replaying a cached manifest.

## Interpretation

1. The Linux toolchain is healthy. Generation and compilation are fast and
   reproducible; the bottleneck is analysis coverage, not build mechanics.
2. Only 5 of the 64 LoROM banks produced code. This is expected with nine
   conservative roots: the analyzer proved a small entry frontier and left the
   rest to runtime interpretation.
3. Dynamic dispatch is the first obstacle. The nine unresolved indirect jumps
   are inside emitted bodies, so a boot attempt will enter the interpreter
   there even if the surrounding function is AOT.
4. The two LLE-only variants are bounded, concrete targets: both hinge on
   unproven callees (`$838AF7` and `$80C3AB`) and a truncated call
   continuation.
5. The 52:2 AOT ratio describes the discovered frontier only. It is not a claim
   that most of the game is native.

## Limitations

- Conservative root set only: no symbols, no disassembly, no dispatch tables.
- Static analysis only: nothing was executed.
- Variant counts are not unique function counts.
- No runtime validation of the FastROM/mapping behavior.
- The build suppresses warnings with `-w`; the build log is captured but is not
  a code-quality signal.
- The generated project builds a static library only; it has no host.

## What this does not prove

- That the game boots, reaches a menu, renders a frame, plays audio, or accepts
  input.
- That the emitted C is semantically correct for the discovered functions.
- That the 52 AOT variants are free of runtime tripwires.
- That Phase B will be short. It only establishes that Phase B is technically
  reachable and names its first obstacle.

## Go/no-go

| Gate | Result |
|---|---|
| Generation completed | PASS (exit 0) |
| Static library built | PASS (exit 0, 390,142 bytes) |
| Manifests identical | PASS (same SHA-256) |

**Recommendation: GO for Phase B**, with the scope explicitly limited to a boot
spike. Phase B should start with the nine unresolved indirect edges and the two
LLE-only variants, and should use runtime coverage capture to find dynamic
entry points before any per-game RTL work. A playable port remains a separate,
much larger decision.

## Artifacts

- Scratch root: `/tmp/opencode/uniracers-baseline/` (ephemeral; generated C is
  not committed).
- Logs: `logs/toolchain.txt`, `logs/rom-identity.txt`, `logs/probe.txt`,
  `logs/emit-a.log`, `logs/emit-b.log`, `logs/determinism.txt`,
  `logs/build.log`.
- Metrics extract: `logs/metrics.json`, `logs/metrics-summary.txt`.
- This report contains no generated C, no ROM bytes, and no game data.
