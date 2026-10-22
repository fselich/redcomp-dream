# Design: Uniracers race reconnaissance (Phase C)

## Context

Phase B produced a working headless build that reaches the main menu, plus a
tier2 capture of the boot route. That capture shows 107 dynamic entries and
10.64M interpreted instructions, but all of them belong to boot/menu code. The
only AOT functions are the four IRQ variants, which never execute on this route.

The final goal is a native port with enhancements such as true 16:9. The
framework's widescreen doctrine is written for 2D tilemap engines (MMX/SMW) and
injects hooks into generated C.

A preliminary route check (before this change was applied) reached a 1P race and
dumped the PPU at frame 2015: Mode 1 (`bgmode=$19`), `m7sel=0`, main screen
BG1+BG2+BG3+OBJ (`tm=$17`), BG1/BG2 scroll at different rates, and HDMA active
on channels 2–5, with channels 3/4 writing `$210F`/`$210D` (BG2/BG1 horizontal
scroll) per line. The track's curves are scanline scroll, not Mode 7. This change
maps that path; it does not implement widescreen.

## Goals / Non-Goals

**Goals:**

- Reach a 1P race deterministically from the menu.
- Capture a bounded race segment with frames, tier2 coverage, and state dumps.
- Identify the race hot paths and the renderer path (modes, layers, HDMA/Mode 7,
  camera/viewport/culling candidates).
- Assess whether true 16:9 is feasible, what it would touch, and which functions
  would need AOT promotion first.

**Non-Goals:**

- Implementing 16:9 or any visual change.
- Promoting functions to AOT or editing generated C.
- Completing a race, audio validation, performance tuning, netplay, or mods.
- Full disassembly/decompilation.

## Decisions

### D1 — Reuse the Phase B build

Use the existing scaffolded project at
`work/boot-spike/project/UniracersSNESRecomp` (same pinned commit and ROM
digest). No re-scaffold, no regeneration: this change is about runtime behavior.

*Alternative:* rebuild from scratch. Rejected: it adds no evidence and risks
changing the binary under test.

### D2 — Input route: manual discovery, scripted replay

The menu shows `1P / 2P / VS / LEAGUE / OPTIONS`. Discover the actual button
sequence with short trial runs, then encode it in a `--script` file using
`press`, `wait`, and `until`. The user's manual play is an acceptable source for
the initial sequence; the script is what makes it repeatable.

The host already supports the grammar; no host changes are expected.

*Alternative:* drive the menu by poking WRAM. Rejected: it bypasses the input
path the port must support and is harder to validate.

### D3 — Determinism is measured, not assumed

Run the same script twice and compare frame hashes at fixed markers (for
example, every 60 frames during the race). Also compare the coverage site sets.
If the race uses RNG or timing-sensitive input, record the first divergence
frame instead of claiming determinism.

*Alternative:* trust a single run. Rejected: a non-deterministic route makes
every later AOT/widescreen comparison meaningless.

### D4 — Bounded capture with narrow windows

Start with a 1,800-frame budget (≈30 s). Capture:

- frames at selected markers via `SNESRECOMP_SCREENSHOT` (one per run) or a
  narrow `SNESRECOMP_SCREENSHOT_DIR` window;
- `--framedump` with `SNESRECOMP_FRAMEDUMP_PIXELS=1` and a narrow
  `START/_END` range for PPU pixels, VRAM and WRAM at a few representative
  frames;
- `SNESRECOMP_STATE_TRACE` if per-frame CPU state is needed to localize a site.

Do not dump every frame: it adds hundreds of megabytes without extra decision
value.

### D5 — Coverage: race paths separated from menu paths

Enable `SNESRECOMP_TIER2_CAPTURE=1` with `SNESRECOMP_COVERAGE_DIR` under the
scratch root. Ingest against `src/gen/program_manifest.json` from the same
build. Extract:

- top instruction costs (`hot_instructions`) and the addresses that appear only
  in the race capture, not in the Phase B menu capture;
- new `discoveries` with site/target, kind, fallback reason, hit count and frame
  range;
- the interpreted-instruction total for the race window.

### D6 — Renderer mapping from hot PCs to PPU writes

Correlate the race hot PCs with the SNES register map:

- PPU mode and layers (`$2105`, `$212C/$212D`), scroll (`$210D–$2114`), window
  and color math;
- HDMA channels (`$4300–$437F`) and their source tables, especially channels
  3/4 writing `$210F`/`$210D` for per-line BG2/BG1 horizontal scroll;
- whether any Mode 7 registers (`$211B–$2120`) are written (preliminary: no).

Method: take the hot PCs, inspect the corresponding 65816 instructions, and
record which register each site writes. The framework's decoder/disassembly
tooling or a 65816 disassembler is sufficient; full decompilation is out of
scope. If a site cannot be resolved, report it as a candidate rather than a
fact.

*Alternative:* statically scan the ROM for writes to Mode 7 registers. Rejected:
it produces many false positives and no execution evidence.

### D7 — Widescreen feasibility has two layers

Assess separately:

1. **Renderer-level widening** — BG tilemap margin sampling and the HDMA
   per-line scroll tables that produce the track's curves. Some of this may be
   possible in the runner without touching game logic.
2. **Engine-level widening** — camera clamps, object culling, track streaming
   and HUD placement (BG3). This is where generated-C hooks (and therefore AOT
   promotion) may be required.

The report must say which layer the first 16:9 experiment would touch and what
must be promoted to AOT before hooks can be injected.

### D8 — Workspace layout

```
work/race-recon/
├── scripts/     # input scripts and route notes
├── frames/      # selected captures (PPM/BMP)
├── coverage/    # tier2 checkpoint + journal
├── metrics/     # derived JSON and tables
└── logs/        # route discovery, runs, ingest
```

Everything is ROM-derived or runtime-derived and stays gitignored.

### D9 — Bounded runs

Every run uses `SNESRECOMP_RUN_FRAMES` and a 5-minute wall-clock `timeout`.
If the route needs more frames, extend the budget explicitly and record why.

## Risks / Trade-offs

- **Menu route unknown** → start with short trial runs; ask the user for the
  buttons they use; iterate.
- **Race needs more selections (character/track)** → the route may take several
  iterations; keep each trial short and record the frame where the menu changes.
- **RNG or timing sensitivity** → measure divergence; restrict comparisons to a
  stable window if needed.
- **Hot PCs are noisy** → rank by exclusive instruction cost and by hits, and
  only inspect the top sites.
- **Capture size** → narrow frame windows; never dump every frame.
- **Renderer mapping needs disassembly** → limit to the top sites and record
  unknowns as candidates.
- **Widescreen may be infeasible without deeper reverse engineering** → the
  report must say so plainly; a negative result is still a successful recon.
- **Copyright** → frames, coverage and disassembly stay in `work/`; the report
  contains counts and addresses only.

## Open Questions

- How are the HDMA source tables generated, and can they be extended for the
  extra margin columns?
- Which BG layers need margin streaming, and where is object culling done?
- Is the race deterministic under scripted input beyond the first marker? (A
  preliminary two-run check at frame 2400 was byte-identical.)
- Which frames best represent the race for a later 16:9 comparison?
- Does the framework's widescreen hook layer already expose a Mode 1/HDMA path,
  or is a new runner mechanism required?
