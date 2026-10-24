# Validation

Two automated checks cover the port: the output gate, which compares frames and logs against recorded references, and the settings round-trip test. A refactor must pass both unchanged; a change that is meant to alter the output re-records the references and says why.

## Output gate

```sh
game/tools/validate.sh            # compare against game/tools/validate_refs.txt
game/tools/validate.sh --record   # re-record the references (each case runs twice)
game/tools/validate.sh --case logs --case 16x9   # run only some cases
game/tools/validate.sh --build /path/to/UniracersSNESRecomp   # another binary
```

It needs the ROM at `rom/Uniracers (USA).sfc` and a built executable (default `game/build-local/UniracersSNESRecomp`).

**It never touches your data.** Each case runs a copy of the executable in a scratch directory with an empty `saves/` and a generated `config.ini` (widescreen off in the file, audio off, `DisableFrameDelay = 1`). A fingerprint of `game/build-local/config.ini` and `saves/save.srm` is taken before and after, and a change is reported as a warning. `VALIDATE_KEEP=1` keeps the scratch directory for inspection.

### Cases

All cases drive the game headless (`SDL_VIDEODRIVER=dummy`, `SNESRECOMP_NO_LAUNCHER=1`) through an input route and take a screenshot at a fixed frame.

| Case | Route | Frame | Settings |
|---|---|---|---|
| `native` | `game/tools/route-validate-dragster.txt` | 2800 | widescreen off |
| `16x9` | same | 2800 | widescreen, 16:9, `source` |
| `21x9` | same | 2800 | widescreen, 21:9 |
| `512` | same | 2800 | widescreen, 32:9 (capped at 512 px) |
| `clamp` | same | 2800 | 16:9, `SNESRECOMP_WS_OFFWINDOW=clamp` |
| `map-wrap` | same | 2800 | 16:9, `OFFWINDOW=wrap`; must equal `clamp` |
| `map-ring` | same | 2800 | 16:9, `OFFWINDOW=ring`; must equal `16x9` |
| `monster` | `tools/scripts/ws-leading-edge/route-monster-move.txt` | 2300 | 16:9 |
| `zoomzoo512` | `tools/scripts/ws-native-map-spike/route-zoomzoo-move.txt` | 3505 | 32:9 |
| `logs` | `game/tools/route-validate-dragster.txt` | 2800 | 16:9 `source`, with the source, offwindow, park and radar logs and the BG probe on frames 2600–2800 |
| `2p-native` | `game/tools/route-validate-2p-looper.txt` | 5000 | widescreen off |
| `2p-16x9` | same | 5000 | widescreen, 16:9 |
| `2p-21x9` | same | 5000 | widescreen, 21:9 |
| `2p-512` | same | 5000 | widescreen, 32:9 (capped at 512 px) |
| `2p-clamp` | same | 5000 | 16:9, `SNESRECOMP_WS_OFFWINDOW=clamp` |
| `vs-16x9` | `game/tools/route-validate-vs-looper.txt` | 5000 | widescreen, 16:9 |

The `2p-native` reference was re-recorded when the per-line OAM size fix landed: the bottom half now shows its rider in 4:3 as well. The 1P references were not re-recorded and are unchanged.

Each case records the SHA-256 of the screenshot. The `logs` case also records the hash of each log (with the wall-clock `search_ms` field normalised) and a **wrong-cell score**: `tools/scripts/ws-offwindow/world_key_check.py` checks the BG probe capture against the offwindow log, and the score hashes the counts of conflicting cells and of wrong cells in each margin, which must stay zero.

For 2P, score a BG probe capture with `world_key_check.py --bottom-line 114 --served`. The option splits the rows at the first bottom-half line (114), unwraps each half's scroll independently and skips the two divider lines. It prints `viewport 0`/`viewport 1` conflicting-cell counts and `margin vp0`/`vp1` wrong-cell counts. The measured 16:9 runs on Dragster, Zoom Zoo, Bowl, Switcher, Monster and Looper report zero wrong served cells in both halves; see [`TWO_PLAYER.md`](TWO_PLAYER.md).

### References and determinism

`game/tools/validate_refs.txt` holds one line per case and artifact. `--record` runs each case twice; an artifact that differs between the two runs is stored as `NONDET` and then skipped, except that a non-deterministic frame falls back to the wrong-cell score.

On the current reference host every case is deterministic and compared bit-exactly. Earlier references were recorded on a host where the headless menu timing varied and every frame came out `NONDET`; if that happens again, try `VALIDATE_FRAME_DELAY=0` (normal pacing instead of `DisableFrameDelay = 1`), which made runs deterministic during the native-map work. Two other rules from that work still hold for any capture made by hand: start from an empty `saves/` (persistent SRAM changes the starting state), and compare captures from the same run rather than across runs.

The Dragster route waits for the real race state (`$0300 == 1`) before accelerating, so it survives variable menu and attract timing.

## Settings round trip

```sh
sh tools/scripts/test_ws_settings_roundtrip.sh
```

Builds `game/src/ws_settings.c` with a small harness (`tools/scripts/ws_settings_roundtrip_test.c`) and checks, in a scratch directory: the precedence of environment, `config.local.ini`, `config.ini` and defaults; locking; in-place writing (comments, unknown keys and other sections untouched, missing sections created); range clamping; the legacy clamp variable; and the mapping of removed margin values.

## Other checks

- `tools/scripts/ws-source/validate_all.sh [source|clamp]` scores a margin policy on the six leading-edge captures (Dragster both ways, Zoom Zoo, Bowl, Switcher, Monster); output goes to `ws-source/` under the git-ignored `work` directory.
- `tools/scripts/docs/check_links.py` and `tools/scripts/docs/check_switches.sh` check that the documentation links resolve and that every environment variable the port reads is documented in [`CONFIG.md`](CONFIG.md).
