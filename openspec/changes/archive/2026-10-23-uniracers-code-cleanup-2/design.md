## Context

The port draws the 16:9+ BG1 margins with one of five policies chosen in `ws_ring.c`:

| Policy | What it does | Fate |
|---|---|---|
| `wrap` | no hook; the wide renderer samples the raw 512 px ring, so the leading edge shows cells written a ring earlier | removed: a renderer artifact, not the game's look |
| `ring` | serve margin cells verified in the native view, mask the rest | removed as a mode; its rule is the first step of `source` |
| `source` | `ring`, then the unpacked `$7F` track table at a searched anchor, then mask | kept, default |
| `ghost` | `ring`, then cells harvested from a run-ahead machine | removed: too slow, environment-only, never the default |
| `clamp` | `PpuSetWidescreenLayerClamp(BG1)`: native 256 px, margins show the backdrop | kept, fallback |

`ghost` costs most: 409 lines, snapshot/restore of the machine and host render state, cull-state accessors and two framework hooks that exist only for it. The framework patch also carries two research probes (a `$7F` read watch in `cpu_state.c`, called on every guest RAM read, and a BG pixel-source probe in the BG rasterizer) that solved the ring formula and the track format and are no longer needed in the build.

Constraint from `recomp-code-structure`: refactors preserve output, checked by `game/tools/validate.sh`.

## Goals / Non-Goals

**Goals:**

- Two margin policies, one module that owns them, no code that exists only for removed policies.
- A framework patch that carries only what the port calls in normal play.
- `source`, `clamp`, native, 21:9 and 512 px output bit-identical to the references recorded before the change.
- Research experiments still reproducible from archived patches.

**Non-Goals:**

- Changing how `source` searches or verifies its anchor.
- Rewriting the documentation (that is `uniracers-docs-overhaul`).
- Touching the HUD, OAM, cull or radar behaviour.
- Renaming the menu labels.

## Decisions

### D1. Legacy margin values are mapped, not rejected

| Value in `config.ini` or `SNESRECOMP_WS_OFFWINDOW` | Resolves to | Reason |
|---|---|---|
| `source` | `source` | |
| `clamp`, `SNESRECOMP_WS_TRACK_CLAMP` set | `clamp` | |
| `ring`, `ghost` | `source` | both meant "fill with verified cells"; `source` is the superset |
| `wrap`, `0`, `off` | `clamp` | they meant "no fill"; with the raw ring gone the native width is the honest equivalent |
| anything else, empty | `source` | the default, as today |

The mapping lives in `Canonicalize` in `ws_settings.c`, so the menu and the writer only ever see the two canonical values. A mapped value read from `config.ini` is not rewritten unless the user saves from the menu. The `allow_ghost` parameter disappears.

Alternative considered: reject unknown values with a warning. Rejected because a user's `config.ini` would silently change meaning on upgrade with no visible reason.

### D2. Module layout for the margin

```
before                                   after
──────                                   ─────
game_rtl.c   policy → hooks install      game_rtl.c   WsMarginFrameBegin / End
ws_ring.c    policy parse, seen table,   ws_margin.c  policy, hook install, tile hook,
             scroll unwrap, line hook,                clamp, scene reset, mask word
             tile hook, ghost dispatch   ws_ring.c    seen table, scroll unwrap,
ws_source.c  $7F table + anchor                       line observation (no policy)
ws_ghost_lane.c run-ahead lane           ws_source.c  $7F table + anchor (unchanged logic)
```

`GameDrawPpuFrame` calls `WsMarginFrameBegin(&ctx)` before the draw and `WsMarginFrameEnd()` after; the margin module decides whether to clamp BG1 or install the line and tile hooks. Names: `WsMarginPolicy()`, `WS_MARGIN_SOURCE`, `WS_MARGIN_CLAMP`, `WsMarginReset()` (today `WsOwReset`, called by `WsSettingsOnChange`). The ring keeps the `WsRing*` prefix.

`WsRingWalkLine` loses its callback: with one caller it writes the seen table directly.

Alternative considered: keep everything in `ws_ring.c` and only delete branches. Rejected: the policy would stay split between `game_rtl.c` and a module named after one of its inputs.

### D3. Framework patch scope

Kept in the patch: `g_snes_beam_line_hook`, `g_ppu_margin_tile_hook`, the widescreen layer clamp/elastic band API, the title-menu hook and the display/volume setters in `host_main`, the overlay draw change, and the BG pixel-source probe (`SNESRECOMP_BGPROBE_*`).

Removed: `snes_beam_frame_driver_get_exec`/`set_exec`, `ppu_rasterStateSize`/`Save`/`Load`, `read_watch_note` and its calls.

The BG probe stays because `validate.sh` uses it in the `logs` case: `world_key_check.py` scores the probe capture for wrong margin cells, the only check that says whether `source` serves correct cells rather than merely the same cells as before. It costs one branch per drawn span when off. Alternatives considered: archive it and lose that check, or build a second validation binary with the probe applied (slower, more moving parts). Both rejected.

The read watch is saved as `tools/patches/research/read-watch.diff`, generated against the cleaned framework tree, with a header comment naming the experiments that used it and how to apply it. `game/patches-snesrecomp-local.diff` is regenerated from the working tree.

### D4. Dead code removed in passing

`WsConfigBool` (no caller), `WsSourceAnchorCount` (no caller), `s_src_served`/`s_src_check_fail`/`s_src_anchors` and `WsSourceNoteServed` (written, never read), `WsRingSceneValid`/`MapBase`/`PrevBgMode`/`ScrollSeed` (ghost only), `WsCullGet`/`WsCullSet` and `WsCullState` (ghost only), `WsEnvInt` (ghost only after removal), the private `Trim` in `ws_settings.c` (duplicate of `WsTrim`). The `[ws-ow]` stderr lines and the `SNESRECOMP_WS_OFFWINDOW_LOG` CSV keep their format, so the log references stay valid; only the C names change.

### D5. Validation references

The committed references were recorded on a host where the headless menu timing was not deterministic, so every frame and log was `NONDET` and only the wrong-cell score was compared. On the starting commit (`05963f1`) three runs of each case now give identical hashes, so the references are re-recorded once there, before any change, and every later step compares bit-exactly against them.

Drop the `wrap`, `ring` and `ghost` rows from `validate.sh` and `validate_refs.txt`. All other rows (native, 16:9 source, 21:9, 512 px, clamp, Monster, Zoom Zoo, the source/offwindow/park/radar/bgprobe logs, the score) must match those references without re-recording. A new case checks the mapping: `SNESRECOMP_WS_OFFWINDOW=wrap` must hash equal to `clamp`, and `=ring` equal to `source`.

### D6. `tools/` pruning rule

Keep a file when it is used by the validation gate, by a documented run command, or by a tool we expect to use for the next changes (track decode for the radar and rival indicator, frame capture, routes, save tool, settings round-trip test, `ws-source/validate_all.sh`, `world_key_check.py`). Delete per-phase patches already merged into the code (`tools/patches/ws-*`, including `*.before` and `ppu.c.orig`), captured `.log` files, and scripts that only drive removed policies (`ws-ghost-lane/`, `ws-ghost-offscreen/`, the ghost parts of `ws-leading-edge/`). Git history keeps all of it; the report records the last commit that still has it. `tools/README.md` is updated to the kept set.

### D7. Order of work

1. Record nothing new; confirm the gate passes on the starting commit.
2. Remove the ghost lane and its framework hooks; gate.
3. Remove the research probes from the framework, archive them; gate.
4. Collapse the policies to two with the mapping; drop the three gate rows, add the mapping case; gate.
5. Move to the margin module and rename; gate.
6. Dead code, comments, `tools/`; gate.

Each step is a commit, so a gate failure points at one step.

### D8. Cost baseline for `source`

`recomp-ws-source` measured `source` against `ring` (< 0.2 ms per frame), which isolated the table cost. With `ring` gone the only baseline is `clamp`, which also skips the ring observation, so the old figure does not carry over. The new budget is < 0.5 ms per frame over `clamp` (3 % of a 60 Hz frame); it is a chosen ceiling, not a measurement, and the report records the measured value.

## Risks / Trade-offs

- [A user relies on `wrap` for the old look] → mapped to `clamp`; the old look is a known-wrong image, documented in the history.
- [The rival-indicator change edits the same files] → implement the two changes sequentially; whichever lands second rebases.
- [Deleting tools someone wanted] → git history keeps them; the report names the commit.
- [Archived probe patches rot as the framework moves] → they are reference material for past experiments, not maintained tools; the header says so.

## Migration Plan

No data migration. `config.ini` values are mapped on read (D1). Rollback is reverting the commits; the validation references are unchanged, so either side of the change can be checked with the same gate.

## Open Questions

None blocking. Whether the menu labels move to English is left to a later change.
