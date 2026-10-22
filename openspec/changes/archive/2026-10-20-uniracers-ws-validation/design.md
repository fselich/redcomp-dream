# Design: Uniracers widescreen validation (Phase E7)

## Context

- 342×224, live-race gate `$000B4 == 1 && $00EE9 == 0`.
- P1–P4 background margins are natural; P7/P8 cull/OAM is implemented with
  `SNESRECOMP_WS_CULL`.
- P2c HUD top band anchors `RACE`/`0/3` and the timer 1:1, clamps the lower
  prompt/arrow/lap-time rows, with `SNESRECOMP_WS_HUD`.
- P9–P13 are N/A for Dragster and Zoom Zoo, each measured independently.
- Track list: Dragster (first), Zoom Zoo (second), Bowl, Switcher, Monster.
  `CRAWLER` on the PICK TRACK screen is the league heading, not a track.
- 2P/VS split-screen is out of scope.

## Goals / Non-Goals

**Goals:**

- Cover all five 1P tracks with native/wide, BG3-only and object landmarks.
- Prove the HUD bands handle every top-left label and arrow side.
- Recheck the CPU ghost at both margins and the cull switch.
- Verify menus, pause and transitions.
- Prove 4:3/WS-off byte-identical to the preserved baseline.
- Fix only presentation defects found; otherwise record acceptance.

**Non-Goals:**

- 2P/VS, spawn/staging, gameplay, RNG, collision, new renderer policy.
- Rewriting archived changes.

## Decisions

### D1 — Track matrix

Use the PICK TRACK menu to select each of the five tracks. Record a per-track
route (the menu uses `Down`/`Up` + `B`). Capture:

- race countdown/start,
- mid-race,
- a late frame with the CPU ghost near a margin.

If a track is not reachable in 1P, record it as unreachable rather than
assuming.

### D2 — HUD variants

For each track, capture BG3-only and measure the top-left group, timer, arrows
and lap time. The top-band elastic segments currently cover source `[17,64)`
and `[193,256)`. If a label is wider than 64 or the timer starts before 193,
adjust the measured segment (or clamp it) and note the track. Glyphs must stay
1:1; never stretch text.

### D3 — Objects and cull

Reuse the P7/P8 probes and the natural CPU ghost:

- cull ON vs OFF at the same frame;
- signed-X left margin and X > 256 right margin;
- no object disappears at a native edge.

No spawn/stage work; those are N/A for both measured tracks.

### D4 — Modes and transitions

Capture the title, 1P menu, PICK YOUR UNI, PICK TOUR, PICK TRACK, NOW PLAYING,
countdown, pause and finish. Widescreen must be off outside live race; the HUD
policy must not leak into menus.

### D5 — 4:3 and simulation gate

- WS-off and HUD-off/cull-off captures diff against the preserved baseline.
- No guest register, OAM staging, RNG, progression or input changes.
- If a presentation fix changes 4:3, revert.

### D6 — Workspace and report

```text
work/ws-validation/
├── project/       # copy of the ws-zoomzoo-spawn build
├── scripts/       # per-track routes
├── logs/
├── frames/
└── metrics/       # per-track matrix, hashes
```

The report is a matrix: track × {4:3, wide HUD, objects, transitions} with
pass/fail and any limitation.

## Risks

- **Track selection timing** → confirm NOW PLAYING names the track; use debug
  controller input if a script press is missed.
- **HUD label width varies** → measure BG3-only, do not assume `RACE` width.
- **HUD policy leak into menus** → gate to live race and verify.
- **Probe artifacts** → probes are debug-only and must not be used in normal
  captures.
- **Scope creep** → no spawn/stage or 2P work.

## Open Questions

- Are all five tracks reachable in 1P from PICK TRACK?
- Does any track use a top-left label wider than `RACE` (30 px) or a timer
  starting before x193?
- Do any tracks use a different HUD band height or arrow side?
- Are pause/finish transitions free of HUD policy artifacts?
