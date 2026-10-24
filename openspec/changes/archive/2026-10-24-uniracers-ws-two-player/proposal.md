## Why

Two-player split screen is the only race mode that still runs at the native 256-px width. `WsInLiveRace()` keys on `$00B4`, which is `0` in a 2P race (the split-screen flag is `$0DDB = 1`), so every widescreen layer is gated off: frame width, BG1 margins, HUD bands, cull widening and rider OAM. On a modern window the top and bottom 256×112 views waste most of the screen, and the work already done for 1P (16:9 through 512 px, track margins, HUD, rider margins) is not applied.

This is roadmap item W4. The scope is: widen each half symmetrically (one player above the other), keep the split at line 112, and make the track margins, the HUD and the rider sprites correct in both halves, as they are in 1P. None of the three is optional: without them the widened 2P frame shows wrong leading-edge track cells in the bottom half, the HUD in the native centre, and riders drawn 256 px to the left or parked entries in the margins.

## What Changes

- **2P/VS live-race predicate.** A split-screen race is live when `$0DDB != 0`, `$0EE9 == 0` and `$0300 <= 1`, even though `$00B4 == 0`. The same predicate is expected to cover **2P** and **VS** (VS must be measured). Menus, PICK screens and NOW PLAYING stay native.
- **Per-half frame width.** The frame becomes `E + 256 + E` × 224 with the split at line 112, using the existing `Aspect`, `MaxWidth` and `DisplayAspect` settings. At 16:9 each half is 342×112; at 21:9 448×112; at 512 px 512×112. `PrepareFrame()` needs no change.
- **BG1 margins per viewport.** The ring observer (`ws_ring.c`) and the source anchor (`ws_source.c`) assume one camera per frame: one scroll unwrap, one BG1 map base latched at line 1, one seen table and one anchor. In 2P the bottom half follows a second camera that can be thousands of pixels away, so that state becomes per viewport (top / bottom). The `source` and `clamp` policies themselves do not change; 1P uses viewport 0 only and must be output-identical.
- **HUD for both halves.** BG3 gets three elastic bands (top half, divider, bottom half) whose segments move `RACE` to the left edge, the timer and player name to the right edge, keep the centred prompts in place and stretch the divider. Columns no segment covers are not drawn, which clamps the rest of BG3 without a clamp band (a clamp band would override the elastic bands). The BG3 widen gate moves from line 17 to line 10 in 2P.
- **Rider OAM for both halves.** The game stages four rider entries (slots 96..99: P2 bottom, P1 bottom, P1 top, P2 top) with per-half cameras and a +112 Y offset. In 2P the port sets the missing OAM X high bit for a real rider in `[256, 256+E)` using the camera of that slot's half, hides the per-half parking sentinels, and writes the bottom half's high-table size bit before each line so the bottom rider is drawn big. The 1P parked-pair and `$170,$70` rules stay 1P-only. The framework's raster journal does not replay OAM writes, so the port synthesises the per-line high/size state from the staged entries and WRAM; it does not replay the game's OAMDATA writes.
- **Cull reuse.** `WsApplyCull()` already scales the margin by `$03ED` (4 in 2P); it only needs the new predicate.
- **Radar stays out.** The radar remains 1P-only.
- **Presentation only.** The only guest write remains the existing cull window. OAM fixes touch only the PPU's OAM copy; HUD and margins touch only PPU configuration and the renderer hooks.
- **Durable documentation.** A new `docs/TWO_PLAYER.md` records how the feature works and how it was measured and built.

## Capabilities

### New Capabilities

- `recomp-ws-two-player`: 2P/VS split-screen live-race detection, per-half frame widening, per-viewport BG1 margins, HUD/divider policy, four-entry rider OAM policy, gating and the 2P validation matrix.

### Modified Capabilities

- `recomp-ws-hud`: adds the 2P band set (three elastic bands, no clamp band, BG3 widen from line 10); the 1P band set stays unchanged.
- `recomp-ws-objects`: adds the 2P rider OAM requirement and changes the high-bit fix gate so that it runs in 2P with the 2P slot policy instead of being inactive.
- `recomp-ws-source`: the ring observation, scroll unwrap, seen table and anchor are kept per viewport; 1P has one viewport and is unchanged.
- `recomp-ws-ultrawide`: states that the aspect-driven width applies per half in 2P/VS, with the same 342..512 px range and cap.

## Impact

- `game/src/ws_frame.c`: 2P/VS branch in `WsInLiveRace()`; a helper that maps a scanline to its viewport.
- `game/src/ws_ring.c/.h`: ring state moved into a per-viewport struct; line-indexed accessors.
- `game/src/ws_source.c/.h`: anchor set, scene generation and race start per viewport; shared piece-library index; viewport-0 accessors kept for the radar.
- `game/src/ws_margin.c`: line hook and tile hook select the viewport by line; per-viewport map check; a 2P per-line diagnostic log.
- `game/src/ws_hud.c`: 2P band set; slots 1..2 cleared on the 1P and off paths.
- `game/src/ws_oam_fix.c`: 2P four-slot policy; 1P paths unchanged.
- `game/src/ws_cull.c`, `game/src/main.c`, `game/src/ws_radar.c`: no code change expected; verify only.
- `game/tools/validate.sh`, new `game/tools/route-validate-2p-*.txt` routes, `game/tools/validate_refs.txt`.
- `docs/TWO_PLAYER.md` (new), `docs/WIDESCREEN.md`, `docs/TRACK_MARGIN.md`, `docs/VALIDATION.md`, `docs/ROADMAP.md` (W4), `docs/README.md`, `README.md`.
- No framework patch: the existing beam line hook, margin tile hook, elastic bands (6 slots, 8 segments) and OAM copy are sufficient.
