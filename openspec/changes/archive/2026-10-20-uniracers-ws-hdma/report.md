# Uniracers HDMA margin extension (Phase E3) — report

Date: 2026-10-20
Change: `uniracers-ws-hdma`
Outcome: **P-U1 not applicable**. The enabled HDMA BG-scroll channels have empty
tables; the visible track curves are BG tilemap/scroll, not per-line HDMA. The
wide margins already sample the same BG data and show no wide-only seam defect
on the sampled curve. No table extension is needed.

## Evidence

### HDMA tables are empty

At the live-race frame 2015 the current HDMA A-bus pointers are
`$7E:8262` (ch4, BG1HOFS) and `$7E:8280` (ch3, BG2HOFS). The first byte at both
pointers (the HDMA count byte) is **0**, i.e. the channel terminates with no
lines. The same is true at the write-pointer base `$7E:9870`.

### No data writer

Trace build + debug server, with default watches cleared:

| Address | Writer | Frames | Result |
|---|---|---|---|
| `$7E:2046` low | `$82D673` | 1523–1524 | base low `$70` |
| `$7E:2047` high | `$808672` | 1529–1534+ | base slides `$50→…→$98` |
| `$7E:5070`…`$9870` | `$82E16D` | 1401–1406 | clear loop, writes zeros |
| `$7E:8262/$8280` | `$82E16D` | 1403 | zeros |

No CPU store carried per-line data, and DMA dumps at frames 1413–1437 show no
WRAM-destination channel (`bAdr=$2180`): ch0 writes VRAM `$2118`, ch2–5 are
HDMA reads from `$7E:xx`, ch6 writes `$2126`.

### Curves are BG content

The second-track curve frame 3500 was captured at 16:9 and 4:3. The large
purple wedge appears in **both**, so it is game content, not a margin seam.
The wide frame adds valid columns from the same BG data; no wide-only step or
wrapped column was observed at that frame.

### Scroll is CPU-driven

The per-frame display block writes `$210D`/`$210F` directly; the final PPU
scroll values (152/76/204/102) are the camera mirrors. The enabled HDMA
channels have empty tables, so they do not modify the scroll.

## Conclusion

P-U1 was based on the assumption that ch3/ch4 drove per-line track curves. The
runtime evidence says otherwise: the tables are empty and the curves are BG
tilemap/scroll. **No HDMA extension is required for the wide margins.**

This is a positive result: it removes a false blocker and means the renderer-only
margins are correct for this engine, at least on the sampled flat and curved
sections.

## Verification status

- 4:3 and the live-race gate from Phase E2 are unchanged.
- Wide captures: 342×224 at frames 3000 (flat) and 3500 (curve); the 3500 purple
  wedge is present in 4:3 too.
- No simulation, camera, collision, RNG or input changes.
- Remaining wide gaps are objects/cull/spawn (P7–P13) and final HUD, not HDMA.

## Recommendation

Close this change as **not applicable** and move to **`uniracers-ws-objects`**:
P7/P8 cull + OAM, then P9–P13 spawn/staging. The wide margins no longer need
HDMA work.

## Artifacts

- Workspace: `work/ws-hdma/` (gitignored).
- Trace/logs: `logs/hdma-trace*.log`, `logs/dma-dump.log`.
- Metrics: `metrics/wram-watch*.txt`,
  `metrics/wram-watch*-summary.txt`, `metrics/dma-channels.txt`.
- Frames: `frames/curve3000/`, `frames/curve3500/`,
  `frames/curve3500-43/`.
- This report contains no generated C, no ROM bytes, and no game data.
