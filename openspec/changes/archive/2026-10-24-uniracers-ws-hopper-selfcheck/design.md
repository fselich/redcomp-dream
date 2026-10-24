## Context

The Hopper start seed fixed the first seconds. A later Hopper Hopper race (1P, WARIO PAINT) still cut the widescreen right margin during a jump/stunt. The measured source log, holding right and then holding B for 30 frames at about one second:

```text
2599 anchor ok seed ax=72 ay=-14
2841 anchor (72,-14) dropped: self-check 15/32 after 0 holds
2841 anchor dropped: no anchor passes the self-check
...
3113 anchor ok seed ax=72 ay=-14
```

The seed re-converges to the same origin `(72,-14)`, so the anchor is not wrong: the verified BG1 cells stop matching the table while the jump moves the vertical view. The same track gap without B produces a shorter disagreement (`3093..3114`, 15/32 then 0/32) that the 30-frame grace already covers. The B-hold stunt lasts ~273 frames, so the 30-frame grace cannot cover it.

The root cause is the airborne state: `$7E:0BE9` is nonzero exactly while the rider is airborne/stunting and the camera Y is negative (measured 1 at frames 2841..3110, 0 before and after), and the BG1 rows move with the jump. `$7E:0BE9` also rises in the no-jump gap at frame ~3108.

## Goals / Non-Goals

**Goals:** keep the source anchor and keep serving the table for the whole airborne state, so the 21:9 right margin does not lose the track during or after the jump; keep the 30-frame drop guarantee on the ground; keep the immediate invalidation on a scene change/non-live race/invalid header; validate the hold-B route and the existing gate.

**Non-Goals:** changing jump input, camera, BG1 data or the source table; a per-track or per-jump special case; inventing track where the table has none (the gap stays empty).

## Decisions

### D1 — Airborne hold keyed on `$7E:0BE9`

Add `kRamAirborne = 0x0BE9`. In `WsSrcFrame`, when a check is below 95 % and `g_ram[kRamAirborne] != 0`, keep the anchor, reset its `self_fail`, and log `self-check ... hold air`; the table keeps serving the margins. When the rider is on the ground, the existing per-anchor `self_fail`/`WS_SRC_SELF_GRACE=30` path runs unchanged and the 30th consecutive failure drops the anchor. A pass, a new anchor, a scene reset and a non-live race reset the counter as before.

### D2 — Why not a longer fixed grace

The measured B-hold transient is 273 frames (2841..3113), so a fixed grace would have to be ~300 frames (5 s); a genuinely wrong anchor would then be served for 5 s on any track. The airborne byte scopes the hold to the state that actually causes the disagreement and leaves the 30-frame guarantee for every ground-level failure. The byte is sampled from the game state, not from the input, so it also covers a gap that the player crosses without pressing B.

### D3 — Validation

- `game/tools/route-validate-hopper-jump.txt` holds right, then holds `right+b` for 30 frames at about one second. The `hopper-jump` case runs it at 21:9 with `SNESRECOMP_UNLOCK_TOURS=1` and captures frame 3000 (during the stunt).
- With the fix the source log has `hold air` and no `anchor dropped` between the seed and the recovery, the off-window log has zero masked cells in the race, and the frame's track reaches the right edge. Without the fix the source log drops at ~2841 and the off-window log masks the race margins; the frame's track ends at the 4:3 edge (`x≈354` of 448, right margin empty) while the fixed frame reaches `x=447`.
- The existing Hopper menu (1960) and race (3200) cases stay; the full gate (19 cases) passes and 1P/2P frames and the Dragster source/park diagnostics are unchanged.

## Risks / Trade-offs

- The airborne byte is an observed game flag (`$7E:0BE9`), not named by the original disassembly; if a later tour leaves it set on the ground the anchor is held longer. The 30-frame ground drop still catches a wrong anchor once the flag clears.
- In 2P the byte is shared (the player 1 airborne state); a P2 jump may also hold the P1 anchor. The self-check is a safety net, not a correctness requirement, and the scene reset still invalidates.
- Serving the table during the airborne state assumes the table origin is still valid; the measured post-stunt seed is the same `(72,-14)`, and the frame at the gap (3100) shows only the table's real track, never invented track.

## Migration Plan

No data or option. The hold is presentation-only and inert outside a source race. Reverting restores the first-failure self-check drop and the Hopper stunt cut.

## Open Questions

- Do the later tours leave `$7E:0BE9` set on the ground? R1 covers the rest.
- Is the shared 2P flag acceptable, or does P2 need its own airborne state? The 2P gate passes; a P2-specific field can be added if a real case appears.
