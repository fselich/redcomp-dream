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
right edge.

The same decode exposes a second, more visible source: slots 96/97 are the
parked 2P pair in a 1P race. Their entries are `X=$165/$160`, `Y=$20/$60`,
high bit set; at `E=128` they decode as `+357/+352`, i.e. image X 485/480 and
rows 32/96. That is the fragment aligned with the rider that playtest saw.
Neither entry has a legitimate 1P position.

At smaller `E` the same entries wrap negative and stay off-screen; the width
cap makes the right-edge case the common one on ultrawide monitors.

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

### D1 — Treat the rider parking signature as undrawable

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

### D1b — Hide the parked 2P pair (slots 96/97)

In a 1P race slots 96/97 are the parked 2P pair. Their gameplay entries are
stable across all captures:

| Slot | Raw X | Y | High bit | Decoded at E=128 | Image |
|---:|---:|---:|---:|---:|---:|
| 96 | `$65` | `$20` | 1 | `+357` | x 485, row 32 |
| 97 | `$60` | `$60` | 1 | `+352` | x 480, row 96 |

They are off-screen at 4:3 (the 9-bit value wraps to `-155`/`-160`) and have
no legitimate 1P position at any width. Hide both slots in the PPU copy
whenever their high bit is set, using the same switch and gates as D1. A
positive X (high bit clear) is left alone, so a future legitimate use of those
slots is not blanked.

*Alternatives considered:*

- **Only hide the `$170`/`$70` sentinel**: this is the original implementation
  and leaves the 96/97 sliver visible at 512 px. Rejected after playtest.
- **Hide only when `lo < E`/the decoded X lands in the window**: works, but
  the pair has no valid 1P position at all; unconditional-on-high-bit is
  simpler and also covers the left margin if the cap changes.
- **Reduce the width cap to 448/BG-only**: rejected; loses the ultrawide
  presentation that is already shipping.
- **Renderer-side special case for `$170`/96/97**: rejected; the sentinel and
  the 2P pair are game-specific and the PPU contract has no knowledge of them.

### D2 — Preserve the real-rider path

The high-bit fix for an actual rider in `[256, 256 + E)` runs only for
entries *without* the high bit and with a low byte below `E`. The parking
signature and the parked 2P pair both have the high bit set, so the two cases
stay disjoint. A real rider whose true X is `368` uploads `lo = 112` without
the high bit, not `$70`; the fix adds its bit after the parked checks, so D1
and D1b cannot hide it.

### D3 — Diagnostics

Emit a diagnostic CSV behind `SNESRECOMP_WS_PARK_LOG=<path>` with
`frame,slot,lo,y,hi,rel,rel_y,E,action`. The scan walks all 128 slots and logs
three things: the `$170`/`$70` sentinel (`candidate`/`hide`), any high-bit
entry whose raw X falls in the right window (`right`), and the parked 2P pair
(`hide-pair`). It is what exposed slots 96/97 after the first sentinel-only
fix. `rel`/`rel_y` are meaningful only for rider slots 98/99. The existing
`SNESRECOMP_WS_OAM_XHIGH_LOG` keeps its format.

### D4 — Gating

The change stays inside the existing gates: widescreen on, live 1P race,
cull widening active, `E > 0`, and `SNESRECOMP_WS_OAM_XHIGH` not `0`. With the
switch off, output matches the current build; with widescreen off, native 4:3
is untouched.

### D5 — Validation matrix

- 512 px (`SNESRECOMP_WS_ASPECT=32:9` or `8:3`), Dragster frame 2500, same
  starting SRAM: exactly 9 pixels differ from pre-change, all in the parked
  96/97 footprint (x 480–486, rows 40–43 and 104–106). The sentinel sliver is
  gone in the forced-keep repro as well.
- 21:9 (`448`, `E = 96`), 16:9 (`342`) and native 4:3: byte-identical to
  pre-change with the same starting SRAM.
- A real rival in the right margin is still drawn at its true X (idle Dragster
  frame 1746, 314 px at x 458–487).
- `SNESRECOMP_WS_OAM_XHIGH=0`: byte-identical to the pre-change build,
  including the old right-edge artifacts.
- Guest state: WRAM/SRAM/VRAM/CGRAM hashes identical; OAM differs only in the
  hidden slots.

## Risks / Trade-offs

- **Wrong hypothesis (already hit once)** → the first sentinel-only fix did
  not remove the playtest sliver because slots 96/97 were also involved. The
  diagnostic's `right` rows now name every high-bit entry in the window; keep
  that scan on when investigating a new artifact.
- **One-frame lag when a rider returns from the parking state** → hiding for
  that frame is below the field; the next upload draws the real sprite. This
  is no worse than the old sliver and never affects simulation.
- **Collision with a legitimate high-bit entry** → a high bit means a negative
  X. Slots 98/99 are resolved from WRAM; slots 96/97 are only hidden while
  they are the parked pair. The diagnostic makes any unexpected `hide-pair`
  visible.
- **Hiding a real 2P sprite** → gated by `$0DDB == 0` (1P full screen), so 2P
  split screen is untouched.
- **Switch-off regression checks** → compare frames with
  `SNESRECOMP_WS_OAM_XHIGH=0` against the current build; they must be
  byte-identical.

## Open Questions

All resolved for this change:

- Which slots carry the player-visible fragment? Slot 97 (`Y=$60`) is the
  fragment aligned with the rider; slot 96 (`Y=$20`) is the smaller one above
  it. The sentinel on 98/99 is a separate, rarer case. D1 and D1b cover all.
- Which tour/track? Not needed: the 2P pair is parked in every 1P race and the
  sentinel is width-driven.
- Is the open `$03ED = 4` rider wrap visible? No; it stays a separate change.
