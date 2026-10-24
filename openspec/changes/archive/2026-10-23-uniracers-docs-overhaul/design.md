## Context

Current `docs/` and where each piece of content goes:

| Current file | Kind | Destination of its durable content | File itself |
|---|---|---|---|
| `UNIRACERS_TRACK_FORMAT.md` | reference | `docs/formats/TRACK_FORMAT.md` (fix `work/` refs, runtime section updated) | moved and edited |
| `UNIRACERS_SAVE_FORMAT.md` | reference | `docs/formats/SAVE_FORMAT.md` | moved and edited |
| `UNIRACERS_BG1_RING_FINDINGS.md` | log | ring model and formula → `docs/formats/BG1_RING.md`; leading-edge, ghost and source sections → `docs/TRACK_MARGIN.md` | `docs/history/` |
| `UNIRACERS_WIDESCREEN_TRACK_FILL.md` | log | clamp, edge fill, tilemap copy, generator call, margin scan → rejected approaches in `TRACK_MARGIN.md` | `docs/history/` |
| `UNIRACERS_NATIVE_MAP_SPIKE.md` | log | uploader and VRAM-stitching result → rejected approaches | `docs/history/` |
| `UNIRACERS_NATIVE_MAP_HANDOFF.md` | handoff | determinism rules → `VALIDATION.md`; "do not repeat" list → rejected approaches | `docs/history/` |
| `UNIRACERS_WS_PARKED_SPRITES.md` | fix note | OAM window and parked slots → `WIDESCREEN.md` | `docs/history/` |
| `UNIRACERS_MEJORAS.md` | roadmap (Spanish) | open items → `docs/ROADMAP.md` (English) | `docs/history/` |
| `docs/workshop/**` | local notes | nothing beyond what the reports and formats already hold | `docs/history/workshop/` |

The OpenSpec archive already holds a `report.md` per phase; those are the durable evidence. The new docs summarise and link to them.

## Goals / Non-Goals

**Goals:**

- One current, English description of the port that matches the code.
- Rejected approaches recorded as short decisions with evidence links, so nobody repeats them.
- Every link resolves; `docs/history/` can be deleted without breaking anything.

**Non-Goals:**

- Editing the history files (they move unchanged).
- Rewriting OpenSpec reports or spec requirements.
- Documenting the vendored framework (`game/snesrecomp/` has its own docs).
- Translating the in-game menu labels.

## Decisions

### D1. Layout

```
README.md                  user entry: what, legal note, build, run, options summary, docs index
game/README.md             the game/ directory: regen, CMake targets, recomp configs, framework pin
docs/
  README.md                index + current state in a few lines
  ARCHITECTURE.md
  WIDESCREEN.md
  TRACK_MARGIN.md
  CONFIG.md
  VALIDATION.md
  ROADMAP.md
  formats/
    TRACK_FORMAT.md
    SAVE_FORMAT.md
    BG1_RING.md
  history/                 obsolete, temporary, unlinked
    README.md
    ...moved files...
tools/README.md            kept tool set (from uniracers-code-cleanup-2)
```

Alternative considered: keep the `UNIRACERS_` prefix. Rejected: every file is about Uniracers, the prefix only adds noise.

### D2. Content of the main documents

- `ARCHITECTURE.md`: the module table (one line per `game/src` module, what it owns, whether it writes guest state), the frame sequence (`GameRunOneFrame` → cull → beam driver; `GameDrawPpuFrame` → OAM fix, radar, margin begin, HUD, draw, margin end), the guest-state rule (`ws_cull` writes WRAM, `uniracers_save` one SRAM byte, everything else presentation-only), the framework hooks the port relies on and how the local patch is maintained.
- `WIDESCREEN.md`: when widescreen applies (live race only), frame width from aspect and `MaxWidth`, why 512 px is the cap (9-bit OAM X), then one section per layer of the solution: BG1 margin (summary, link to `TRACK_MARGIN.md`), BG3 HUD bands, OAM X-high and parked sprites, cull window, radar. Known limits (2P stays 4:3).
- `TRACK_MARGIN.md`: the problem (32×32 ring of 16×16 cells, 512 px, the leading edge holds stale cells), the `source` algorithm (verified ring cells, `$7F` table, anchor search, self-check, masking), `clamp`, then "Rejected approaches".
- `CONFIG.md`: generated from `ws_settings.c`'s table by hand, with a check task (D5).
- `VALIDATION.md`: what `validate.sh` runs, the reference file, `--record`, non-deterministic cases, determinism rules for captures, the round-trip test.

### D3. Rejected approaches format

Each entry in `TRACK_MARGIN.md` has the same four fields, kept short:

| Approach | What it was | Why rejected | Evidence |
|---|---|---|---|
| Raw wrap | let the wide renderer sample the 512 px ring | leading edge shows cells written a ring earlier | archived `uniracers-ws-track-fill` report |
| Shadow/edge fill | history of previously seen columns | wrong on direction changes, no source of truth | archived `uniracers-ws-track-margins` |
| Tilemap copy prefill | copy the native tilemap into the margins | the ring is not the level; copies are wrong cells | archived `uniracers-ws-track-fill` |
| Synthetic generator call | call stage-1/2 lookup from C | could not reproduce the chunk-to-screen transform | archived `uniracers-native-map-spike` |
| VRAM stitching / native uploader | rebuild BG1 from upload traces | uploader proven, transform missing; no-go | archived `uniracers-native-map-spike` |
| Track repeat | repeat the native band | visibly wrong geometry | archived `uniracers-code-cleanup` |
| Verified ring only (`ring`) | serve only verified cells, mask the rest | correct but leaves the leading margin empty; became step one of `source` | archived `uniracers-ws-leading-edge` |
| Ghost lane (`ghost`) | run-ahead machine harvests future cells | correct but too slow, complex snapshot/restore | archived `uniracers-ws-ghost-lane` |

The exact wording and the evidence paths are checked against the reports during implementation; the table above is the plan, not the final text.

### D4. Links

Live documents (every Markdown file outside `docs/history/`, `openspec/` and `game/snesrecomp/`, plus the comments in `game/src`) link only to live documents, to archived OpenSpec changes, to `openspec/specs/`, or to source files. They never link to `docs/history/` or `work/`. `docs/history/README.md` is the only file that describes the history folder and is not linked from anywhere either; the docs index mentions in one plain sentence that a temporary history folder exists, without a link.

Checked by a script run in the tasks: extract every relative markdown link and backticked repo path from live files and verify it exists and is not under `docs/history/` or `work/`.

### D5. Accuracy against the code

Every environment variable, config key, default and value in `CONFIG.md` and `README.md` must exist in `game/src`, and every `SNESRECOMP_*` variable read in `game/src` must appear in `CONFIG.md` (user or debug table). Checked by grep in the tasks. Hashes and frame numbers come from `validate_refs.txt`, not from old reports.

### D6. Style

English, present tense, describes what is, not what happened. No dates in living docs except in `ROADMAP.md` items. One line per paragraph in Markdown (no hard wrap). Claims that are measured cite the report that measured them.

### D7. `openspec/specs` purposes

Replace each "TBD - created by archiving change …" Purpose with one or two sentences on what the capability covers. Spike capabilities say they record a finished investigation.

## Risks / Trade-offs

- [Docs written before the code cleanup lands describe removed modes] → this change starts only after `uniracers-code-cleanup-2` is archived.
- [Content lost when logs move to history and are later deleted] → the durable content is extracted first (Context table), and the OpenSpec reports keep the evidence.
- [Docs drift from code again] → the D4 and D5 checks are scripted so a later change can rerun them.

## Migration Plan

Pure documentation. Rollback is reverting the commit.

## Open Questions

None.
