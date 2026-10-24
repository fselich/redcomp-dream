# Tools

Scripts, routes and patches used to develop and check the port. Raw captures, frames, logs and dumps stay outside the repository because they are derived from the ROM; scripts that write files put them in a scratch directory or in the git-ignored work directory at the repository root.

| Path | Contents |
|---|---|
| `scripts/save/srm.py` | inspect (`dump`, `verify`) or unlock (`unlock IN OUT`) a battery save |
| `scripts/test_ws_settings_roundtrip.sh`, `scripts/ws_settings_roundtrip_test.c` | builds and runs the settings model round-trip test (precedence, writer, margin value mapping) |
| `scripts/ws-offwindow/world_key_check.py` | scores a BG pixel-source probe capture for wrong BG1 margin cells; used by `game/tools/validate.sh` |
| `scripts/ws-source/validate_all.sh` | runs a margin policy over the six leading-edge captures and scores them |
| `scripts/ws-map-decode/` | RNC unpacker and `$7F` track table tools (directory scan, cell lookup, shape and track rendering, probe comparison) |
| `scripts/ws-native-map-spike/dbg.py` | line-oriented client for the framework debug server (`SNESRECOMP_DEBUG_PORT`); the other debug scripts import it from here |
| `scripts/ws-native-map-spike/capture_frame.py`, `capture_wram_series.py` | capture a historical frame (WRAM/VRAM) or a WRAM time series through the debug server |
| `scripts/ws-native-map-spike/route-*.txt` | deterministic menu-to-race input routes for Dragster and Zoom Zoo; `route-zoomzoo-move.txt` is a gate case |
| `scripts/ws-leading-edge/route-*.txt` | input routes for Bowl, Monster, Switcher and Dragster (move, dump, left); `route-monster-move.txt` is a gate case |
| `scripts/ws-leading-edge/dis.py` | linear 65816 disassembler using the framework's recompiler decoder |
| `scripts/ws-ghost-offscreen/` | records rival/player OAM and camera series (`run.sh`, `rec.py`) and summarises them (`ana.py`); the JSON files are its capture specs |
| `scripts/ws-ghost-xhigh/gate/` | debug-server grab and monitor scripts for the rival OAM X-high check |
| `scripts/ws-radar/route-dragster-radar.txt` | route that reaches a race with the radar visible |
| `scripts/docs/check_links.py` (`--without-history`), `scripts/docs/check_switches.sh` | documentation checks: every link and repository path in the live docs and code comments resolves (and none points into the temporary history folder or the local work directory); every `SNESRECOMP_*` variable read in `game/src` is documented in `docs/CONFIG.md` |
| `patches/research/read-watch.diff` | archived `$7F` read watch for the framework CPU core; see its header for use |

The routes assume the build is launched with the ROM path supplied on the command line, as `game/tools/validate.sh` does. "Ghost" in `ws-ghost-offscreen` and `ws-ghost-xhigh` refers to the rival rider sprite, not to the removed ghost-lane margin policy.
