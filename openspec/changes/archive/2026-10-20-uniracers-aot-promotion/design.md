# Design: Uniracers AOT promotion (Phase D)

## Context

Phase C produced a deterministic 1P route and a race capture:

- 3,000 frames, 40,960,774 interpreted instructions, 594 discoveries
  (488 race-only), 14,017 hot instruction PCs.
- Baseline frame hash at frame 2400:
  `0bd41144740f9ebe70a452e538519d86ba6b386b4503e0e692c866bf994cb260`.
- The renderer is Mode 1 with BG1/BG2 parallax and HDMA per-line scroll.
- The per-frame display block is `$8086xx–$8087xx`; the camera/scroll update is
  `$81AE7D–$81AE9B`; the HDMA channel setup is around `$808755–$808769`.
- The only AOT functions are the four IRQ variants, which never execute.

The next goal is a native port with true 16:9. The framework's widescreen hooks
are injected into **generated C**, so the functions that will host those hooks
must first be promoted from the interpreter. This change performs a conservative
promotion and proves replay equivalence; it does not implement widescreen.

## Goals / Non-Goals

**Goals:**

- Preserve the Phase C baseline executable, frame hashes and coverage totals.
- Promote a conservative set: the display block, the camera/scroll update, the
  HDMA setup, and the highest-hit race entries.
- Rebuild and replay the exact 3,000-frame route.
- Prove frame equivalence and measure the interpreted-instruction reduction.
- Record the widescreen-doctrine hand-off (P1–P16 plus Uniracers-specific
  HDMA/parallax invariants).

**Non-Goals:**

- Implementing 16:9, culling, OAM widening, spawning, or staging.
- Promoting every discovery or removing the interpreter tier.
- Audio validation, gameplay completion, or performance tuning beyond the
  measured delta.

## Decisions

### D1 — Isolated promotion workspace

Copy the scaffolded project to
`work/aot-promotion/project/UniracersSNESRecomp` and leave
`work/boot-spike/project/UniracersSNESRecomp` untouched. The baseline build and
its frames remain runnable for comparison.

*Alternative:* modify the boot-spike project in place. Rejected: it would
destroy the baseline the replay gate depends on.

### D2 — Wire the race capture into regeneration

The project's `tools/regen.sh` does not expose profile manifests today. Add a
repeatable `--profile-manifest <path>` flag that forwards to
`snesrecomp_cli.py generate`, then pass:

- the Phase C race checkpoint (`coverage/uniracers/*.json`);
- the matching journal (`coverage/uniracers/*.jsonl`).

This is a project-local change; the framework already supports the flag.

*Alternative:* call the CLI directly from the change. Rejected: the project's
regen path is the reproducible entry point, and a future developer should not
have to reconstruct the command.

### D3 — Conservative promotion set

Promote:

1. The per-frame display block containing `$808622` (INIDISP), `$80863B` (TM),
   `$80883C` (MOSAIC), `$8087D9` (HDMAEN), `$8087DE` (CGWSEL), and the OAM
   address writes.
2. The camera/scroll update `$81AE7D–$81AE9B`.
3. The HDMA channel setup at `$808755–$808769`.
4. The highest-hit race-only call targets, for example `$81B664`, `$8182E6`,
   `$8185BB`, `$818A4A`, `$82A6F1`, `$83ED33`, `$83EF10`, `$81B77B`.

Exclude:

- `$82D4EE` (`BIT $57; BPL`): the frame/vblank wait loop. Promoting it adds code
  for a spin that should keep waiting; it is a performance topic, not a hook
  anchor.
- `$82B1F5`/`$82B1FC`: the menu/asset VRAM upload loop. It is not a widescreen
  path.
- The blank, blanket promotion of all 488 race-only discoveries. Replay cost and
  risk are too high for the first pass.

*Alternative:* promote everything the capture offers. Rejected: a failed replay
would not tell us which promotion caused it.

### D4 — Explicit symbols where the profile needs a boundary

The profile manifest can seed observed call targets, but some need an
independent function boundary. Add `[[func]]` entries with `emit = true` in
`recomp/symbols.toml` for the display block and camera update if the generated
manifest does not classify them as AOT-eligible. Regenerate after every
symbol change.

Inspect the emitted manifest before building: confirm the promoted roots, the
AOT/LLE split, and that no unintended bank was pulled in.

### D5 — Replay equivalence

Keep the exact Phase C route (`work/race-recon/scripts/route-1p.txt`) and run
the promoted build for 3,000 frames. Capture frames at 2400 and 3000 and compare
PPM SHA-256 against the preserved baseline. Ingest coverage against the promoted
build's `src/gen/program_manifest.json` and compare:

- interpreted-instruction total;
- discoveries for the promoted targets;
- new tripwires or crash output.

### D6 — Acceptance: exact or stable aligned equivalence

Accept if:

- the build succeeds;
- frames at 2400 and 3000 match the baseline exactly, or match at a documented,
  stable offset (promoted N = baseline N−k) with identical visual/state content;
- no new tripwire/crash appears;
- the interpreted-instruction total decreases measurably;
- promoted targets are resolved (no longer `missing_entry`) or are recorded as
  emitted-but-not-reached dispatch gaps.

Measured Phase D result: the promoted build is visually identical but two
frames ahead at frame 2400 (promoted N = baseline N−2 across 2395–2405). The
aligned gate passes; the exact fixed-frame gate does not. The baseline is
preserved; the promoted build is retained with the offset documented.

If no stable alignment exists, record the first divergent frame and the active
promoted targets, and restore the baseline. A divergence is a finding, not a
failure to hide.

### D7 — Workspace

```
work/aot-promotion/
├── project/       # isolated copy of the scaffolded project
├── logs/          # regen, build, replay logs
├── frames/        # promoted-build frames at the markers
├── coverage/      # promoted-build tier2 capture
└── metrics/       # comparison JSON and hashes
```

Everything is derived and stays gitignored.

### D8 — Widescreen doctrine carry-over

The framework's `docs/WIDESCREEN_PATTERNS.md` defines P1–P16 and an order of
work. The promotion set is chosen so future hooks have generated-C anchors.
This change does not implement the patterns; it records the mapping and the
missing code.

| Pattern | Uniracers status | Note |
|---|---|---|
| P1 scroll phase from PPU | applies | use `g_ppu->hScroll/vScroll`, not the `$7E:2026` mirrors |
| P2 margin prefill | applies | BG1/BG2 margins, per HDMA line |
| P2b bounded arenas | likely N/A | continuous track; verify no authored bound |
| P2c HUD rigid/elastic | applies | BG3 `RACE`/timer bar; measure in isolation |
| P3 periodic fold | verify | BG2 parallax may be periodic |
| P4 key to rendered scroll | applies | camera mirrors advance before the PPU write |
| P5 gameplay discriminator | **missing** | need a live-race byte, proven across modes |
| P6 liveness, not mode alone | **missing** | require an observable consequence |
| P7 cull windows | **missing** | cull code not located |
| P8 OAM reject compare | **missing** | OAM setup found; reject compare not |
| P9–P12 spawn/activation | **missing** | spawn logic not located |
| P13 staging lead | **missing** | track staging not located |
| P14 kill switches | plan | one gate per widening |
| P15 previews stand down | plan | only if margins get previews |
| P16 4:3 bit-identical | **gate, offset-aware** | AOT promotion shifts fixed-frame hashes by 2 frames; compare aligned frames or fix AOT timing |
| P-U1 HDMA per-line extension | **new** | not covered by the doc; hardest renderer step |
| P-U2 BG2 = BG1/2 parallax | **new** | preserve per-layer offsets when widening |

Order of work, adapted:

1. P16 baseline gate (Phase C frames; already deterministic).
2. P5/P6 gameplay discriminator (missing).
3. P1–P4 + P-U1/P-U2 backgrounds/HDMA (after this change).
4. P2c HUD.
5. P7/P8 cull + OAM.
6. P9–P12 spawning.
7. P13 staging, then P14/P15/P16 verification.

The hand-off is deliberately explicit: the widescreen change must not start at
"just widen the viewport". P-U1 means the HDMA tables must be extended, and
P5–P13 mean several engine paths still need discovery.

**Measured Phase D hand-off:** the conservative promotion produced 16 roots /
39 AOT variants and cut interpreted instructions by **7.91%** (40,960,774 →
37,722,540). `FrameDisplay` and 10 of 11 top race targets are AOT;
`CameraScroll` is emitted and registered in `dispatch_v2.c` but still runs in
the interpreter (19,201 instructions in `$81AE7D–$81AE9F`), so indirect-call
dispatch remains a widescreen blocker. The fixed-frame hashes shift by two
frames. P-U1 remains blocked on the HDMA builder; P5–P13 remain missing.

### D9 — No widescreen code

No margin sampling, HUD anchoring, cull widening, OAM changes, spawn changes,
or HDMA-table extension in this change. The only output is AOT promotion plus
the replay/doctrine report.

## Risks / Trade-offs

- **Profile seeds unsafe or too many variants** → inspect the manifest before
  building; fall back to explicit symbols.
- **AOT changes frame timing** → the framework models cycles, but the replay gate
  is the authority; classify the first divergence.
- **The display block lives under a force-LLE NMI path** → the profile may not
  promote it; use an explicit boundary or record it as blocked.
- **Function boundaries are wrong** → tripwires or wrong frames; revert and
  narrow the promotion set.
- **The wait loop stays interpreted** → expected; it is not a widescreen anchor.
- **Missing culling/OAM/spawn code** → widescreen remains blocked on discovery;
  the hand-off records it.
- **HDMA table builder still unresolved** → P-U1 cannot start until it is found;
  recorded as the first widescreen blocker.
- **Copyright** → generated C and frames stay in `work/`; the report contains
  counts, addresses and hashes only.

## Resolved Questions

- **Profile seeding:** the profile alone seeded 274 roots across 8 banks; it did
  not seed the display block or camera as clean boundaries. Explicit symbols
  were required and narrowed the set to 16 roots / 5 banks.
- **Target survival:** 10 of 11 top race targets emitted and were reached;
  `CameraScroll` emitted but was not reached (indirect-call dispatch gap).
- **Timing:** AOT does not preserve exact fixed-frame hashes; the promoted build
  is two frames ahead at 2400, with identical visuals/state.
- **Reduction:** 7.91% fewer interpreted instructions (3,238,234).

## Open Questions

- How does the interpreter's indirect-call bridge decide to enter an AOT body?
  `CameraScroll` is registered but never reached.
- Can AOT cycle accounting be made bit-identical to the interpreter, or is the
  aligned-frame gate the permanent comparison for P16?
- Where are the gameplay discriminator, cull windows, OAM reject compare, and
  spawn/staging code? These remain the widescreen blockers.
- Where is the HDMA table builder for P-U1?
