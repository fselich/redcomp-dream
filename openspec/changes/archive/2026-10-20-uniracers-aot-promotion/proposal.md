# Proposal: Uniracers AOT promotion (Phase D)

## Why

The race reconnaissance showed that the game runs **100% interpreted**: the
scaffold's only AOT functions are the four IRQ variants, which never execute.
The race capture provides 594 dynamic entries, 488 of them race-only, plus a
concrete display block (`$8086xx–$8087xx`) and camera update (`$81AE7D`).

The final goal is a native port with enhancements such as true 16:9. The
framework's widescreen hooks are injected into generated C, so the hot display,
camera and HDMA paths must become AOT before hooks can be placed. This change
performs a conservative first promotion and proves the capture → promote →
rebuild → replay loop without changing the game's behavior.

The framework's `docs/WIDESCREEN_PATTERNS.md` defines the invariants a
widescreen port must satisfy (P1–P16) and the order of work (P16 baseline gate
first, then a gameplay discriminator, backgrounds, cull/OAM, spawning, staging).
Uniracers adds a Mode 1/HDMA-specific problem: the track curves come from
per-line BG scroll writes, so margin widening must extend the HDMA tables, not
just the camera. This change records that mapping and promotes the functions
future hooks will anchor to; it does not implement any of it.

## What Changes

- Preserve the Phase C baseline build, frame hashes and coverage totals.
- Copy the scaffolded project into an isolated promotion workspace so the
  baseline remains runnable.
- Add a profile-manifest path to the project's `regen.sh` and feed the Phase C
  race capture; add explicit `symbols.toml` entries where the profile cannot
  seed a function boundary (display block, camera update).
- Promote a conservative set: the per-frame display block, the camera/scroll
  update, and the highest-hit race entries. Exclude the wait loop and the
  menu/asset VRAM upload.
- Rebuild, replay the exact 3,000-frame route, and compare frame hashes,
  interpreted-instruction totals, discoveries and tripwires against the
  baseline.
- Produce a report with the promotion set, the measured delta, and a rollback
  decision if equivalence fails.
- Record a widescreen-doctrine hand-off: map P1–P16 to Uniracers, add the
  Uniracers-specific HDMA/BG-parallax invariants, and list the code still
  missing (culling, OAM reject, spawning, gameplay discriminator).

## Non-goals

- Implementing 16:9 or any visual change.
- Promoting every capture entry or replacing the interpreter tier.
- Fixing culling, HDMA table construction, audio, or gameplay completion.
- Completing a race or adding input beyond the existing route.
- Redistributing generated C or ROM-derived output.

## Capabilities

### New Capabilities

- `recomp-aot-promotion`: conservative promotion of captured hot entries into
  AOT code for a scaffolded SNESRecomp project, with replay-equivalence proof
  against a preserved baseline.

### Modified Capabilities

None.

## Impact

- Changes only the scaffolded project under `work/aot-promotion/` (gitignored).
- Dependencies: the Phase C race capture, the pinned framework commit, the
  existing route script, SDL3, CMake/Ninja/GCC.
- The baseline in `work/boot-spike/` is not modified.
- Runtime remains bounded by the 3,000-frame route and a wall-clock timeout.
- The report contains counts, addresses and hashes only; generated C and
  frames stay in `work/`.
