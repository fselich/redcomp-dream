## 1. Measurement

- [x] 1.1 Record the hold-B route source log: seed `(72,-14)` at 2599; drop at 2841 (15/32); seed re-converges at 3113
- [x] 1.2 Confirm the anchor is still `(72,-14)` after the stunt; the disagreement is the airborne BG1 rows, not a wrong anchor
- [x] 1.3 Confirm the no-jump transient is only ~19 frames (3093..3114) and is covered by the 30-frame grace; the 273-frame stunt is not
- [x] 1.4 Identify the airborne state: `$7E:0BE9` is 1 for the stunt (2841..3110) and the no-jump gap (~3108), 0 on the ground

## 2. Self-check hold

- [x] 2.1 Add `kRamAirborne` (`$7E:0BE9`) and the airborne hold branch in `ws_source.c`
- [x] 2.2 Hold/serve while airborne; keep the 30-frame ground grace/drop; reset on pass/new anchor/scene reset; scene reset/non-live still immediate
- [x] 2.3 The held anchor keeps serving the table; the source log shows `hold air` instead of `dropped`

## 3. Validation

- [x] 3.1 Replace `route-validate-hopper-jump.txt` with the hold-B-at-1s route and move the `hopper-jump` case to 21:9 frame 3000
- [x] 3.2 Record refs twice; no NONDET; the source log has `hold air` and the off-window log has zero masked race cells
- [x] 3.3 Confirm the frame: no-hold track ends at the 4:3 edge (`x≈354` of 448), the fixed frame reaches `x=447`
- [x] 3.4 Run the full gate (19 cases); 1P/2P frames and the Dragster source/park refs unchanged

## 4. Docs, report and archive

- [x] 4.1 Update `docs/TRACK_MARGIN.md` and `docs/VALIDATION.md` with the airborne hold and the hold-B case
- [x] 4.2 Write `report.md` with the stunt/no-jump logs, the frame-3000 pixel measurement, the gate and the decision
- [x] 4.3 Archive the change and commit (confirmed on the real 1P build)
