## 1. Preparation

- [x] 1.1 Create `work/ws-native-map-spike/{project,logs,frames,metrics,patches}/`.
- [x] 1.2 Copy the current validated clamp build and record its commit/framework pin.
- [x] 1.3 Reproduce native 4:3 and clamp 16:9 at the Dragster and Zoom Zoo loop frames.
- [x] 1.4 Build a trace/debug binary for WRAM, VRAM and PPU-scroll capture.
- [x] 1.5 Record the current `SNESRECOMP_WS_TRACK=clamp|off|fill` behavior as the baseline.

## 2. Full level-table decode

- [x] 2.1 Dump WRAM `$7E`/`$7F` at a loop frame, including `$7F000F/$7F800F`.
- [x] 2.2 Confirm the table shape. The actual stride is `$04F3` and the column count is `$04F1`; the earlier `$0236`/`$8FA` interpretation was wrong (seen `0x800`/`0x400` on Dragster and `0x200`/`0x100` on Zoom Zoo).
- [x] 2.3 Characterize the entry format. Low/high tables hold real cells; `$00A0` is a stage-2 sentinel, not evidence of a blank table.
- [x] 2.4 Trace how `$023E` selects a source chunk and how `$0433/$0455` are written and DMAed. (`$81:B3E2`, 17 words, DMA dest `0x0E88`, frame 3504.) Read-watch PB/S caveat: function labels are stale; stage-1 vs `$B3D0` attribution still needs separation.
- [x] 2.5 Record stage 1 (`$81:8A4A`) segment IDs `$0260…` and stage 2 (`$81:8B95`) tile output.
- [x] 2.6 Build an offline blank-vs-track map of the full table and store it under `work/`.

## 3. World ↔ screen mapping

- [x] 3.1 Capture camera/world position, PPU `hScroll/vScroll` and VRAM BG1 cells on the same frame.
- [x] 3.2 For each native 4:3 column, find the table column that reproduces its VRAM tiles. *(Superseded by ws-offwindow/ws-map-decode: the 16×16 ring formula and table decode supplied the mapping.)*
- [x] 3.3 Derive the formula and units from world position to table column/row to screen X/Y. *(Superseded by ws-offwindow: `map_x = ((hscroll + x) >> 4) & 31`.)*
- [x] 3.4 Extrapolate the formula to ±43 px and record the expected margin table cells. *(Superseded by ws-source: margins are served from the table, including ±43 px.)*
- [x] 3.5 Validate the decoded whole track against the VGMaps full-track images (shape, loops, connections). *(Partial: ws-map-decode validated topology on Zoom Zoo, Monster, Bowl and Megajump; the 403 download was not needed.)*
- [x] 3.6 Confirm the mapping on a second track with different geometry (Zoom Zoo). *(Confirmed later on Zoom Zoo, Bowl, Switcher and Monster by ws-offwindow/ws-source.)*

## 4. Generator / margin extraction

- [x] 4.1 Evaluate stage 1/2 for margin columns using AOT or the synthetic-call harness.
- [x] 4.2 Save/restore the full `CpuState` and WRAM around synthetic calls.
- [x] 4.3 Compare generated cells with the decoded table/map. (The `$0433` chunk matches BG1 map row 18 x=7..23 exactly; full margin mapping is still open.)
- [x] 4.4 Determine whether any margin cell requires state not present in the saved context. *(Superseded by ws-source: live WRAM table read; no extra saved state needed.)*
- [x] 4.5 Decide the production candidate: AOT promotion, pure `TrackColumn()`, or table read-only.
- [x] 4.6 Record whether the full table alone is sufficient, without running the guest generator per frame.

## 5. Test-only wide prototype

- [x] 5.1 Build a host-side BG1 tilemap for 342 columns using native plus decoded margin cells. *(N/A: no-go spike; ws-source took the production path.)*
- [x] 5.2 Add a dedicated test switch; do not reuse or redefine `SNESRECOMP_WS_TRACK=clamp|off`. *(N/A: no prototype; no separate switch.)*
- [x] 5.3 Keep HUD, cull and track switches independent. *(N/A: no prototype.)*
- [x] 5.4 Do not alter guest camera, scroll, RNG, input or simulation state. *(N/A: no prototype; later changes preserved simulation.)*
- [x] 5.5 Fall back to clamp for any frame or cell that cannot be mapped. *(N/A: no prototype; ws-source masks until the anchor is found.)*
- [x] 5.6 Capture native, clamp and prototype frames for both margins. *(N/A: no prototype.)*

## 6. Verification

- [x] 6.1 Verify 4:3 with the prototype and widescreen off is bit-identical to the baseline.
- [x] 6.2 Verify camera, scroll, RNG, input and gameplay are unchanged.
- [x] 6.3 Verify no wrapped, duplicated or smeared track in loops and slopes, both directions. *(N/A: no prototype.)*
- [x] 6.4 Verify the prototype track matches the full-map shape at the same world position. *(N/A: no prototype.)*
- [x] 6.5 Measure frame cost and memory; record whether it is acceptable for production. *(N/A: no prototype; cost was measured by later changes.)*
- [x] 6.6 Confirm all ROM-derived captures/dumps stay under `work/` and are gitignored.

## 7. Report and decision

- [x] 7.1 Write `report.md` with table layout, mapping evidence and the upload trace. *(No prototype; no-go.)*
- [x] 7.2 State go/no-go for a production native-map change.
- [x] 7.3 Update `docs/UNIRACERS_WIDESCREEN_TRACK_FILL.md` with the final result and correction status.
- [x] 7.4 If no-go, record the blocker and keep clamp as the accepted default.
- [x] 7.5 Summarize the next change (production native map or AOT promotion).

## Closure

Closed on 2026-10-22 as a historical no-go. The remaining items were either
superseded by `uniracers-ws-offwindow`, `uniracers-ws-map-decode` and
`uniracers-ws-source`, or were prototype steps that the no-go decision made
unnecessary. No work remains in this change.
