## Context

Read first:

- `docs/UNIRACERS_TRACK_FORMAT.md`: the format. The rest of this document
  assumes it.
- `openspec/changes/uniracers-ws-leading-edge/report.md`: Option A mechanism
  (seen table, renderer margin hook, why no VRAM bracket).
- `openspec/changes/uniracers-ws-map-decode/report.md` §4: the source decision.

Code that exists today (`src/game_rtl.c` in the shared tree):

| Piece | Where | Role |
|---|---|---|
| `WsOffwindowMode()` | ~l.641 | `ring`=1, `source`=2 (**unused**), `ghost`=3 |
| `WsOffwindowLine()` | beam line hook | line 1: scene guard (map base / BGMODE), blank char search, scroll unwrap (`s_ow_unwrapped_h/v`); every line: `WsOwObserveLine` fills `s_ow_seen[32][32]` = {wx, wy, tile} |
| `WsOwLineScroll()` | | per-line unwrapped scroll (`s_ow_base_h/v` + line delta) |
| `WsOffwindowTile()` | renderer margin hook (`g_ppu_margin_tile_hook`, called in `PpuDrawBackgroundBig` for `screen_x < 0 \|\| >= 256`) | ring-verified → ghost (mode 3) → mask |
| `WsOwFloor16`, `WsOwMapAddrBase`, `WsOwLogVisit` | | helpers; `SNESRECOMP_WS_OFFWINDOW_LOG` visit log |

The world key used there, `w = floor16(unwrapped_scroll + screen)`, is the same
key validated in the ring findings. The table key is a constant offset from it
per race (map-decode §2.6): `table = w + A`.

Format facts used here (WRAM, `g_ram[0x10000 + off]`):

```text
cols  = (byte $7F:000D or 256) * 4        rows = (byte $7F:000E or 256) * 4
id    = u16 $7F:000F + 2*((ty>>2)*cols + (tx>>2))      (0 <= tx < cols*4, 0 <= ty < rows*4)
word  = u16 $7F:800F + id*32 + (ty&3)*8 + (tx&3)*2
outside the grid -> 0x0000 (what the game shows there)
```

`$7F` is written only during track load (frames ~1409–1820 on the routes),
static afterwards.

## Goals / Non-Goals

**Goals:**

- Every BG1 margin cell in 16:9 equals the authentic world cell, including
  leading-edge cells and cells never seen natively. Proof: 0 wrong served cells
  against the probe.
- Negligible cost (target < 0.2 ms/frame).
- Fail safe: an invalid anchor masks, as `ring` does; it never shows wrong
  content.

**Non-Goals:**

- Ultrawide widths, BG2/cull/HUD changes.
- Unpacking RNC in the host (the game already did it into `$7F`).
- Removing `ghost` or `ring`.

## Decisions

### D1 — Table reader over live WRAM

Read `$7F` directly from `g_ram` on each lookup; no cache is needed. Validate
the header when the anchor is established:

- `cols * rows * 2 == 0x8000`;
- the piece-table end word `$7F:000B` lies inside the bank.

If the header is invalid, the mode behaves as `ring`. Alternative rejected:
copying the table into a host buffer at load. It needs a load-complete signal
and only saves two reads.

### D2 — Anchor by matching verified cells

*(Revised during implementation; see `report.md` §1 "Deviations".)*

Unknown `A = (ax, ay)` with `table(w + A) == tile` for verified cells.

- **Samples:** every valid entry of `s_ow_seen` (all frames: the map is static,
  so older observations stay true and cover more ground). Skip words that are
  `0x0000` (unwritten ring cells at race start) and words absent from the piece
  library (race-start overlay). Use at most 256 samples; at least 8 are needed.
- **Seed:** the rarest sampled word. A per-piece bitmask of where it occurs
  gives the candidate positions; at most 4096 candidates, otherwise try the next
  seed.
- **Survivors:** candidates matching ≥ 95 % of the samples. Keep **up to 8**
  distinct survivors (repeated sections are legitimately ambiguous). With more
  than 8, retry on a later frame.

Cost: 0.015–0.6 ms per search, once per race plus cheap retries.

Rejected alternative: deriving `A` from a game camera variable. No plain `$7E`
word was found (map-decode §2.6).

### D3 — Self-check every frame

After each drawn frame, compare this frame's verified cells (same filters) with
every live anchor:

- prune anchors below 95 %;
- with none left, mask and re-search;
- log pruned anchors and partial agreement (`SNESRECOMP_WS_SOURCE_LOG`).

### D4 — Serving order in `WsOffwindowTile` (mode 2)

```text
ring-verified (s_ow_seen match)               -> VRAM tile (unchanged)
all live anchors give the same table word     -> that word (log masked=0)
else                                          -> mask word (unchanged)
```

### D5 — Scene and race lifecycle

Invalidate `A` on every Option A scene reset (map base / BGMODE change), when
leaving `InLiveRace()`, and when the self-check fails. The `$7F` header is
re-validated on every anchor search, so a new track load is picked up.

### D6 — Validation and measurements

Probe captures with `SNESRECOMP_WS_OFFWINDOW=source` + `SNESRECOMP_BGPROBE_*` +
`SNESRECOMP_WS_OFFWINDOW_LOG`, scored with
`work/ws-offwindow/scripts/world_key_check.py --served --mask-log`. Use the same
windows and routes as `uniracers-ws-leading-edge/report.md` §3 (Dragster →/←,
Zoom Zoo, Bowl, Switcher, Monster).

Required:

- 0 wrong served cells on every track;
- leading-side masked count ≈ 0 once anchored;
- native 4:3 SHA-256 `3417f91a…df44` with the switch unset;
- frame 2500 WRAM/VRAM/CPU/DMA dumps identical to `ring`;
- cost (process CPU seconds against `ring`, 2,500 frames);
- anchor latency (frames from race start to first valid anchor) per track.

Optional cross-check: per served margin cell, `source` vs `ghost` words in the
same run (`ghost` stays available).

## Risks / Trade-offs

- [Anchor ambiguity on repetitive geometry] → uniqueness rule, rare-word seed,
  retry; worst case is masked margins for a few frames.
- [Ring and table disagree for reasons not yet seen (dynamic BG writes, tracks
  outside the five)] → the D3 self-check masks and logs. Extend validation to
  more tracks with routes if possible.
- [Wrong-track `$7F` contents after a track change without a scene reset] →
  header re-validation plus the self-check.
- [Start-of-race blank cells] → the table value is served (decided in
  map-decode 3.6). The margin may show track where the native window briefly
  shows blank; this is accepted.
- [Probe "unverifiable" cells increase] → expected: `source` serves cells never
  seen natively. Spot-check them visually against the offline renderer
  (`work/ws-map-decode/scripts/render_track.py`).

## Migration Plan

Test switch first. If D6 passes, a follow-up decides the default (`source`
replacing `clamp` for 16:9). Rollback = `ring` or unset.

## Open Questions

- Can `A` be read from a game variable instead of searched?
- Do non-1P tracks (tours beyond Crawler) behave the same? Spot-check two once
  routes exist.
