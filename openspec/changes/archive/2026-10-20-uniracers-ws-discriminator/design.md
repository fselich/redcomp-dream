# Design: Uniracers live-race discriminator (Phase E2)

## Context

The widescreen spike renders a genuine 342×224 frame, but the gate is only the
developer flag. The menu/race WRAM diff produced 65 addresses that change from
0 to 1, but none was verified across pause or transitions. The doctrine says
P5/P6 must come before backgrounds/HUD/objects, and explicitly rejects an
HDMAEN or PPU mirror as the discriminator.

The current gate lives in `src/main.c` (`PrepareFrame` → 342 when wide is on)
and `src/game_rtl.c` (BG3 clamp). This change adds a game-state predicate.

## Goals / Non-Goals

**Goals:**

- Capture menu, live race and pause states from the same build.
- Find and verify a byte/word that means "live race" and nothing else.
- Gate the wide frame and the BG3 clamp on that predicate.
- Prove menu/pause stay 4:3 and race is 16:9, with simulation unchanged.

**Non-Goals:**

- HDMA builder/P-U1, cull/OAM/spawn, final HUD.
- Simulation, collision, RNG or input changes.
- Player-facing polish.

## Decisions

### D1 — Capture the state set

Use the deterministic route and scene dumps:

- menu at frame ≈600;
- live race at frame ≈2015;
- pause: press Start during the race (≈2200), dump at ≈2300; if Start does not
  pause, record the actual behavior and use the closest reachable state.

Capture `wram.bin` for each with `SNESRECOMP_DUMP_DIR`. Optionally capture a
transition frame to check the gate does not flicker.

### D2 — Candidate criteria

A candidate must:

- be stable for the whole live-race window (not a timer or physics accumulator);
- differ in menu and pause;
- not be a PPU/HDMAEN mirror or a scroll/camera variable (P5 warning);
- have a plausible game-state meaning (state machine, mode, race-active flag).

A verified **combination** is acceptable: a race-mode byte plus a pause flag.
`$7E:2004` was rejected after interactive testing because it tracks player/camera
state and toggles with jumps.

Start from the 65 menu→race 0→1 addresses and rank them by stability and
neighborhood context.

### D3 — Verify across states

Use the debug server watchpoints (`set_wram_watch`/`wram_watch`) or interpreter
WRAM traces to record each candidate's value and frame ranges across menu, live
race, pause and jump states. A candidate that changes inside live race, toggles
with player state, or shares the pause value is rejected.

Measured result: `$000B4` is the race-mode byte (0 menu, 1 race/pause) and
`$00EE9`/`$00EEB` the pause flag (0 race, 1 pause). The combined predicate is
`$000B4 == 1 && $00EE9 == 0`; it holds across menu, three race dumps, two pause
dumps and four jump states.

### D4 — Gate implementation

Add a host-side predicate, e.g. `InLiveRace()`, that reads the verified
address(es) from WRAM:

```c
InLiveRace() = (g_ram[0x000B4] == 1 && g_ram[0x00EE9] == 0)
```

Apply it in:

- `PrepareFrame`: return 342×224 only when the wide flag is on **and**
  `InLiveRace()`; otherwise 256×224.
- `GameDrawPpuFrame`: apply the BG3 clamp only while the wide frame is active.

Re-evaluate per frame. Verify that switching width does not reallocate the
presenter or flicker; if it does, stage the transition at a frame boundary and
record it.

### D5 — Fallback if no single byte exists

If no single byte is proven, use a documented combination (for example
race-state byte plus timer activity) and record the risk. Do not use HDMAEN or a
PPU mirror as the primary signal.

### D6 — 4:3 default and simulation read-only

The flag-off build stays 4:3. No camera, collision, RNG or input changes. If the
gate changes simulation behavior, revert it.

### D7 — Workspace

```
work/ws-discriminator/
├── project/       # copy of the ws-spike project
├── logs/
├── dumps/         # menu/race/pause WRAM
├── frames/        # menu/race/pause captures
├── metrics/
└── patches/       # gate diff
```

## Risks / Trade-offs

- **Start does not pause** → use the menu/transition states and document the
  missing pause evidence.
- **Candidate is a timer or oscillator** → reject; look for a state byte.
- **Dynamic width reallocates/flickers** → stage the switch at a frame boundary
  or keep the window fixed and change only the PPU margin.
- **Gate fails during transitions** → widen the predicate or add a debounce.
- **Candidate is a PPU/HDMA mirror** → reject per P5.
- **No single byte is proven** → documented combination with explicit risk.

## Open Questions

- Does Start pause the race, or open a different menu?
- Which of the 65 candidates is a real state byte rather than a count/timer?
- Does changing `frame_width` mid-session reallocate the presenter cleanly?
- Is a transition debounce needed between menu and race?
