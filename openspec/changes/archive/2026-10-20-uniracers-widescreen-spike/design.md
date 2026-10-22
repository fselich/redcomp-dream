# Design: Uniracers widescreen spike (Phase E1)

## Context

The dispatch fix made `CameraUpdate` (`$81:AE59`, M0X0) AOT-reached. The
renderer is mapped:

- Mode 1, BG1 + BG2 + BG3 + OBJ; BG2 = BG1/2 parallax.
- HDMA ch3/ch4 write `$210F`/`$210D` per line; ch5 writes `$2131`.
- Camera scroll mirrors: `$7E:2026/$202A/$202E/$2032`.
- 4:3 baseline is aligned-equivalent at `k=3` (fix N = baseline N−3).

The framework's `WIDESCREEN_PATTERNS.md` requires P16 (4:3 baseline) first,
then P5/P6 (live-race discriminator), P1–P4 backgrounds, P2c HUD, P7/P8
cull/OAM, P9–P13 spawn/staging. This spike covers the first renderer-level
milestone and documents the rest; it is not a finished 16:9 mode.

The host already exposes native widescreen:

- `SnesDesktopHostGame.native_widescreen` / `.widescreen_supported`
- `frame_width` (host width) and `g_ws_extra`
- `PpuSetExtraSpace` and the `PpuSetWidescreenLayer...` policies
- `[Graphics] Widescreen` config and `snesrecomp_desktop_set_widescreen()`

## Goals / Non-Goals

**Goals:**

- Preserve the 4:3 baseline and the `k=3` aligned gate.
- Find and verify a live-race discriminator (P5/P6).
- Render a genuinely wider field at a fixed race state (342×224), behind a flag.
- Keep the HUD readable (clamp/anchor) in the wide frame.
- Identify the HDMA table builder and document or implement P-U1.
- Report the object/cull/spawn gaps that remain.

**Non-Goals:**

- Full P7–P13 implementation (cull, OAM, spawn, staging).
- Final HUD design, netplay, audio, release, or a playable 16:9 build.
- Simulation, collision, RNG, or input changes.

## Decisions

### D1 — P16 baseline first

Use the dispatch-fix build as the 4:3 baseline. Record its frame hashes and the
`k=3` offset. 4:3 remains the default; the wide mode is opt-in. Any wide-mode
comparison must first prove 4:3 is unchanged.

*Alternative:* start at 16:9 and compare later. Rejected: the doctrine's P16
gate exists to prevent exactly that.

### D2 — P5/P6: find a real live-race discriminator

Search for a WRAM byte or word that means "in live race" and nothing else.
Candidates: the race state machine, the timer, or the player/vehicle state.
Verify with `set_wram_watch`/`interp_edges` across menu, race and pause. If no
single byte is proven, gate on a documented combination and record the risk.

*Alternative:* gate on PPU mode or TM bits. Rejected: P5 explicitly warns that
an HDMAEN or PPU mirror is not a gameplay discriminator.

### D3 — Enable native widescreen behind a flag

Set `native_widescreen = 1`, `widescreen_supported = 1`, and `frame_width = 342`
(16:9 from 4:3: 256 + 43 + 43) in the project host descriptor, guarded by the
config/env flag. Keep 4:3 as the default and keep the 4:3 build byte-identical.

*Alternative:* hard-code 342. Rejected: it removes the P16 comparison and the
ability to bisect.

### D4 — Renderer policies: start simple, measure, then refine

Use the shared PPU policies:

- BG1/BG2: `PpuSetWidescreenLayerClamp`/`Mirror`/`Repeat` as the margin evidence
  dictates; begin with natural rendering and inspect the margins.
- BG3 HUD: `PpuSetWidescreenHudSplit` or `PpuSetWidescreenLayerClamp`; start with
  clamp (authentic HUD centered) to keep it readable.
- OAM: do not widen the cull/emitter in this spike; record the pop-in.

The wide frame must use the PPU-rendered scroll (P1/P4), not the WRAM mirrors.

### D5 — HDMA builder and P-U1

Trace writes to the HDMA source tables (`$7E:8262`, `$7E:8280`) and the pointer
variable `$7E:2046` using the debug server's WRAM watchpoints or the interpreter
WRAM trace. If the builder is found, extend the per-line scroll values for the
extra margin columns. If not, document P-U1 as blocked and keep the spike
renderer-only: the wide frame will show the native curve only within the
original 256 columns, with the margins visibly wrong or clamped.

*Alternative:* invent a scroll function from the final hscroll values. Rejected:
the per-line curve is the track's geometry; guessing it would produce wrong
margins and hide the blocker.

### D6 — Capture and compare at a fixed race state

Use the deterministic route. Capture 4:3 and 16:9 at the same aligned state
(baseline N and wide N−k). Compare:

- frame dimensions and non-black/color metrics;
- whether the world is genuinely wider (new columns, not stretched pixels);
- HUD placement and readability;
- margin correctness (edge columns, curves, pop-in);
- 4:3 alignment against the preserved baseline.

### D7 — Defer objects, cull and spawn

P7/P8 (cull + OAM reject) and P9–P13 (spawn/activation/staging) are out of
scope. The report must show the symptoms (objects disappearing at the native
edge, spawn pop-in) and name the next change.

### D8 — Simulation is read-only

No camera, collision, RNG, input, or save-state changes. The wide mode is
presentation plus renderer policies. If a change alters simulation, revert it.

### D9 — Workspace

```
work/ws-spike/
├── project/       # copy of the dispatch-fix project
├── patches/       # host/config/PPU hook changes
├── logs/
├── frames/        # 4:3 and 16:9 captures
├── metrics/
└── coverage/      # optional
```

Everything is derived and gitignored.

## Risks / Trade-offs

- **HDMA builder not found** → P-U1 stays blocked; the wide frame shows wrong
  margins. That is a valid spike result, not a failure to hide.
- **P5/P6 discriminator not found** → gate on a documented combination and
  record the risk; do not use HDMAEN or a PPU mirror alone.
- **Margins show stale/wrapped tilemap data** → P2 prefill is missing; record it
  and keep the margins clamped rather than inventing content.
- **HUD clamp looks wrong** → try anchor/elastic in a follow-up; P2c needs
  per-column measurements.
- **Objects disappear at the native edge** → expected without P7/P8; record it.
- **Widescreen changes the frame offset** → re-measure `k`; use the aligned gate.
- **PPU policies do not cover this engine** → document the gap and the needed
  runner/P-U1 work.
- **Simulation changes** → revert; P16 forbids it.

## Open Questions

- Which WRAM byte/word is the live-race discriminator?
- Does the PPU's widened field sample BG1/BG2 margins correctly with the current
  tilemaps, or does it need P2 prefill first?
- Where is the HDMA table builder, and can the per-line tables be extended?
- Is BG3 clamp enough for the HUD, or does it need anchor/elastic bands?
- Does 342×224 produce the intended 16:9 from 4:3, or does the game's pixel
  aspect require a different width?
