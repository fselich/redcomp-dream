## 1. Implementation

- [x] 1.1 Save `src/game_rtl.c` as `work/ws-source-default/patches/game_rtl.c.before`.
- [x] 1.2 Rework `WsOffwindowMode()` to the D1 table (default `source`, new `clamp` = 4, `wrap`/`off`/`0` = 0, legacy `SNESRECOMP_WS_TRACK_CLAMP`).
- [x] 1.3 In `GameDrawPpuFrame`, apply the global BG1 clamp for mode 4; keep repeat for mode 0 only; install the margin hooks for modes 1–3 only.
- [x] 1.4 Gate the `[ws-ow]` stderr lines behind the log switches (D2).

## 2. Verification

- [x] 2.1 Native 4:3 hash unchanged.
- [x] 2.2 Default run (only `SNESRECOMP_WS=1`): probe-scored Dragster 2300–2500 equals the `source` result (0 wrong, 0 masked).
- [x] 2.3 `clamp`, legacy `SNESRECOMP_WS_TRACK_CLAMP`, `wrap` and `ring` produce their previous frames (Dragster 2500 captures; `ring`/`source` agree with the `uniracers-ws-source` captures). *(`clamp` = legacy switch, bit-identical; default = explicit `source` = the `uniracers-ws-source` capture; `ring` differs (leading side masked) — no earlier `ring` capture existed to compare bit-for-bit; `wrap` = `source` on Dragster because its strip is periodic, different on Monster 2300.)*
- [x] 2.4 Default run prints no `[ws-ow]` lines.

## 3. Docs

- [x] 3.1 Write `report.md`; save `work/ws-source-default/patches/ws-source-default.diff`.
- [x] 3.2 Update the handoff, `docs/UNIRACERS_TRACK_FORMAT.md` §8 and `work/README.md` with the default and the switch table; note that the older "clamp is the default" statements were wrong for this tree.
