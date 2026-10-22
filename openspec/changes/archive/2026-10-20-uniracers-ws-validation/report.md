# Uniracers widescreen validation (Phase E7) — report

Date: 2026-10-20
Change: `uniracers-ws-validation`
Outcome: **Accepted for 1P with the track clamp.** Horizontal gameplay, HUD,
objects, menus and the 4:3 gate pass. The track layer's disconnected wide
fragments were resolved as a 256-wide-map wrap: the true generator authors no
track in those margins, so BG1 is clamped to native 256 and the margins remain
background. The track is native by design.

## Track matrix

All five 1P tracks are reachable; `CRAWLER` is the league heading, not a track.

| Track | Selection | NOW PLAYING | Wide | BG3 | OBJ | Native |
|---|---|---|---|---|---|---|
| Dragster | 0 Down | `RACING ON DRAGSTER` | ✓ | ✓ | ✓ | ✓ |
| Zoom Zoo | 1 Down | `OVER 3 LAPS ON ZOOM ZOO` | ✓ | ✓ | ✓ | ✓ |
| Bowl | 2 Down | `DOING STUNTS ON BOWL` | ✓ | ✓ | ✓ | ✓ |
| Switcher | 3 Down | `RACING ON SWITCHER` | ✓ | ✓ | ✓ | ✓ |
| Monster | 4 Down | `OVER 3 LAPS ON MONSTER` | ✓ | ✓ | ✓ | ✓ |

Artifacts: `frames/matrix/`, `frames/tracks/`, `track-matrix.md`,
`matrix-captures.md`.

## HUD

- Top-left spans after the P2c policy: x4–41 max (`STUNT` on Bowl).
- Timers: x278–324.
- `RACE`, `0/3`, `STUNT` all fit the source `[17,64)` segment; no per-track
  segment change was needed.
- Bowl's bottom-right `/68` stays under the clamp band.
- Glyphs are 1:1; no stretch or overlap.

## Objects and cull

- OBJ-only on every track shows only the rider; track content is BG.
- P7/P8 probes on the shared rider path: signed-X left and X>256 right draw in
  wide and clip in 4:3.
- `SNESRECOMP_WS_CULL=0` matches the pre-cull frame.
- No object pop was observed.

## Gate fix found by validation

The old gate `$0B4==1 && $0EE9==0` was also true on PICK TRACK and NOW PLAYING:
`$0B4=1`, `$0EE9=0`, but `$0300=0xE2`. Widescreen leaked into those menus.

Fix in `main.c`/`game_rtl.c`:

```c
return g_ram[0x000B4] == 1 && g_ram[0x00EE9] == 0 &&
       g_ram[0x00300] <= 1;
```

Evidence:

- NOW PLAYING: 256×224 (authentic).
- Live race: 342×224.
- `$0300` is 0/1 during the race/countdown and 0xE2/0x4C in menus.
- Countdown may render 4:3 until `$0300` becomes 1; live race is 16:9.

## 4:3 and simulation

- Native 256×224 frames captured for all five tracks.
- WS-off path is unchanged; the gate/HUD changes are presentation-only.
- RNG, progression, collision, camera and input were not touched.
- HUD=0 restores the clamp; cull=0 restores native edge behavior.

## Track margin resolution

Reproduced on Zoom Zoo with `right` held:

- native 4:3 at frame 5000: continuous S-curve;
- wide 16:9 at frame 5000: the same S-curve plus a detached blue/green segment
  at the right edge;
- BG1-only confirms the detached segment is in the track layer;
- BG1 is a 32×32 (256×256) map, so the extra 43 px margins sample its wrap.

The follow-up `uniracers-ws-track-fill` decoded the track generator and called
it on a saved guest state. Margin offsets (|off| >= 1024 from `$00A5`) return
blank (`$00A0`): the game does not author track beyond native 256. The correct
presentation is therefore the BG1 clamp; the margins are background, not
missing track. See `docs/UNIRACERS_WIDESCREEN_TRACK_FILL.md`.

## Caveat

The signed-X/right-margin probes were re-run on Dragster/Zoom Zoo, not
individually on Bowl/Switcher/Monster; the cull code is shared and the matrix
shows only the rider, so this is a sampling caveat, not a known defect. If the
release gate must be strict per track, run the two probes on the remaining
three tracks.

## Decision

**Accepted for 1P** with the BG1 clamp. Track margins are native by design;
the wrap fragments are hidden and no guest state is changed.

## Artifacts

- `work/ws-validation/frames/matrix/`, `frames/tracks/`, `frames/gate/`
- `track-matrix.md`, `matrix-captures.md`
- `logs/`, `metrics/`
