# True BG1 track fill (Phase E9) — report

Date: 2026-10-20
Change: `uniracers-ws-track-fill`
Outcome: **Clamp is correct; true fill is unnecessary.** The game's track
generator authors only the native 256 columns. The wide margins are background;
the old disconnected pieces were the 256-wide VRAM map wrapping, not real
geometry.

## Evidence

### Generator

`$81:8B95` calls `$81:8A4A`, maps segment IDs through `$7E:C000/$7E:A000`, and
writes tile entries `$0230..$0242`. A synthetic call from C
(`SNESRECOMP_WS_TEST_GEN=1`) saves `CpuState` + `g_ram`, offsets `$00A5`, calls
the guest function, reads `$0230`, and restores state. It returns cleanly.

### Margin scan at frame 5000

Offsets are from the frame's `$00A5`; the native half-width is ±1024 in these
units.

```text
off = -1408 .. -1024 : 00a0 ... 00a0   (blank)
off =  1024 ..  1408 : 00a0 ... 00a0   (blank)
```

The true generator does **not** author the margin columns. The detached wide
pieces are a wrapped copy of other map columns, not track.

### Result

- BG1 is a 32×32 (256×256) map.
- Native 4:3 is continuous.
- Wide clamp: track native, margins background; no fragments.
- `SNESRECOMP_WS_TRACK=clamp` is the correct mode.
- `fill` remains experimental and is not needed.

The user-provided maps confirm Dragster is almost flat and Zoom Zoo has loops;
the discontinuity came from the wrap, not from the authored track.

## 4:3 and simulation

Clamp is presentation-only. WS-off frames and simulation are unchanged.

## Decision

**Clamp accepted.** Do not claim wide-track coverage beyond native 256; the
engine does not author it. The track-layer wrap bug is fixed by hiding the
wrapped columns, not by inventing track.

## Next

Re-run the per-track validation with the clamp as the accepted presentation and
record that track margins are native by design.
