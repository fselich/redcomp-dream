## Context

The widescreen gate and the source anchor were validated on tour 1. Hopper exposed two later-tour failures.

### Hopper PICK TRACK is widened

Measured with the unlocked scratch save (Hopper route, frame 1958):

```text
PICK TRACK at rest      $00B4=0x1F  $0300=0x2200  $0EE9=0
                        $0421=0xB33C  $0423=0xBB3C
PICK TRACK transition   $0300 drops to 0 while the PICK TRACK frame is still
                        displayed; $00B4 stays 0x1F
Hopper race             $00B4=0x1F  $0300=0  $0421=0xFFA4  $0423=0x012B
```

`WsInLiveRace()` sees `$00B4 != 0`, `$0EE9 == 0`, `$0300 <= 1` and returns true in the transition. The frame widens and the margin hook runs on the menu's BG1 (mode 3, map base `0x0200`), exposing the PICK TOUR margins and the `WARIO PAINT` artifact. The race cull window is a plausible screen window; the transition one is not.

### Hopper race anchor latency

Measured source log with the same route (1P, 16:9):

```text
2400..2582  anchor wait samples=0
2582..3551  anchor retry (seed 1b24/1c00, then rare words; survivors=0)
3552        anchor ok ax=72 ay=-14 anchors=1 grid=4096x64 samples=256
            latency=1077
```

The race-start overlay has no usable table words for about 107 frames; after that the general search is ambiguous/rare-word limited and only resolves at a much later track section. The track is cut at both widescreen sides until then. The radar uses the same anchor, so it is also absent.

Header measurements (`$7F:0000` words; big-endian pair interpretation):

| Track | header `0002–0009` | resolved anchor | rider table cell | header x = rider table x |
|---|---|---|---|---|
| Dragster | `0x4400 0x3200` (x=68, y=50) | ax=52 ay=34 | x=68, y=53 | yes |
| Hopper | `0x5800 0x0200` (x=88, y=2) | ax=72 ay=-14 | x=88, y=8 | yes |

The header X is the table X of the start line/rider; the rider starts 3 cells (Dragster) or 6 cells (Hopper) after the header Y. The header Y is not the rider row, so it cannot seed Y safely without a per-track offset.

For both tracks:

```text
ax = header_x - ((WsRingScrollH(vp) + rider_screen_x(vp)) >> 4)
```

with `rider_screen_x = riderX - camX` for the viewport's own rider. This gives 52 on Dragster and 72 on Hopper.

## Goals / Non-Goals

**Goals:**

- Exclude the Hopper PICK TRACK transition from the live-race gate; menus stay 256 and artifact-free.
- Seed the source X anchor from the track header on every track, so the track margins and radar fill at the first frame with usable samples (Hopper: frame 2598, before the race frame 2764; the old search found it at 3552).
- Keep the existing self-check/fallback and 1P/2P output; no guest write.
- Add Hopper menu/race validation and one second later tour.

**Non-Goals:**

- Filling the absolute first frames if the race-start overlay has no usable track words; that limit is documented.
- A per-track header-Y offset table or hardcoded Hopper values.
- Reopening the archived two-player change.
- Validating all 45 tracks (roadmap R1).

## Decisions

### D1 — Cull plausibility in `WsInLiveRace()`

Add to `ws_cull.[ch]`:

```c
int WsCullPairLooksLive(uint16_t l, uint16_t r);
int WsCullLooksLive(void);
```

`WsCullPairLooksLive` is `(int16_t)l <= 0 && (int16_t)r >= 128`; `WSApplyCull` uses the pair form for its existing base wait. `WsInLiveRace()` calls `WsCullLooksLive()` after the pause/screen checks and before the race-mode branch. It accepts 1P `-49..256`, 2P `-784..4096` and the widened values; it rejects PICK TRACK/transition `0xB33C/0xBB3C`, PICK TOUR `0x4C00/0x4C00` and NOW PLAYING `0x373C/0x4C3C`.

This is the only change to the live-race gate. If a track's cull is written late, the frame stays native for those few frames rather than widening a menu; the race is still live once the rider setup writes the window. The existing 2P gate and the per-line OAM fix are unaffected.

### D2 — Start-seeded source anchor

**X seed.** At the race scene reset, capture the rider's start world X once; the camera moves afterwards, so the current scroll must not be used for the seed:

```text
header_x      = (g_ram[$7F:0002] << 8) | g_ram[$7F:0003]   /* big-endian cells */
start_world_x = (WsRingScrollH(vp) + (riderX - camX)) >> 4  /* at scene reset */
ax_seed       = header_x - start_world_x
```

The header pair is the start line/rider X; the formula reproduces the locked anchor on Dragster and Hopper. Use the viewport's own rider/camera (P1 for the top, P2 for the bottom in 2P) and the per-viewport scroll. Add `WsRingScrollValidVp`, `WsRingScrollHVp`, `WsRingScrollVVp` (the existing `WsRingScrollValid/H/V` remain viewport-0 wrappers for the radar). The seed waits for `WS_SRC_SEED_WAIT` failed general searches, so the normal fast anchors (Dragster) are not preempted. The cull gate does change the live-race start timing, so the 1P `logs` source/park diagnostics are re-recorded; the 1P frames and wrong-cell score are unchanged.

**Y search.** With `ax_seed` fixed, use the current frame's verified samples (not the all-frame history that can still contain the race-start overlay):

- for each sample `(wx, wy, tile)`, scan the table rows `ty = 0..rows-1` at `(wx + ax_seed, ty)`; when the table word equals `tile`, candidate `ay = ty - wy` scores one;
- keep the best `ay` if it agrees with all/most samples; otherwise do not seed;
- install the seed as the single anchor only if the existing self-check agrees. The normal `WsSrcSearch` remains the fallback and handles all ambiguity/pruning.

This is generic: the header is per track, the X formula uses the live scroll, and the Y comes from the table, not a Hopper constant. It does not guess the header-Y offset (3 on Dragster, 6 on Hopper).

**When it resolves.** On Hopper the current samples reach 95% agreement at frame 2586 (`ax=72 ay=-14`); that seed is dropped by the self-check on the race scene reset and re-seeds at frame 2598. The race screenshot is at frame 2764, so the track margins and radar are already filled when the race starts. The old general search only found the anchor at frame 3552. The start overlay before frame 2581 has `samples=0`; the seed cannot invent a word and the margins stay masked (never wrong). The absolute first frames remain the game's overlay limit. If the seed is ambiguous or disagrees, the old search runs unchanged.

**2P.** The seed is per viewport; the header X is shared, the rider/camera and scroll are per half. The 2P source state and wrappers are unchanged.

### D3 — Validation

- Hopper route from an unlocked scratch save (copy of `save.srm`; never the player's save):
- transition after selecting `WARIO PAINT`: frame is 256 px, no artifact, PICK TRACK/TOUR native;
- race: capture at the first usable frame and later; BG1 margins fill both sides and the radar appears at the seeded anchor; BG1 probe wrong cells 0;
- record first anchor frame and latency.
- A second later tour (e.g. Jumper) checks the header seed is not Hopper-only.
- The existing 1P/2P frames (16 cases) must pass unchanged. The cull gate changes the live-race start timing, so the 1P `logs` source/park diagnostics are re-recorded; its frame hash and wrong-cell score stay unchanged. The 2P references should not change.
- Add the Hopper menu/race cases to `validate.sh` with an unlocked scratch save; the default validation save is empty, so the route needs the unlock copy.

## Risks / Trade-offs

- **Header X is not a start line on some tracks** → the seed is only a candidate; the self-check drops it and the old search runs. Record the header and resolved anchor for every new track.
- **Header Y is not the rider row** → do not seed Y from it; the Y search uses the first samples. The absolute first frames stay masked until the overlay ends.
- **Cull written late** → a few native frames at race start, never a wide menu. Verify on Hopper and the existing gate. The 1P `logs` source/park hashes may change with the live-start timing; the frame and wrong-cell score must not.
- **2P seed** → per-viewport scroll/rider; the existing 2P gate must stay output-identical at the tested widths.
- **Later tours may have other table shapes** (grid 256×64, 128×128, 1024×16, 64×256, 512×32, 16×1024) → the X wrap and Y scan use the existing `WsSrcCell`; record the second later tour.

## Migration Plan

No data migration or option. The cull gate is transient; the seed is computed per race from the `$7F` image. Reverting the change returns the old latency and the Hopper menu widening; no save changes.

## Open Questions

- Does the first usable sample on other later tours appear at a similar early frame, or only after a distance?
- Is the header X always the rider table X on every grid shape? The two measured tracks say yes; the second later-tour test checks one more.
- Can the header Y plus the start-line/rider offset be derived safely for the absolute first frame? Not in this change; only the X seed plus the Y sample search is committed.
