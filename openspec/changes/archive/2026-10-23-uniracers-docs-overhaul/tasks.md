## 1. Preconditions and tooling

- [x] 1.1 Confirm `uniracers-code-cleanup-2` is implemented and archived; read its report for the final module and switch names
- [x] 1.2 Write the link check (`tools/scripts/docs/check_links.py`): collect relative Markdown links and backticked repo paths from live documents and code comments, report missing targets and targets under `docs/history/` or `work/`
- [x] 1.3 Write the switch check (`tools/scripts/docs/check_switches.sh`): compare `SNESRECOMP_*` names in `game/src` against `docs/CONFIG.md` and `README.md` both ways

## 2. Move the logs to history

- [x] 2.1 `git mv` the logs of the design Context table and `docs/workshop/` into `docs/history/`, unchanged
- [x] 2.2 Write `docs/history/README.md`: obsolete, not maintained, may contradict the code, temporary; points to `docs/README.md`
- [x] 2.3 Before writing new docs, list per history file the durable content to extract (design Context table) and tick it off as it lands

## 3. Reference formats

- [x] 3.1 `docs/formats/TRACK_FORMAT.md` from the current track format doc: drop `work/` paths (point to `tools/scripts/ws-map-decode/`), update the runtime section to the margin module and radar
- [x] 3.2 `docs/formats/SAVE_FORMAT.md` from the current save doc; check field claims against `uniracers_save.c` and `tools/scripts/save/srm.py`
- [x] 3.3 `docs/formats/BG1_RING.md`: the ring model and the validated formula from the ring findings (§1, §3, §4), with the evidence link to the archived `uniracers-ws-offwindow` report

## 4. Main documents

- [x] 4.1 `docs/ARCHITECTURE.md` (design D2), with the module table checked against `game/src` and the hook list checked against `game/patches-snesrecomp-local.diff`
- [x] 4.2 `docs/WIDESCREEN.md` (design D2), absorbing the parked-sprites note
- [x] 4.3 `docs/TRACK_MARGIN.md`: problem, `source` algorithm, `clamp`, rejected approaches (design D3); verify each reason and evidence path against the archived reports
- [x] 4.4 `docs/CONFIG.md`: options table from `ws_settings.c`, precedence, menu controls, locked options, user and debug environment variables; run the switch check
- [x] 4.5 `docs/VALIDATION.md`: the gate, references, `--record`, non-deterministic cases, capture determinism rules (from the handoff §4), the settings round-trip test
- [x] 4.6 `docs/ROADMAP.md`: open items from `UNIRACERS_MEJORAS.md` in English, finished ones dropped, each with effort and a one-line goal
- [x] 4.7 `docs/README.md`: index of the above and a current-state summary; one plain sentence (no link) saying a temporary history folder exists

## 5. Entry points

- [x] 5.1 Rewrite the root `README.md` (design D1): shorter, user-first, options summarised with a link to `docs/CONFIG.md`, docs index
- [x] 5.2 Rewrite `game/README.md` for the `game/` directory; remove the scaffold text
- [x] 5.3 Check `tools/README.md` against the kept tool set; add the two docs check scripts

## 6. Specs

- [x] 6.1 Fill the Purpose of every `openspec/specs/*/spec.md` (design D7)

## 7. Checks and close

- [x] 7.1 Run the link check; fix until clean
- [x] 7.2 Run the link check with `docs/history/` temporarily removed (scratch copy); it must stay clean
- [x] 7.3 Run the switch check; fix until clean
- [x] 7.4 Grep live docs for `ring`, `wrap`, `ghost` as margin values outside the rejected-approaches section, and for Spanish prose; fix what is found
- [x] 7.5 Write `report.md`: old-to-new file map, check results, anything left for the user to decide before deleting `docs/history/`
