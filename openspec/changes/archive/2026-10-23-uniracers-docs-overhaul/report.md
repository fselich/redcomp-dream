# uniracers-docs-overhaul — report

## Result

`docs/` is a current, English documentation set that describes the code after `uniracers-code-cleanup-2`. The superseded logs are in `docs/history/`, unchanged and unlinked. The root, `game/` and `tools/` READMEs were rewritten, and every spec in `openspec/specs/` has a real Purpose.

## Old to new

| Old file | Durable content now in | File itself |
|---|---|---|
| `docs/UNIRACERS_TRACK_FORMAT.md` | `docs/formats/TRACK_FORMAT.md` (tools point at `tools/scripts/ws-map-decode/`, runtime section rewritten, no `work/` paths) | moved and edited |
| `docs/UNIRACERS_SAVE_FORMAT.md` | `docs/formats/SAVE_FORMAT.md` (unlock section tied to `uniracers_save.c`, no `work/` paths) | moved and edited |
| `docs/UNIRACERS_BG1_RING_FINDINGS.md` | §1, §3, §2.6 → `docs/formats/BG1_RING.md`; §8–§11 → `docs/TRACK_MARGIN.md` (source algorithm, measured results, rejected `ring`, VRAM bracket, widened builder, ghost lane) | `docs/history/` |
| `docs/UNIRACERS_WIDESCREEN_TRACK_FILL.md` | clamp → `TRACK_MARGIN.md` "clamp"; edge fill, synthetic generator call → rejected approaches | `docs/history/` |
| `docs/UNIRACERS_NATIVE_MAP_SPIKE.md` | VRAM stitching, plain tilemap copy, edge repeat → rejected approaches | `docs/history/` |
| `docs/UNIRACERS_NATIVE_MAP_HANDOFF.md` | determinism rules → `docs/VALIDATION.md`; "do not repeat" list → rejected approaches | `docs/history/` |
| `docs/UNIRACERS_WS_PARKED_SPRITES.md` | OAM window, parked pair, sentinel → `docs/WIDESCREEN.md` "Riders"; debug variables → `docs/CONFIG.md` | `docs/history/` |
| `docs/UNIRACERS_MEJORAS.md` | open items → `docs/ROADMAP.md` in English; finished ones (validate.sh, scratch-copy tests, config options, menu, module split, tour unlock) dropped | `docs/history/` |
| `docs/workshop/**` | nothing new beyond the reports and formats | `docs/history/workshop/` |

New documents: `docs/README.md`, `ARCHITECTURE.md`, `WIDESCREEN.md`, `TRACK_MARGIN.md`, `CONFIG.md`, `VALIDATION.md`, `ROADMAP.md`, `formats/BG1_RING.md`, `docs/history/README.md`.

## Checks

- `tools/scripts/docs/check_links.py`: 0 problems. With `--without-history`: 0. On a copy of the repository without `docs/history/`: 0.
- `tools/scripts/docs/check_switches.sh`: the 16 `SNESRECOMP_*` variables read in `game/src` are all in `docs/CONFIG.md`, and every variable named in `CONFIG.md` and `README.md` is read in `game/src` or listed as a framework variable (checked present in the framework source).
- `openspec validate --specs --strict`: 26 passed. No spec contains the archive placeholder any more.
- Live docs: `ring`, `wrap` and `ghost` appear as margin values only in the rejected-approaches table and in the mapping notes; "ghost" otherwise only in path names (`ws-ghost-xhigh`, rival sprite) and the best-lap ghost idea. No Spanish prose except the quoted menu labels.
- Every rejected-approach reason and number in `TRACK_MARGIN.md` was checked against the cited archived report. Two drafts were corrected: the ghost-lane cost is +2.3 ms mean (+1.9–2.9 ms end to end), and the plain-tilemap-copy evidence is the native-map spike (multi-segment row assembly), not the track-fill report.

## Facts corrected or found while writing

- The cull window is `$0421`/`$0423` (a draft had other addresses).
- Netplay is not built into this target (`game/CMakeLists.txt`); the old README and roadmap implied it was available. The docs now say so.
- The radar needs the source anchor, so it does not appear with `Margin = clamp`. Documented in `WIDESCREEN.md` and `CONFIG.md`.
- `run.sh` changes to `game/build-local/` before starting the game, so a relative `--script` path resolves against that directory. The old README example used a relative path; the README and the `run.sh` comment now use `"$PWD/..."`. `run.sh` itself was not changed (not tested, since a run uses the player's save).
- `game/recomp/README.md` cited `tools/regen.sh` relative to `game/`; it now says `game/tools/regen.sh`.
- Code comments that cited `docs/UNIRACERS_*_FORMAT.md` now cite `docs/formats/`; a separator line in `ws_radar.c` was shortened to the file's width.

## Scope adjustments

- The link check excludes `openspec/` as well as `docs/history/`: changes and specs are records of their time (some spike requirements name `work/` directories, active changes name files that do not exist yet). The spec and design of this change were updated to define live documents that way.
- `game/recomp/README.md` was not in the task list; it was fixed because the link check found it.

## Left for the user

- `docs/history/` can be deleted whenever you have read it; the link check confirms nothing depends on it.
- `maps/` has no provenance note. The README now calls them third-party reference maps without naming a source; if the source is known, it should be recorded.
- Some spec Purposes for finished spikes were written from their requirement titles and the archived proposals; they are short by design.
- No commits were made.
