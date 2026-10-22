## 1. Reproduce and capture

- [ ] 1.1 Create `work/ws-right-edge-ghost/` and record the launch setup: fullscreen ultrawide (or `SNESRECOMP_WS_ASPECT=32:9` / `8:3`) with `Widescreen = 1`, so the race frame is 512 px (`E = 128`).
- [ ] 1.2 Add the `SNESRECOMP_WS_PARK_LOG=<path>` diagnostic (D3) that logs every parking-signature candidate (`frame,slot,lo,y,hi,rel,rel_y,E,action`) before changing behaviour.
- [ ] 1.3 Run a route where a rider is culled and parked (`tools/scripts/ws-leading-edge/route-dragster-dump.txt` and a Zoom Zoo route) and capture the frame(s) with the right-edge sliver.
- [ ] 1.4 Confirm from the log and OAM dump: slot 98/99, raw X `$170`, Y `$70`, high bit set, `E = 128`. Record the first bad frame and image coordinates in the task notes.
- [ ] 1.5 If the sliver is not the parking sentinel, stop implementation, record the actual slot/raw entry, and split the zoomed-track case (`$03ED = 4`) into its own change.

## 2. Implementation

- [ ] 2.1 In `game/src/game_rtl.c` (`WsFixRiderOamXHigh`), make the parking-signature branch unconditional: move the slot to `Y = $E0`, clear its high bit in the PPU copy, and remove the WRAM `(-144, 112)` keep-exception.
- [ ] 2.2 Keep the real-rider high-bit path unchanged and ensure the parking branch runs first, so a high-bit-clear rider is never hidden (D2).
- [ ] 2.3 Keep the existing gates (`WideEnabled`, `InLiveRace`, 1P, cull enabled, `E > 0`, `SNESRECOMP_WS_OAM_XHIGH != 0`) and do not write guest WRAM.
- [ ] 2.4 Confirm `SNESRECOMP_WS_OAM_XHIGH_LOG` output and add a comment at the parking branch explaining the `E = 128` / `X = 368` decode.

## 3. Validation

- [ ] 3.1 Pre-change artifact: at 512 px, the OBJ-only layer shows the right-edge sliver on the reproduction frame; save `before-*.png` and the log excerpt.
- [ ] 3.2 Post-change: the same frame has no OBJ pixels in either outer margin and the parked slot is below the field; save `after-*.png`.
- [ ] 3.3 Real right-margin rider still draws at its true X (reuse the `ws-ghost-xhigh` right-margin route); no slot 98/99 disappearance.
- [ ] 3.4 21:9 (`ASPECT=21:9`, 448 px) and 16:9 (`ASPECT=16:9`, 342 px) frames are unchanged compared with the pre-change build except for removed parked slivers.
- [ ] 3.5 `SNESRECOMP_WS_OAM_XHIGH=0` restores the pre-change frames byte-for-byte; native 4:3 (WS off) frame hash is unchanged.
- [ ] 3.6 Guest state is unchanged: compare WRAM snapshots / frame hashes for the simulation path, not only the PPU output.

## 4. Wrap-up

- [ ] 4.1 Write `report.md` with the evidence, the confirmed root cause, the fix, switch behaviour, hashes and residual risks.
- [ ] 4.2 Update `docs/UNIRACERS_NATIVE_MAP_HANDOFF.md` and the relevant `openspec/specs/recomp-ws-objects` entry when the change is archived.
- [ ] 4.3 Run the project build and existing tests (`bash game/tools/regen.sh` only if generated sources need it; `cmake --build game/build-local --parallel`) and record the result.
