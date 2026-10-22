## Context

The ultrawide work (`uniracers-ws-ultrawide`) caps the race frame at 512 px,
so every aspect at or above 21:9 in `auto` mode resolves to `E = 128` per
side. `uniracers-ws-ghost-xhigh` later added parked-rider handling for the
game's sentinel OAM entry (`X = $170`, `Y = $70`, high bit set): it hides the
slots only when the rider's WRAM screen position is *not* near `(-144, 112)`.

That guard is wrong precisely when the rider is genuinely parked. With
`E = 128`, `PpuDecodeOamX` maps the 9-bit OAM X `368` as `+368`, because its
wrap threshold is `256 + E = 384`. The 64-px-wide parked sprite then starts at
world X `368`, i.e. image X `496`, and its edge shows as a 16 px sliver at the
right edge. Playtest describes exactly that: a fragment of the current
character on the right side, "like a reflection". It is intermittent because
it appears only on frames where the game has parked a rider.

At smaller `E` the same sentinel wraps to `-144` instead and can show a left
sliver, which the previous change already treated. The width cap makes the
right-edge case the common one on ultrawide monitors.

## Goals / Non-Goals

**Goals:**

- Confirm from a capture that the artifact is the `X = $170` / `Y = $70`
  sentinel on slots 98/99 at `E = 128`.
- Never draw a parked sentinel in either margin, whatever the WRAM position
  and whatever `E` is (up to the 128 cap).
- Keep the fix presentation-only and reversible with the existing
  `SNESRECOMP_WS_OAM_XHIGH` switch.
- Keep 16:9 and native 4:3 output byte-identical.

**Non-Goals:**

- Changing the 512 px cap, the cull, or the OAM high-bit logic for real
  right-margin riders.
- Fixing the open zoomed-track rider wrap (`$03ED = 4`) unless the capture
  proves the reported artifact is that defect.
- Any guest WRAM, simulation, spawn, AI, collision or RNG change.
- 2P split screen and widths above 512 px.

## Decisions

### D1 — Treat the parking signature as undrawable

In `WsFixRiderOamXHigh`, when a rider slot matches the parking signature
(`high bit set`, `lo = $70`, `Y = $70`), always move it below the field in the
PPU copy (`Y = $E0`, high bit cleared) instead of consulting WRAM. The
sentinel is not a real screen position: at the current cap the intended
`-144` is outside the left edge (`-128`), and at `E = 128` the raw value lands
inside the right window. The WRAM test only decided whether to keep a sprite
that cannot be legitimately visible.

This makes the existing "move below the field" path unconditional for the
signature and removes the special case that produced both the old left sliver
and the reported right sliver.

*Alternatives considered:*

- **Hide only when the decoded X lands in the window** (`E >= 112`): fixes the
  right edge but keeps the left-sliver case at smaller margins. Rejected;
  the sentinel is never valid at any capped width.
- **Reduce the width cap to 448/BG-only**: rejected; loses the ultrawide
  presentation that is already shipping.
- **Renderer-side special case for `$170`**: rejected; the sentinel is
  game-specific and the PPU contract has no knowledge of it.

### D2 — Preserve the real-rider path

The high-bit fix for an actual rider in `[256, 256 + E)` runs only for
entries *without* the high bit and with a low byte below `E`. The parking
signature has the high bit set, so the two cases stay disjoint. A real rider
whose true X is `368` uploads `lo = 112` without the high bit, not `$70`, so
D1 cannot hide it.

### D3 — Diagnostics

Reuse the parked branch's decision points to emit a diagnostic CSV behind a
new path switch, e.g. `SNESRECOMP_WS_PARK_LOG=<path>` with
`frame,slot,lo,y,hi,rel,rel_y,E,action`. The capture is needed to prove the
hypothesis before changing behaviour and to confirm that no legitimate rider
is caught by D1. The existing `SNESRECOMP_WS_OAM_XHIGH_LOG` keeps its format.

### D4 — Gating

The change stays inside the existing gates: widescreen on, live 1P race,
cull widening active, `E > 0`, and `SNESRECOMP_WS_OAM_XHIGH` not `0`. With the
switch off, output matches the current build; with widescreen off, native 4:3
is untouched.

### D5 — Validation matrix

- 512 px (`SNESRECOMP_WS_ASPECT=32:9` or `8:3`) on a route where a rider is
  culled and parked: OBJ-only render shows no pixels in either outer margin,
  whereas the pre-change build shows the right-edge sliver.
- 21:9 (`448`, `E = 96`) and 16:9 (`342`): no change versus pre-change; the
  parked sprite was not visible at 16:9.
- Native 4:3 with WS off: frame hash unchanged.
- A real rival in the right margin is still drawn at its true X (reuse the
  `ws-ghost-xhigh` right-margin route).

## Risks / Trade-offs

- **Wrong hypothesis** → capture first (tasks §1). If the OAM entry behind the
  sliver is not the parking signature, stop and record the actual slot/entry;
  the zoom-wrap issue is a separate change.
- **One-frame lag when a rider returns from the parking state** → hiding for
  that frame is below the field; the next upload draws the real sprite. This
  is no worse than the current sentinel sliver and never affects simulation.
- **Collision with a legitimate `lo = $70`, `Y = $70`, high-bit entry** →
  sign semantics require a negative X for the high bit, and the low byte and Y
  are the exact sentinel constants. The capture confirms there is only one
  such entry; the diagnostic log makes any exception visible.
- **Switch-off regression checks** → compare frames with
  `SNESRECOMP_WS_OAM_XHIGH=0` against the current build; they must be
  byte-identical.

## Open Questions

- Does the user's capture show the parked sentinel on slot 98 (player), 99
  (rival), or both? D1 covers both.
- Which tour/track triggers it most often? The fix is width-independent, but
  the reproduction route in tasks §1 should use that track if the user can
  supply it.
- Is the open `$03ED = 4` rider wrap (`uniracers-ws-ghost-xhigh` report,
  "rival flashing in wrong places") visible in the same capture? If yes, keep
  it separate.
