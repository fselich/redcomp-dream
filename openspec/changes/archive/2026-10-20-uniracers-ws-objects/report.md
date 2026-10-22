# Uniracers widescreen objects (Phase E4) — report

Date: 2026-10-20
Change: `uniracers-ws-objects`
Outcome: **P7 implemented; P8 validated.** The CPU ghost is a full-screen OAM
object in 1P and now survives the widened cull window. 4:3 is unchanged and the
cull words have no spawn/AI/camera/RNG readers.

## Scope

1P is full-screen against a CPU ghost. 2P and VS are horizontal split-screen
modes and are not the 16:9 target. The CPU ghost is the margin object this
change protects.

Track identity: the route selects **Dragster**, the second entry in PICK TRACK;
the NOW PLAYING screen reads `RACING ON DRAGSTER`. Its road runs **left**. The
figures placed at both margins in the P8 captures are synthetic OAM probes, not
track geometry, so they do not indicate the road direction.

## Object path

- HUD is BG3; OAM slots 98/99 are the player and CPU ghost. Slots 96/97 are
  parked (2P pair); slot 0 is parked.
- OAM staging buffer: `$00:1501–$1510`.
- Uploader: `$82:D2DC` (sets OAM address `$00C0`, copies 16 bytes to `$2104`,
  then writes the high-table byte `$1599`).
- Object staging: `$82:ACA5`; slot 98 X/Y at `$82:AD5D/$82:AD60`, slot 99 at
  `$82:AE1C/$82:AE21`.
- Negative X is a signed 9-bit value (low byte plus OAM high bit). The runner
  already decodes it in `PpuDecodeOamX`.

## Cull

The game computes screen X, then compares it with `$0421` (left) and `$0423`
(right):

- horizontal: `$82:AD19/$82:AD1E` and `$82:ADE7/$82:ADEC` (plus
  `$82:AEA7`, `$82:AF75`, `$82:AFFE`);
- at frame 3500 the native window is `[-49, 256]`;
- vertical: `[-41, 112]`, unchanged;
- `$0421/$0423` are written only by the rider setup (`$81:A385…`) and read
  only by those five `CMP`s. No spawn, AI, camera, collision or RNG reader.

## P7 implementation

`src/game_rtl.c` shifts both horizontal bounds by 43 px while widescreen is
active inside a live race:

```c
left  = base_left  - 43;   /* -49 -> -92  */
right = base_right + 43;   /* 256 -> 299 */
```

The base is captured on entry and recaptured when the game installs a new pair
(track/section reload). The vertical window and all other object logic are
untouched. `SNESRECOMP_WS_CULL=0` is the independent kill switch.

## P8 validation

The runner's signed-X decode was tested with a presentation-only OAM probe
(`SNESRECOMP_WS_TEST_OAM`, default unset; it writes one OAM slot after the game
logic and before rasterization, so it cannot change simulation):

| Probe | 342×224 | 256×224 |
|---|---|---|
| slot 99 X = −40 (raw 472) | ghost visible in the left margin | only a 1 px sliver at x=0 |
| slot 99 X = 270 (raw 270) | ghost visible in the right margin | identical to baseline (invisible) |

Hashes at frame 1741:

- wide left probe: `fa9d555f…`
- native left probe: `5cd30ac1…`
- wide right probe: `265650d5…`
- native right probe: `deff8481…` (same as the no-probe 4:3 frame)

The same probe pair on the **Dragster** frame 3500 (tile 0, the player sprite)
shows a rider at each margin in 342×224, while the 256×224 run clips both. This
is the curve evidence; the road itself runs left.

Artifacts: `frames/p8-curve-both/` (wide, both margins) and
`frames/p8-curve-both-native/` (4:3, both clipped).

Natural cull evidence at frame 1741: with cull on, the game admits the ghost at
native X=2 and the wide frame shows it; with `SNESRECOMP_WS_CULL=0` it matches
the pre-change wide frame and the ghost is hidden.

## 4:3 and simulation

- 4:3 frame 1741 SHA-256 is identical before and after the patch
  (`deff8481…`).
- With widescreen off, `$0421/$0423` are never written by the host.
- The cull-on/off WRAM delta is exactly `$0421/$0423` plus the same run-to-run
  noise present between two cull-off runs. There is no consistent progression,
  RNG or camera change.
- No spawn/activation/staging (P9–P13) work was done.

## Remaining gaps

- The live 1P route did not naturally produce a frame with the CPU ghost at
  X > 256 or at X in [−43,−1]; the signed-X and right-margin cases are covered
  by the presentation probe and the static cull audit.
- 2P/VS split-screen is out of scope for 16:9.
- P9–P13 (spawn anchors, dual pass, activation distance, staging lead) remain a
  separate change.

## Artifacts

- `work/ws-objects/object-path.md` — full path map.
- `work/ws-objects/metrics/baseline-hashes.txt`,
  `metrics/cull-1741-hashes.txt`, `metrics/p8-margin-hashes.txt`.
- `work/ws-objects/frames/trick-wide/`, `frames/trick-native/`,
  `frames/cull-wide/`, `frames/cull-wide-off/`, `frames/p8-inject-wide/`,
  `frames/p8-inject-native/`, `frames/p8-right-wide/`,
  `frames/p8-right-native/`.
- `work/ws-objects/logs/` — OAM, WRAM, cull and injection runs.
- `work/ws-objects/patches/cull-window.diff`.

No generated C, ROM bytes or game data are stored in the repository; all
derived output is under the gitignored `work/ws-objects/`.
