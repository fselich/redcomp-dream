# Hopper widescreen self-check hold — report

## Symptom

A 1P Hopper race (PICK TOUR three downs, WARIO PAINT) with widescreen 21:9. Holding right and holding B at about one second makes the rider airborne/stunt for several seconds; during and after the landing the right widescreen margin is empty, so the track visibly ends at the 4:3 edge. The track returns after a few seconds. The same happens with a plain jump over a later track gap; the player sees it as "jump and the track to the right is gone".

## Root cause

The source self-check dropped a still-correct anchor on the first sub-95 % frame. While the rider is airborne the vertical camera moves and the BG1 rows change, so the verified cells no longer match the table even though the anchor still names the same table origin. The anchor re-appears as the same `(72,-14)` after the stunt, which proves it was not wrong.

Measured source log (hold B for 30 frames at ~1 s, no fix):

```text
2598 anchor ok seed ax=72 ay=-14
2841 anchor (72,-14) dropped: self-check 15/32 after 0 holds
2841 anchor dropped: no anchor passes the self-check
...
3113 anchor ok seed ax=72 ay=-14
```

The drop window is 273 frames (~4.5 s). The earlier 30-frame grace could not cover it. Without B, the track gap still produces a 19-frame disagreement (3093..3114); that one is within the grace. The airborne state was isolated with WRAM dumps: `$7E:0BE9` is 1 during the stunt (frames 2841..3110) and the no-jump gap (~3108), 0 on the ground, and the camera Y is negative while it is set.

## Fix

`kRamAirborne = $7E:0BE9`. In `WsSrcFrame`, a below-95 % check while `kRamAirborne` is set keeps the anchor, resets its failure counter and logs `hold air`; the table keeps serving the margins. On the ground the existing 30-frame grace/drop is unchanged. Scene reset, non-live race and invalid header still invalidate immediately. No guest writes; the source is presentation-only.

## Visual measurement

Frame 3000, 21:9, 448×224, track row 158, counted from the PPM:

- No hold: track spans `x=0..354` (355 px) and ends at the 4:3 right edge (~352); the right margin `x≈355..447` is background.
- Airborne hold: track spans `x=0..447` (448 px); the off-window log has 0 masked race cells.
- At the gap (frame 3100) the hold serves only the table's real track piece on the left and does not invent track in the gap.

## Validation

- New route `game/tools/route-validate-hopper-jump.txt` holds right and then holds B for 30 frames at ~1 s; the `hopper-jump` case runs it at 21:9 with `SNESRECOMP_UNLOCK_TOURS=1` and captures frame 3000.
- `game/tools/validate.sh --record` re-recorded the refs; `hopper-jump` is deterministic (no NONDET).
- Full gate: 19/19 PASS (`native`, `16x9`, `21x9`, `512`, `clamp`, `map-wrap`, `map-ring`, `monster`, `zoomzoo512`, `logs`, `2p-*`, `vs-16x9`, `hopper-menu`, `hopper-16x9`, `hopper-jump`).
- The `logs` (Dragster) source/park refs are unchanged; 1P/2P frames unchanged.

## Limits

- `$7E:0BE9` is a measured game byte, not a named original-disassembly variable. If a later tour leaves it set on the ground, the anchor is held longer; the 30-frame ground drop still runs once it clears.
- In 2P the byte is shared (P1 airborne); a P2 jump may hold the P1 anchor too. The gate passes; a per-viewport field can be added if a real case appears.
- The gap itself is not filled and must not be: the table has no track there.

## Decision

Adopt the airborne hold. The first-failure drop stays for ground-level failures; the 30-frame grace covers short transients; the airborne state is held for as long as it lasts. Archive/commit is left pending the player's confirmation on the real build.
