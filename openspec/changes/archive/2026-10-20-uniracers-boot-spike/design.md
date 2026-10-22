# Design: Uniracers boot spike (Phase B)

## Context

Phase A established that the toolchain works and produced a reproducible static
baseline: 9 roots, 54 variants (52 AOT-eligible, 2 LLE-only), 5 banks, and 9
unresolved indirect edges. It executed nothing.

Phase B is a runtime experiment. It uses the framework's own project scaffold
and desktop host, runs the game headless for a bounded number of guest frames,
captures frames and coverage, and classifies the outcome. The spike succeeds if
it produces a verdict with evidence, not if the game is playable.

The framework already exposes the needed host controls:

- `--no-launcher`, `--rom <path>`, `--framedump <dir>`
- `SNESRECOMP_RUN_FRAMES`, `SNESRECOMP_FRAMEDUMP_START/_END`,
  `SNESRECOMP_FRAMEDUMP_PIXELS`
- `SNESRECOMP_SCREENSHOT`, `SNESRECOMP_SCREENSHOT_DIR/_FROM/_TO`, `PRESENT_LOG`
- `SNESRECOMP_TIER2_CAPTURE`, `SNESRECOMP_COVERAGE_DIR`
- `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`

## Goals / Non-Goals

**Goals:**

- Build a real Linux executable from the pinned ROM and the pinned framework
  commit using the project scaffold.
- Run it headless for a bounded frame budget and produce at least one classified
  boot outcome.
- Capture frame evidence and runtime coverage, including whether Phase A's 9
  unresolved indirect edges and 2 LLE-only variants are hit.
- Leave a report naming the first concrete obstacle and whether targeted RTL
  work is justified.

**Non-Goals:**

- Playability, input tuning, audio fidelity, widescreen, save states, netplay,
  or mods.
- Full symbol/disassembly work or wholesale dispatch-table reconstruction.
- Shipping or committing generated C, frames, or coverage journals.

## Decisions

### D1 — Scaffold with `setup_project.sh` and `--no-recomp-ui`

Use the framework's project wizard rather than hand-assembling a host. The
desktop host and beam frame driver are the framework's; the wizard wires them,
the ROM identity, the regen script, and the packaging.

`--no-recomp-ui` skips the Dear ImGui pre-boot launcher and its submodule. The
host still resolves a ROM (positional path or `rom.cfg`) in text mode, which is
what a headless spike needs.

*Alternative:* build the CLI-generated `gen-a` tree into an executable by hand.
Rejected: it would duplicate the host integration the wizard exists to own.

### D2 — Pin the framework submodule after scaffolding

`setup_project.sh` passes `--snesrecomp-ref` to `git submodule add -b`, which
expects a branch, not a commit. A commit SHA silently falls back to the default
branch.

Procedure:

1. Scaffold **without** `--generate --build`.
2. `git -C snesrecomp checkout --detach 9c6afcbf…` and `git add snesrecomp`.
3. Verify `git -C snesrecomp rev-parse HEAD` equals the pin.
4. Run `tools/regen.sh` and the CMake build.

This keeps the pin explicit without depending on the current `main`.

*Alternative:* scaffold with `--no-submodules` and symlink the Phase A checkout.
Rejected: it produces a project whose pin metadata is false, and the explicit
detach is one command.

### D3 — All project and runtime output stays in scratch

```
<project>/work/boot-spike/
├── project/           # scaffolded project (its own git repo)
├── logs/              # scaffold, regen, configure, build, run logs
├── shots/             # frame captures (PPM and/or BMP)
├── coverage/          # tier2 checkpoint + journal
└── metrics/           # derived JSON summaries
```

The spike initially ran from `/tmp/opencode/uniracers-boot-spike/`; the
artifacts were moved to `work/` after completion so they survive reboots.
`work/` is gitignored, so the repository still receives only the report and
non-derived metrics. Frames and coverage contain ROM-derived material and are
never committed.

### D4 — Headless execution with dummy drivers

Run with `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, `--no-launcher`, the
ROM path, and `SNESRECOMP_RUN_FRAMES`. A wall-clock `timeout` (5 minutes per
run) bounds interpreter-tier execution.

If the default OpenGL presenter cannot initialize under the dummy driver,
select the SDL presenter in `config.ini` and record which presenter ran. The
spike is not a rendering-quality test; it needs a presented frame, not a
particular backend.

*Alternative:* run under Xvfb with GL. Rejected unless the dummy path cannot
produce frames: Xvfb adds a dependency and hides presenter failures.

### D5 — Frame evidence: a small, deterministic set

Use one run to 600 guest frames and capture a narrow window (for example frames
598–600) with `SNESRECOMP_SCREENSHOT_DIR`, plus a frame-1 capture from a short
run. If the screenshot path fails, fall back to `--framedump` with
`SNESRECOMP_FRAMEDUMP_PIXELS=1` and a narrow `START/_END` range.

Derived metrics per frame: non-black pixel fraction (max RGB > 8), unique color
count (capped), mean luminance, and frame-to-frame difference. Store metrics
JSON; never commit the images.

Threshold: a frame is "non-trivial" when its non-black fraction is at least 1%.
The threshold is deliberately low: the spike asks whether anything rendered, not
whether it looks right.

*Alternative:* dump every frame. Rejected: hundreds of megabytes for no extra
decision value.

### D6 — Coverage capture and ingestion

Enable `SNESRECOMP_TIER2_CAPTURE=1` with `SNESRECOMP_COVERAGE_DIR` pointing
inside the scratch root. Keep the `src/gen/program_manifest.json` from the same
build, so the ingest identity check is meaningful.

Ingest with:

```sh
python3 tools/tier2_ingest.py <checkpoint.json> <journal.jsonl> \
  --cfg-dir recomp \
  --program-manifest src/gen/program_manifest.json --json
```

Record interpreted entries, fallback reasons, and whether the Phase A targets
appear: the 2 LLE-only variants (`$8091DC:M1X0`, `$8094EB:M1X0`) and the PCs of
the 9 `has_lle_indirect_edge` nodes (read from the Phase A manifest).

*Alternative:* infer coverage from stdout alone. Rejected: it cannot distinguish
an interpreted entry from a logged error.

### D7 — Boot outcome classification

Classify exactly one outcome:

| Outcome | Evidence |
|---|---|
| `visible_nonblack` | At least one captured frame meets the non-trivial threshold |
| `black` | All captured frames are below the threshold, process ran to budget |
| `tripwire_or_crash` | Nonzero exit or tripwire/crash message before the budget |
| `loader_failure` | ROM resolution, identity, or host init failed |
| `hang` | Wall-clock timeout reached |

The report names the first error or the first dispatch site that leaves
generated code, with the log excerpt that supports it.

### D8 — Success is a verdict, not a port

A clean `black` classification is a successful spike. The failure mode to avoid
is an ambiguous result ("it ran but we do not know what happened"). Every run
must end in one of the five outcomes and leave enough evidence to justify the
next step.

### D9 — Targeted dispatch work only

If the capture shows a specific unresolved edge blocking boot, investigate that
edge and its function boundary. Do not begin broad symbol or disassembly work in
this change; that is a separate decision informed by this spike.

## Risks / Trade-offs

- **OpenGL presenter fails under the dummy video driver** → switch to the SDL
  presenter; if neither presents, fall back to `--framedump` and classify the
  capture path.
- **Scaffold network or submodule failure** → capture the log; verify the
  framework URL and pin before retrying.
- **The game runs far slower than real time in the interpreter** → lower the
  frame budget; record frames per second and adjust.
- **`SNESRECOMP_RUN_FRAMES` is not honored before the launcher** → pass both
  `--no-launcher` and `SNESRECOMP_NO_LAUNCHER=1`, and verify the frame loop
  started from the log.
- **Coverage hooks absent in the scaffolded host** → verify the capture
  destination is logged; if unavailable, report it as a limitation and use
  `STATE_TRACE`/stdout for localization.
- **Ingest identity mismatch** → always ingest against the manifest from the
  exact build that produced the capture.
- **Hang** → 5-minute wall-clock timeout, then classify as `hang` with the last
  log line.
- **Copyright** → frames, coverage, and generated C stay in `/tmp`; the report
  contains counts and addresses only.

## Migration Plan

No production deployment. Rollback is deleting the scratch root and the change
artifacts. If the spike produces a repeatable harness, turning it into a script
is a separate change.

## Open Questions

- Does the default presenter initialize under `SDL_VIDEODRIVER=dummy`, or does
  `config.ini` need the SDL presenter?
- Is 600 guest frames enough to reach the first visible output, or does the boot
  sequence need 1800?
- Does tier2 capture start before the launcher path, or only once the host is
  in-game? The docs say headless env capture works; the first run will confirm.
- What frames-per-second does the interpreter tier sustain for this ROM, and
  does that fit the 5-minute budget?
