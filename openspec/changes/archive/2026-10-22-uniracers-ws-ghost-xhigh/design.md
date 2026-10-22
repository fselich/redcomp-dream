# Design: rider OAM X high bit in the right margin

## Context

The diagnosis lives in `work/ws-ghost-offscreen/`:

- `logs/still.json` + `logs/still-1725-1750.txt`;
- `frames/w169-17xx.png`;
- `scripts/run.sh`, `rec.py` and `ana.py` to reproduce.

Route: `work/ws-native-map-spike/scripts/route-dragster.txt`. The player stays
idle after the start and the rival leaves to the right.

### Rider staging (1P path, `$0DDB == 0`)

From the disassembly (`work/ws-leading-edge/scripts/dis.py ROM 82:ACA5 230`):

| | Player (slot 98) | CPU rival (slot 99) |
|---|---|---|
| world X / Y | `$0411` / `$0415` | `$0413` / `$0417` |
| camera X / Y | `$0419` / `$041D` | same |
| screen Y cull | `[-41, 225)` | `[-41, 225)` |
| screen X (OAM) | `rel = wX − camX`, **unshifted** | same |
| cull compare | `rel << $03ED` vs `[$0421, $0423)` (cull units are ×2^zoom px) | same |
| OAM X low byte | `$1509 = X & $0D49` (`$0D49 = $FFFF`) | `$150D` |
| OAM Y | `$150A` | `$150E` |
| X high bit | `$1599 \|= $30` **only if X < 0** | `$1599 \|= $60` **only if X < 0** (bit 6 = slot 99) |
| culled | X=Y=`$70`, `$1599 \|= $30` | X=Y=`$70`, `$1599 \|= $C0` |

The uploader `$82:D2DC` copies `$1501–$1510` to OAM slots 96–99 and then
`$1599` to the high table. The high-table bits are 2 per slot:
`bit 2k` = X high, `bit 2k+1` = size, with `k = slot − 96`.

The bug: for positive X in `[256, $0423)` the high bit stays 0, so the renderer
decodes `X − 256`. It cannot happen natively, because `$0423 = 256`.

### Timing

In the screenshots the drawn position trails the WRAM `$0413 − $0419` value by
up to about one frame of rider motion (≤ ~16 px at top speed). The fix must
not assume the WRAM X at `GameDrawPpuFrame` equals the uploaded X exactly.

## Goals / Non-Goals

**Goals**

- A rider that passes the widened right cull is drawn at its true X in the
  right margin.
- No phantom on the left.
- Presentation-only: no guest memory writes, no simulation change.
- Native 4:3 and WS-off output stay byte-identical.

**Non-Goals**

- Changing the cull, the parking position or the game's OAM staging.
- 2P split screen.
- Generic OAM hints (`PpuWsSetOamLeftHints`) for other objects.

## Decisions

### D1 — Fix the PPU copy, not WRAM

Patch `ppu->highOam` (the uploaded high table) in `GameDrawPpuFrame`, before
rasterization, next to `WsInjectTestOam`. Writing `$1599` would also work, but
it is guest state that the game reads back with `ORA`/`AND`. It could leak into
the next frame and diverge from the native run. The PPU copy is rebuilt by the
uploader every frame, so a per-frame patch cannot accumulate.

### D2 — Resolve the 256-px ambiguity from WRAM

For each rider `r ∈ {98: $0411, 99: $0413}`:

```c
int16 want = (int16)(rd16(wx) - rd16(0x0419));   /* true screen X, NOT shifted */
int   lo   = ppu->oam[slot*2] & 0xFF;
int   hi   = (ppu->highOam[slot >> 2] >> ((slot & 3) * 2)) & 1;
/* only the missing-bit case: positive, uploaded without the high bit */
if (!hi && lo < E && want >= 256 - SLACK && want < 256 + E + SLACK
        && abs((lo + 256) - want) < abs(lo - want))
    set high bit for slot;
```

- Comparing distances (candidate `lo` vs `lo + 256`) tolerates the
  about-one-frame lag of D-Timing without requiring an exact match.
  `SLACK = 32` covers the measured per-frame motion.
- `lo < E` limits the fix to the only aliased range `[0, E)`. An on-screen
  rider with `lo ≥ E` is never touched.
- The parked/culled state has the high bit set (`$C0` / `$30`), so it is never
  touched.
- Never clear a bit. Never touch Y, tile, size or other slots.

### D3 — Gating and switch

Active only when all of these hold:

- `WideEnabled() && InLiveRace()`;
- `$0DDB == 0` (1P full screen);
- `WsCullEnabled()`;
- `WsExtra() > 0`;
- `SNESRECOMP_WS_OAM_XHIGH` is not `0`.

With the cull off, `$0423` stays 256 and the bug cannot occur. The fix is then
a no-op by construction, but it is still gated off. The switch is independent
of `SNESRECOMP_WS_CULL` (spec: independent kill switches).

### D4 — Optional log

`SNESRECOMP_WS_OAM_XHIGH_LOG=<path>` writes one CSV line per fixed slot and
frame: `frame,slot,lo,want`. It is used by the verification only.

## Risks / Trade-offs

- **Lag larger than `SLACK`** (e.g. a camera snap): the fix declines and the
  native-like phantom stays for that frame. Measure it in verification.
- **Zoom (`$03ED > 0`)** — *corrected after a regression*. Zoom Zoo uses
  `$03ED = 2`. The staging code does `TAX` **before** the `ASL` loop, so the
  OAM X is the unshifted `rel`; only the cull compare (and the sign test for
  the high bit) uses `rel << $03ED`. The first implementation shifted `want`,
  so at 21:9 (E = 96) the **player** (slot 98, `rel` ≈ 56–95) got the high bit
  and vanished into the right margin: 1,038 bad fixes in one Zoom Zoo run, none
  at 16:9. Evidence: `work/ws-ghost-xhigh/regress-zoom/`.
  The same unit mismatch affects the cull widening (`uniracers-ws-objects`).
  The Zoom Zoo base `[-196, 1024]` is `[-49, 256]` px ×4, so adding E widened
  it by only E/4 px. `WsApplyCull` now widens by `E << $03ED`.
- **Wrong slot or bit mapping**: verified on the recorded frames 1740–1742
  before it is generalized.
- **Ultrawide**: `E ≤ 128` keeps `[256, 256 + E)` inside `PpuDecodeOamX`'s
  right range; no extra handling.

## Open Questions

- ~~Does Zoom Zoo (or any 1P track) use `$03ED ≠ 0`?~~ Yes: Zoom Zoo = 2
  (see Risks, Zoom).
- Is the lag ever more than one frame of motion?
