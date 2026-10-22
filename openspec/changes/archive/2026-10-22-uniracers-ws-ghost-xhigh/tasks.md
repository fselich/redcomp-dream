## 1. Setup

- [x] 1.1 Create `work/ws-ghost-xhigh/{patches,logs,frames,scripts}`. Save `src/game_rtl.c` as `patches/game_rtl.c.before`. Work only in the tree `<workshop>/work/ws-native-map-spike/project/UniracersSNESRecomp/`: edit `src/game_rtl.c`, build in `build-local/` (`cmake --build build-local`). Do **not** use `work/ws-offwindow/project` or `work/ws-map-decode/project`; they are stale standalone copies without the `source`/ultrawide code.
- [x] 1.2 Reproduce the defect with `work/ws-ghost-offscreen/scripts/run.sh` (Dragster idle route). Confirm frames 1740–1742: slot 99 low byte 2/16/30, high bit 0, true X 258/272/286. Capture `frames/before-1742.png`: phantom left of the player.

## 2. Implementation

- [x] 2.1 Add `WsFixRiderOamXHigh(Ppu *)` in `src/game_rtl.c` (D2): slots 98 (`$0411`) and 99 (`$0413`), camera `$0419`, zoom `$03ED`, `SLACK = 32`. Only set the high bit in `ppu->highOam`; never write WRAM.
- [x] 2.2 Gate it (D3): `WideEnabled() && InLiveRace() && g_ram[0x0DDB] == 0 && WsCullEnabled() && WsExtra() > 0`, plus the `SNESRECOMP_WS_OAM_XHIGH` switch (default on, `0` off).
- [x] 2.3 Call it from `GameDrawPpuFrame` right after `WsInjectTestOam`.
- [x] 2.4 Optional `SNESRECOMP_WS_OAM_XHIGH_LOG` CSV (`frame,slot,lo,want`) (D4).

## 3. Verification

- [x] 3.1 Dragster idle route: no rider pixels left of the player in frames 1740–1742. The rival is drawn at wide x ≈ 301–329 (true X + 43). Save `frames/after-174x.png` and the CSV log. *(`after-1742.png`/`after-1744.png`; log fixes 1742–1744; before/after diff in x<43 is 0.)*
- [x] 3.2 Dragster moving route (`route-dragster-move.txt`) and Zoom Zoo (`route-zoomzoo.txt` idle + `route-zoomzoo-move.txt`): scan the log for fixes, and check visually that there is no left phantom when the rival is just ahead. Record the `$03ED` values seen. *(Dragster move: 65 fixes, `$03ED=0`. Zoom Zoo idle/move 1400–3500: no fixes, `$03ED=2`; visual no phantom.)*
- [x] 3.3 Measure the lag: for every logged fix, `|lo + 256 − want|`. Report the max; it must be < `SLACK`. *(Idle max 14, moving max 6, <32.)*
- [x] 3.4 Left margin unchanged: frames where slot 99 X < 0 are byte-identical to the pre-change build. *(Zoom Zoo 1817 fix on/off byte-identical; Dragster 1742 diff has no x<43 pixels.)*
- [x] 3.5 `SNESRECOMP_WS_OAM_XHIGH=0`: Dragster 2500 SHA-256 equals the pre-change 16:9 default (`4b138431…`). Native WS-off hash is unchanged (`3417f91a…`). *(`4b138431bdc0…`, `3417f91afc8f…`.)*
- [x] 3.6 WRAM trace for 2,500 frames with the fix on vs off is identical: the fix is presentation-only.
- [x] 3.7 21:9 and 512 px: the rival is visible in the right margin up to 256 + E. No parked sprite is visible. *(21:9 = 448×224 E=96; 512×224 E=128 via `SNESRECOMP_WS_ASPECT=8:3`; 9 fixes, no parked sprite at 1756.)*

## 4. Report

- [x] 4.1 Write `report.md` (cause, fix, evidence, lag, switches). Save `work/ws-ghost-xhigh/patches/ws-ghost-xhigh.diff`.
- [x] 4.2 Update `docs/UNIRACERS_NATIVE_MAP_HANDOFF.md` and `work/README.md` with the new switch and work dir.

## 5. Zoom regression fix (2026-10-21)

- [x] 5.1 `WsFixRiderOamXHigh`: `want = rel` (unshifted; OAM X is taken before the `$03ED` shift). Removes the 21:9 Zoom Zoo player vanish (1,038 bad slot-98 fixes before).
- [x] 5.2 `WsApplyCull`: widen by `E << $03ED` (Zoom Zoo cull units are ×4 px).
- [x] 5.3 Re-validate: Zoom Zoo move 21:9/512 → 0 fixes, player frame 1825 identical to fix-off; Zoom Zoo idle 21:9 → 7 rival fixes, 0 slot 98; Dragster idle 16:9 → 3, move 21:9 → 65, slot 98 never; Dragster 2500 16:9 hash `4b138431bdc04382` unchanged. Evidence `work/ws-ghost-xhigh/regress-zoom/fix-*`.

## 6. Parked-sprite sliver on ultrawide (2026-10-21)

- [x] 6.1 Reproduce: at 2.39:1 (458 px, E ≈ 101) Zoom Zoo move frame 1825 OBJ-only shows the parked rival (`X=$170` → −144, `Y=112`, large) at x 0–4, y 132–147. Not visible at 16:9 / 21:9.
- [x] 6.2 `WsFixRiderOamXHigh`: when a slot has the parking signature (`lo=$70`, `Y=$70`, high bit) and the rider's WRAM screen X/Y is not within 4 px of (−144, 112), move it to `Y=$E0` as a small sprite in the PPU copy (log `lo=-1`).
- [x] 6.3 Verify: 2.39 and 512 px frame 1825, no OBJ pixels in either outer 60 px; 1820/1830 clean, 1819 shows only the real rival at the edge (last frame before cull). Dragster 16:9 2500 hash `4b138431bdc04382`; Zoom Zoo 16:9 fix on == off; Dragster move 21:9: 65 high-bit fixes, 0 slot 98.

## 7. Race gate on later tours (2026-10-22)

- [x] 7.1 Diagnose: playtest showed widescreen only on tour 1. Live RAM monitor (`work/ws-ghost-xhigh/gate/monitor.log`, debug port 4377): in race `$0B4` = `01` on tour 1 but `0B` on a later tour; `$0EE9 = 0` and `$0300 ∈ {0,1}` in both. The gate required `$0B4 == 1`.
- [x] 7.2 `InLiveRace()` in `src/main.c` and `src/game_rtl.c`: `$0B4 != 0 && $0EE9 == 0 && $0300 <= 1`. `$0300` keeps excluding menus (`4C`, `E2`, `0C04`…); `$0B4 != 0` excludes boot.
- [x] 7.3 Cull scale clamp raised to `$03ED ≤ 6` (seen 0, 2, 4; the later-tour track uses 4, base `[-784, 4096]` = `[-49, 256]` ×16). int16 stays safe up to 6 with E ≤ 128.
- [x] 7.4 Tour 1 unchanged: Dragster 2500 16:9 `4b138431bdc04382`, native (config `Widescreen = 0`) `3417f91afc8f6d47`. Later-tour race confirmed by the user in play (no scripted route to later tours yet).

## 8. BG1 `source` margins on a tour-2 track (2026-10-22)

- [x] 8.1 Diagnose: user source log on tour 2 track 1 (`work/ws-source/xwrap/src-user-tour2-before.log`). The anchor drops at the moments the user saw the cut (≈2, 9, 34 s), and the good anchor re-appears at `ax = 95 ± 256k` (95, −161, 351, −673). Grid 256×1024 cells: the race crosses the table's X edge, and `WsSrcCell` returned 0 there, so the self-check failed and the margins were masked for up to ~120 frames.
- [x] 8.2 `WsSrcCell` wraps `tx` modulo the grid width; `WsSrcSearch` stores `ax` modulo the width (one canonical anchor). Y keeps "outside → 0".
- [x] 8.3 Regression: `work/ws-source/scripts/validate_all.sh source` on the six tour-1 captures: 0 wrong, the same masked counts (`work/ws-source/xwrap/validation-after.txt`).
- [x] 8.4 User replay of tour 2 track 1 with `SNESRECOMP_WS_SOURCE_LOG`: no recurring `dropped: self-check 30/32` at the X edge and no left cut. *(`src-user-tour2-after.log`: one `anchor ok ax=95` at frame 993 and no drop for the rest of the session; before: ~40 drops/re-searches.)*
