# Uniracers boot spike (Phase B) — report

Date: 2026-10-20
Change: `uniracers-boot-spike`
Outcome: **`visible_nonblack`** — the game boots headless to the Nintendo logo,
the title screen, and the main menu on Linux.

## Verdict

The scaffolded desktop executable runs *Uniracers (USA)* on Linux with dummy
SDL video/audio, reaches the main menu in about 10 seconds of simulated time,
and exits cleanly at the frame budget. The generated program is almost entirely
interpreted, so the next obstacle is AOT coverage, not rendering: the capture
found 107 dynamic entries absent from the generated program.

## Input identity

| Field | Value |
|---|---|
| ROM | `Uniracers (USA).sfc` |
| Size | 2,097,152 bytes (2 MiB) |
| Mapping | LoROM / FastROM |
| Region | USA |
| Coprocessor | none |
| SRAM | 8 KiB |
| CRC32 | `383858c7` |
| SHA-256 | `859ec99fdc25dd9b239d9085bf656e4f49c93a32faa5bb248da83efd68ebd478` |

## Toolchain and pin

| Item | Value |
|---|---|
| Python | 3.15.0 |
| rustc / cargo | 1.97.1 |
| CMake | 3.28.3 |
| Ninja | 1.11.1 |
| GCC | 13.3.0 |
| SDL3 | 3.4.14 |
| Framework commit | `9c6afcbfd4106e4613e522741d667047a48ded3b` |
| Project | `<workspace>/work/boot-spike/project/UniracersSNESRecomp` |

The scaffold pinned the framework submodule to the same commit as Phase A.
`rom_identity.txt` matches the pinned CRC32 and SHA-256. The scaffold ran with
`--no-recomp-ui --no-github --no-ci`.

## Build

| Step | Result |
|---|---|
| Scaffold | exit 0, 9 s |
| `tools/regen.sh` | exit 0, 10 s |
| Generated program | 4 roots, 4 AOT variants, 0 LLE-only, 1 bank |
| CMake configure + build | exit 0, 6 s |
| Executable | `build/UniracersSNESRecomp`, 1.4 MB |
| Backend | SDL3 desktop, LLE frame composition |

The scaffold's seed differs from the Phase A CLI baseline: it applies
`force_lle` to `I_RESET` and `I_NMI` and uses `tier_down_stubs`, so only 4 AOT
variants were emitted. This is the expected LLE-first default, and it is why the
boot is interpreter-driven. The build is not warning-free (format-truncation
warnings in `host_main.c`), but it succeeds.

## Headless run

| Item | Value |
|---|---|
| Video / audio driver | `dummy` / `dummy` |
| Launcher | bypassed (`--no-launcher` + `SNESRECOMP_NO_LAUNCHER=1`) |
| Frame budget | 600 frames |
| Result | exit 0, 10.0 s simulated, 59.98 fps |
| CPU time | 3.40 s |
| Renderer | `sdl` |
| ROM load | 2,097,152 bytes, `cartType=1` |
| Crashes / tripwires | none |

A second run to 900 frames (coverage capture) also exited 0 in 16 s.

## Frame evidence

All frames are 256×224 PPM captures from the SDL presenter.

| Frame | Non-black | Unique colors | Mean luminance | Content |
|---:|---:|---:|---:|---|
| 1 | 0.00% | 1 | 0.00 | black |
| 30 | 0.00% | 1 | 0.00 | black |
| 60 | 0.00% | 1 | 0.00 | black |
| 120 | 0.00% | 1 | 0.00 | black |
| 150 | 100.00% | 16 | 111.00 | Nintendo logo + copyright line |
| 180 | 100.00% | 16 | 111.00 | Nintendo logo |
| 210 | 100.00% | 16 | 111.00 | Nintendo logo |
| 240 | 100.00% | 16 | 111.00 | Nintendo logo |
| 270 | 0.00% | 1 | 0.00 | black transition |
| 300 | 99.74% | 250 | 120.97 | *Uniracers* title screen |
| 480 | 99.50% | 135 | 126.82 | title/attract transition |
| 598 | 99.50% | 131 | 128.27 | menu |
| 600 | 99.50% | 133 | 125.72 | main menu (1P / 2P / VS / LEAGUE / OPTIONS) |

Frame-to-frame differences:

| Pair | Mean abs diff / pixel | Changed pixels |
|---|---:|---:|
| 120 → 300 | 374.96 | 99.74% |
| 300 → 480 | 156.35 | 93.55% |
| 480 → 598 | 23.72 | 38.12% |
| 599 → 600 | 19.61 | 37.16% |

The 599→600 change confirms animation in the menu scene, not a static frame.
The first non-trivial content appears between frames 120 and 150; the title
screen appears between frames 270 and 300.

## Coverage capture

| Metric | Value |
|---|---|
| Run | 900 frames |
| Interpreted instructions | 10,641,670 |
| Interpreted guest cycles | 41,440,256 |
| Distinct dynamic sites | 107 |
| Observed hits | 505,071 |
| Fallback reason | `missing_entry` (107/107) |
| Site kinds | 102 `call_gap`, 5 `goto_gap` |
| Ingest warnings | none |
| Overflow / write failures | none |
| Identity | ROM SHA-256 and program digest verified |

Top dynamic entries by observed hits:

| Site | Target | M/X | Kind | Hits | Frames |
|---|---|---|---:|---:|---|
| `$80FAD2` | `$828035` | M1X0 | call_gap | 340,308 | 116–899 |
| `$82B1F2` | `$82B293` | M1X0 | call_gap | 52,768 | 28–434 |
| `$82B1F9` | `$82B293` | M1X0 | call_gap | 52,768 | 28–434 |
| `$80FAC9` | `$828035` | M1X0 | call_gap | 46,486 | 116–899 |
| `$8280CE` | `$828151` | M1X0 | call_gap | 7,278 | 34–50 |
| `$80FADB` | `$80FADF` | M1X0 | call_gap | 718 | 117–899 |
| `$008599` | `$80F60C` | M1X0 | goto_gap | 624 | 276–899 |

`$828035` is the hottest target: 386,794 hits from two call sites. The capture
also kept 1,505 instruction-cost records for future hot-path work.

## Phase A targets

| Phase A target | Kind | Hit | Evidence |
|---|---|---:|---|
| `$8091DC:M1X0` | LLE-only | yes | 1 instruction, 3 cycles |
| `$8094EB:M1X0` | LLE-only | no | not observed |
| `$008584:M0X0` | IRQ stub | no | not observed |
| `$008584:M0X1` | IRQ stub | no | not observed |
| `$008584:M1X0` | IRQ stub | no | not observed |
| `$008584:M1X1` | IRQ stub | no | not observed |
| `$008588:M0X0` | NMI stub | yes | 164 instructions, 492 cycles |
| `$008588:M0X1` | NMI stub | no | not observed |
| `$008588:M1X0` | NMI stub | yes | 246 instructions, 738 cycles |
| `$008588:M1X1` | NMI stub | yes | 214 instructions, 642 cycles |
| `$80C3AB:M1X0` | indirect | yes | 1 instruction; entered from `$80ACE5` (call_gap, `missing_entry`, frame 436) |

Of the 9 unresolved indirect nodes, 4 were hit and 5 were not. The IRQ vector
stub `$8584` never executed during the boot-to-menu route, so an IRQ is not
part of this path. Of the 2 LLE-only variants, `$8091DC` was hit once and
`$8094EB` was not reached.

## First obstacle

Dynamic dispatch is the first concrete obstacle to a native port. The boot is
already correct enough to reach the menu, but it does so through the
interpreter: 107 distinct entries are absent from the generated program, and
the hottest one (`$828035`) accounts for roughly 77% of all observed dynamic
hits. Promoting the top targets into AOT is the next measurable step; per-game
RTL work is not justified by this evidence yet.

## Limitations

- No input was sent; the route stops at the main menu.
- Audio was opened with the dummy driver; audio fidelity was not evaluated.
- Only the boot-to-menu route was captured (900 frames).
- No comparison against a reference emulator was performed.
- The SDL presenter was used; the OpenGL presenter was not exercised.
- The generated program is the scaffold's conservative seed (4 AOT variants),
  not the 52-variant CLI baseline from Phase A.
- Frame captures are native 256×224; no widescreen or scaling claims.

## What this does not prove

- That the game is playable, accepts input, or completes a race.
- That audio, later levels, or two-player modes are correct.
- That the interpreter's behavior matches hardware frame-for-frame.
- That promoting the captured entries will preserve the boot result without a
  replay comparison (that is the next change's job).
- That the game is close to a shippable native port. The boot spike proves the
  host, mapper, PPU, and interpreter path work for this ROM; it does not size
  the remaining port.

## Recommendation

Next change: **promote the captured dynamic entries to AOT and verify
equivalence**. Concretely:

1. Add the top call/goto targets (`$828035` M1X0, `$82B293` M1X0, `$828151`
   M1X0, plus the next tier) to `recomp/symbols.toml` with `emit = true`, or
   feed the capture as a coverage profile manifest.
2. Regenerate and rebuild against the same commit.
3. Replay the same 900-frame route and compare frame CRCs/luminance and the
   discovery set for those targets.

A separate follow-up can add input-script navigation (`--script`) to reach a
race once the AOT promotion is verified. Full playability remains a much
larger, separate decision.

## Artifacts

- Workspace root: `<workspace>/work/boot-spike/`
  (persistent, gitignored; generated C, frames, and coverage are not committed).
- The spike originally ran from `/tmp/opencode/uniracers-boot-spike/` and was
  moved here after completion. The executable was rebuilt at the new path and
  the 600-frame run re-verified (exit 0, 99.50% non-black at frame 600).
- Logs: `logs/scaffold.log`, `logs/regen.log`, `logs/cmake-configure.log`,
  `logs/build.log`, `logs/run-600.log`, `logs/run-coverage.log`.
- Metrics: `metrics/frames.json`, `metrics/tier2-ingest.json`,
  `metrics/targets.json`, `metrics/phase-a-targets.json`.
- Capture: `coverage/uniracers/uniracers_1792502337_p862042_s1.json` and
  `.jsonl` (checkpoint 289,250 bytes; journal 85,033 bytes).
- This report contains no generated C, no ROM bytes, and no game data.
