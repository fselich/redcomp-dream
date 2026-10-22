# Design: Uniracers widescreen objects (Phase E4)

## Context

The 16:9 preview renders 342×224, gated to live race, with the BG margins
sampled naturally. P-U1 was found **not applicable**: the enabled HDMA BG tables
are empty and the track curves are BG content.

The wide frame still uses the native cull/OAM windows. Opponents and objects can
disappear at the native left/right edge or fail to appear in the margins. The
doctrine requires P7 (cull) and P8 (OAM reject) together before spawn/staging
(P9–P13).

Known starting points:

- OAM address writes at `$808665/$808669` in the per-frame display block.
- OAM setup and the object update path were not mapped in Phase C.
- The route selects **Dragster**, the second entry in PICK TRACK (the NOW
  PLAYING screen reads `RACING ON DRAGSTER`). Its road runs left; use it for
  margin evidence.

**Mode scope (confirmed by the user):** 1P is full-screen against a CPU ghost.
2P and VS are horizontal split-screen and are **not** the 16:9 target. The CPU
ghost is a real full-screen OAM object; it is the margin case this change must
preserve.

**Recon result (see `work/ws-objects/object-path.md`):**

- HUD is BG3. OAM slots 98 and 99 are the player and the CPU ghost; slots 96/97
  are parked (2P pair), slot 0 is parked. No OAM HUD slots.
- The staging buffer is `$00:1501–$1510`, uploaded by `$82:D2DC`; the object
  routine `$82:ACA5` writes slot 98 at `$82:AD5D` and slot 99 at `$82:AE1C`.
- Negative OAM X is encoded as a signed 9-bit value; the runner already decodes
  it (`PpuDecodeOamX`), so there is no separate game-side unsigned reject.
- The horizontal gate is `$82:AD19/$82:AD1E` (and the twin at
  `$82:ADE7/$82:ADEC`) comparing screen X against `$0421/$0423`. At frame 3500
  that is `[-49, 256]`; the vertical gate is `[-41, 112]`.

## Goals / Non-Goals

**Goals:**

- Identify the object update, cull and OAM reject path.
- Widen the horizontal cull bounds symmetrically, preserving the vertical base.
- Validate the runner's signed 9-bit OAM X path for the left margin.
- Add independent kill switches for cull and OAM.
- Prove objects remain drawn while straddling both margins on Dragster.

**Non-Goals:**

- Spawn/activation/staging (P9–P13), final HUD, netplay, audio.
- Simulation, collision, RNG or input changes.
- Player-facing polish.

## Decisions

### D1 — Trace the object/OAM path

Use the debug server's OAM rings and sprite traces during a live race:

- identify which OAM slots are objects vs HUD;
- find the emitter's X reject compare and the cull comparisons;
- record the writer PCs and frame ranges.

If the OAM rings are insufficient, use a temporary block/WRAM trace around the
known OAM address writes and disassemble the caller.

### D2 — P7 cull windows

The game computes screen X first, then compares it against two bounds:
`$0421` (left) and `$0423` (right). Widening is therefore a bound shift, not the
doctrine's object-coordinate `(v + margin) >= (base + 2*margin)` form:

```c
left_wide  = base_left  - margin;   /* -49 -> -92 */
right_wide = base_right + margin;   /* 256 -> 299 */
```

Keep the vertical window `[-41, 112]` untouched. Do not unify player and ghost
windows; both use the same horizontal bounds today, but each call site keeps its
own base. The bounds are per-race-section values, so the widening must survive a
track/section reload.

### D3 — P8 OAM reject

The game does **not** have a separate unsigned OAM reject compare. It writes a
signed 9-bit X: negative values are the low byte with the OAM high bit set. The
runner already handles that:

```c
if (x >= 256 + ppu->extraRightCur) x -= 512;   /* PpuDecodeOamX */
```

So the P8 work here is validation, not a new reject predicate: widen the cull
window so a negative-X rider reaches OAM, then prove the runner draws it in the
left margin. If a live frame with X in `[-43, -1]` is not found, force one in a
focused test before claiming P8 complete. Do not add a redundant compare.

### D4 — Independent kill switches

Add separate env/runtime gates for cull and OAM widening (P14), so one can fall
back to authentic 4:3 behavior while the other stays active. Do not use a single
global toggle.

### D5 — Verification

- 4:3 remains aligned with the preserved baseline.
- On the Dragster curve, an object must remain drawn while straddling the left
  and right margins, not just the right one. The probes are OAM riders; the
  road direction is left.
- No spawn early/late, no progression/RNG change, no HUD shift.
- If widening changes simulation or progression, revert and record it.

### D6 — Spawn/staging deferred

P9–P13 (spawn anchors, dual pass, activation distance, staging lead) are out of
scope. The report records the observed pop-in/spawn symptoms and names the next
change.

### D7 — Workspace

```text
work/ws-objects/
├── project/       # copy of the ws-discriminator build
├── patches/       # cull/OAM hooks and kill switches
├── logs/          # traces, OAM rings, runs
├── frames/        # 4:3 and 16:9 margin captures
└── metrics/
```

## Risks / Trade-offs

- **Cull/OAM path not found** → document the evidence gap; no widening claimed.
- **OAM emitter hidden behind a shared routine** → trace the writer PC and
  disassemble; do not patch a guess.
- **Widening cull fires progression records early** → dual pass is P9–P13; keep
  this change to cull/OAM only and use kill switches.
- **Negative-X compare** → test both margins, not only the right.
- **Object count/performance** → measure; the wide view may add sprites.
- **4:3 regression** → revert and record the first bad frame.
- **HUD sprites mixed with objects** → identify slots before widening.

## Resolved Questions

- **Object vs HUD OAM:** HUD is BG3; slots 98/99 are player/CPU ghost, 96/97 are
  parked, 0 is parked.
- **OAM reject:** none separate; signed 9-bit X is decoded by `PpuDecodeOamX`.
- **Cull owner:** `$82:ACA5`, with horizontal `$0421/$0423` and vertical
  `[-41,112]`.
- **Spawn/progression:** the cull bounds are read-only gates; no spawn code was
  touched in recon.
- **Opponent rendering:** the CPU ghost is OAM (slot 99), not BG. Track curves
  and markers are BG.

## Open Questions

- Does the widened `$0421/$0423` survive every track/section reload, or does it
  need reapplying per race section?
- Which live frame, if any, puts the ghost at signed X in `[-43, -1]` for a
  natural left-margin P8 test?
