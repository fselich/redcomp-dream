# Uniracers live-race discriminator (Phase E2) — report

Date: 2026-10-20
Change: `uniracers-ws-discriminator`
Outcome: **corrected after interactive testing**. The first gate (`$7E:2004`)
was a camera/player-state byte and toggled with jumps. The final gate is
**`$7E:000B4 == 1 && $7E:00EE9 == 0`** (race mode and not paused), verified
across menu, race, pause and four jump states.

## What went wrong with `$2004`

The first build gated on `$7E:2004 == 0x80`. In actual play the frame toggled
between 16:9 and 4:3 constantly, spending more time in 4:3, and correlated with
jumping. The state dumps explain it: `$2004` is `$80` in race dumps but `$40` in
menu/pause, and it is a camera/player-state byte, not a live-race flag. The
no-input race dumps happened to keep it at `$80`; real jumps cycle it.

## Final discriminator

| Address | Role | menu | race 1/2/3 | pause | jump B/A/X/Y |
|---|---|---:|---:|---:|---:|
| `$000B4` | race mode | 0 | 1/1/1 | 1 | 1/1/1/1 |
| `$00EE9` | pause flag | 0 | 0/0/0 | 1 | 0/0/0/0 |
| `$2004` | camera/player state | 40 | 80/80/80 | 40 | 80/80/80/80 |

Gate:

```c
InLiveRace() = (g_ram[0x000B4] == 1 && g_ram[0x00EE9] == 0)
```

- menu: `0 && ...` → false.
- live race (including jumps): `1 && 1` → true.
- pause: `1 && 0` → false.

Dumps used:

- menu: frame 600;
- race: frames 2015/1675/1975/2175;
- pause: frames 2338 and 2298 (`CONTINUE GAME / QUIT`);
- jumps: 1718/1821/1924/2027 (B/A/X/Y presses).

`$00EE9`/`$00EEB` are the pause flag; `$000B4` is the race-mode byte. Neither
toggles with the jump states tested.

## Gate implementation

Patch: `work/ws-discriminator/patches/live-race-gate.diff`.

- `PrepareFrame`: 342×224 only when the wide flag is on **and** `InLiveRace()`;
  256×224 otherwise.
- `GameDrawPpuFrame`: BG3 HUD clamp only in the gated wide mode.
- `SNESRECOMP_WS=1` (or `[Graphics] Widescreen`) still enables the feature;
  4:3 remains the default.

## Verification status

- **Predicate verified by WRAM dumps** across menu, three race frames, two pause
  frames and four jump states.
- **Headless width captures are not frame-exact**: `SNESRECOMP_SCREENSHOT`
  captures the first present at or after the requested frame, and the state-dump
  framebuffer is native 256 wide. They are not reliable evidence of the gate's
  per-frame width. The interactive build is the authoritative test.
- 4:3 simulation is unchanged; only the host gate and conditional clamp changed.
  Fixed-frame hash differences between builds are capture-timing artifacts.

## Interactive test

```sh
cd <workspace>/work/ws-discriminator/project/UniracersSNESRecomp/build
SNESRECOMP_WS=1 ./UniracersSNESRecomp
```

Expected:

- menus and the pause screen: 4:3 (256 wide);
- live race, including jumps: 16:9 (342 wide);
- no constant toggling.

If it still flickers, the next step is a short debounce (for example 10–15
consecutive frames before switching) rather than another state byte.

## Remaining risk

- `$00EE9`/`$00EEB` were verified as a pause flag on this revision; re-check if
  another mode shares it.
- The exact first/last frame of race transitions was not narrowed.
- Desktop window resizing during the width change is not measured headless.
- P-U1 (HDMA builder), P7/P8 (cull/OAM) and P9–P13 (spawn/staging) remain open.

## Recommendation

Confirm interactively with the command above. Once confirmed, archive this
change and continue with **`uniracers-ws-hdma`** (find the HDMA table builder and
extend P-U1), then `uniracers-ws-objects`.

## Artifacts

- Workspace: `work/ws-discriminator/`.
- Patch: `patches/live-race-gate.diff`.
- Dumps: `dumps/menu*.wram.bin`, `dumps/race*.wram.bin`,
  `dumps/pause*.wram.bin`, `dumps/jump_*.wram.bin`.
- Metrics: `metrics/combined-gate.txt`, `metrics/jump-test.txt`,
  `metrics/state-diff.txt`, `metrics/pause-flag-candidates.txt`.
- This report contains no generated C, no ROM bytes, and no game data.
