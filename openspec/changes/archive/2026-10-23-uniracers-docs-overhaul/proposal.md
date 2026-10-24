## Why

`docs/` is a set of investigation logs written at different moments, each closing with the conclusion of its day, so they now contradict each other and the code: three documents still call `clamp` the answer, the handoff lists a "current blocker" that is solved, and the final BG1 design sits in §11 of a findings log. About fifty links point at `openspec/changes/<name>` paths that were archived, ten documents cite the untracked `work/` directory, the docs mix Spanish and English, and `game/README.md` still has its scaffold placeholder. We want documentation a newcomer can trust: one current description of the port, with the rejected approaches recorded as decisions.

## What Changes

- New English documentation set under `docs/`, describing the code as it is after `uniracers-code-cleanup-2`:
  - `docs/README.md`: index and a short current-state summary.
  - `docs/ARCHITECTURE.md`: modules, the frame pipeline, the guest-state rule, the framework patch.
  - `docs/WIDESCREEN.md`: how widescreen works (frame width and aspect, BG1 margin, HUD bands, OAM X-high and parked sprites, cull window, radar).
  - `docs/TRACK_MARGIN.md`: the `source` policy in detail, `clamp` as fallback, and every rejected approach with why it was rejected and where the evidence is.
  - `docs/CONFIG.md`: user options, precedence, the in-game menu, environment variables split into user and debug.
  - `docs/VALIDATION.md`: the output gate, references, re-recording, the settings round-trip test.
  - `docs/formats/TRACK_FORMAT.md`, `docs/formats/SAVE_FORMAT.md`, `docs/formats/BG1_RING.md`: the decoded formats, as reference.
  - `docs/ROADMAP.md`: the open items from `UNIRACERS_MEJORAS.md`, in English, without the finished ones.
- Move the current investigation logs (`UNIRACERS_BG1_RING_FINDINGS.md`, `UNIRACERS_WIDESCREEN_TRACK_FILL.md`, `UNIRACERS_NATIVE_MAP_SPIKE.md`, `UNIRACERS_NATIVE_MAP_HANDOFF.md`, `UNIRACERS_WS_PARKED_SPRITES.md`, `UNIRACERS_MEJORAS.md`, `docs/workshop/`) to `docs/history/` unchanged, with a `docs/history/README.md` stating they are obsolete and temporary. Nothing outside `docs/history/` links into it, so the folder can be deleted at any time without breaking a link.
- Rewrite the root `README.md` around the user (what it is, build, run, options, docs index) and `game/README.md` around the `game/` directory, removing the scaffold text.
- Fix every cross-reference: live documents cite only live documents or archived OpenSpec changes (`openspec/changes/archive/<date>-<name>`), and never `work/`.
- Fill the `Purpose: TBD` sections of the specs in `openspec/specs/`.

## Capabilities

### New Capabilities

- `recomp-docs`: rules for the project documentation: language, layout, accuracy against the code, link targets, the temporary history folder and the record of rejected approaches.

### Modified Capabilities

None. Filling a spec's Purpose does not change its requirements.

## Impact

- Files: `docs/**`, `README.md`, `game/README.md`, `tools/README.md` (only if `uniracers-code-cleanup-2` left it inconsistent), `openspec/specs/*/spec.md` (Purpose sections only).
- No code or build changes.
- Depends on `uniracers-code-cleanup-2`: it must be implemented first so the docs describe two margin policies, the margin module and the trimmed framework patch.
