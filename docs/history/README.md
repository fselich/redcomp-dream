# History (obsolete, temporary)

The files in this folder are the investigation logs, handoffs and the old roadmap written while the port was being built. They are **obsolete**: they are not maintained, they describe experiments, switches and margin policies that no longer exist, and they contradict the current code in places (several end with "clamp remains" or list a blocker that was solved later).

They are kept **only temporarily**, to be read once more before they are deleted. Nothing outside this folder links here, so the whole folder can be removed at any time without breaking a link.

For the current documentation start at [`docs/README.md`](../README.md). The durable evidence of each phase is in its archived OpenSpec change (`openspec/changes/archive/<date>-<name>/report.md`).

| File | What it was |
|---|---|
| `UNIRACERS_WIDESCREEN_TRACK_FILL.md` | first widescreen track-fill investigation: clamp, edge fill, generator probes, margin scan |
| `UNIRACERS_NATIVE_MAP_SPIKE.md` | native-map spike: level tables, the BG1 uploader, the no-go on VRAM stitching |
| `UNIRACERS_NATIVE_MAP_HANDOFF.md` | handoff written during the BG1 off-window work |
| `UNIRACERS_BG1_RING_FINDINGS.md` | ring formula, leading-edge prototype, ghost lane, map decode and the source mode |
| `UNIRACERS_WS_PARKED_SPRITES.md` | parked OAM entries and the right-edge sliver at 512 px |
| `UNIRACERS_MEJORAS.md` | roadmap in Spanish (superseded by `docs/ROADMAP.md`) |
| `workshop/` | notes copied from the local, git-ignored workshop directory |
