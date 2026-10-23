## Context

Uniracers keeps all progress in its 8 KiB battery SRAM and works on it in
place. The ROM carries a default image at `$83:8000` (`ASJIver3.30`, rival
names, profiles, a name filter).

Facts verified on copies (tasks 1.1–1.3; `work/unlock-tours/`):

```
$000C-$016B  names     ┐
$0230-$02AF  progress  │ each block is followed by a word holding the
$02B2-$054D  records   │ 16-bit sum of its little-endian words
$0550-$05E5  tracks    │ (verified on a fresh save and two progressed saves)
$05E8-$073B  tours     ┘
$069C+n·$20  per-tour medal shown under the tour in PICK TOUR (n = 0..3 seen)
$10D1        last level chosen (0 Bronze, 1 Silver, 2 Gold)
$10D3        tour reveal stage, NOT checksummed:
               0 → Crawler, Shuffler, Walker, Hopper
               1 → + Jumper, Bounder
               2 → + Runner, Sprinter
              ≥3 → + Hunter (Gold only)
```

How it was found: grafting regions of the player save onto a fresh one
narrowed visibility to `$1000–$1FFF`, then to `$10D3`. Clearing the medal
flags kept Jumper and Bounder visible, and the counters at `$0230`/`$0232`
changed nothing.

With `$10D3 = 3` on a fresh save:
- all nine tours appear;
- Jumper/Wobble (Bronze) and Hunter/Griller (Gold) race normally;
- the value is saved on exit and still shows every tour after a restart.

Within a visible tour, every level and track can be picked, both in the
player's save and in the patched one.

Framework facts:
- the desktop host reads SRAM (`RtlReadSram`) after `after_config` and
  before the first frame;
- it writes SRAM back on exit;
- `g_sram` / `g_sram_size` expose the live image;
- the config parser accepts unknown keys silently in `[Video]` and
  `[Emulation]`, but prints "Invalid .ini section" for an unknown section.

## Goals / Non-Goals

**Goals:**
- All nine tours available in the player's save, by an explicit option.
- A documented SRAM progress format and a tool that edits copies.

**Non-Goals:**
- Marking tracks or medals as finished; decoding the 150-byte track table.
- A separate save root (rejected by the user: they want their own progress
  plus every tour).
- Netplay: the option is ignored in a netplay session, so peers stay in
  sync.

## Decisions

### D1. Raise one byte, never lower it

The patch is `if ($10D3 < 3) $10D3 = 3`.
- It never lowers the stage, so it cannot hide anything the player earned.
- It never touches the checksummed blocks, so no checksum is recomputed.
- The game's own progress (medals, records, counters) is unchanged.

Alternatives:
- Hook the PICK TOUR check: the routine runs on the interpreter and isn't
  mapped, and other screens may read the byte too.
- Mark everything finished: unnecessary, and it destroys progression.

### D2. When to apply

`$10D3` is checked between frames, after `run_frame`. The write happens only
when:
- the option is on and no netplay session is active;
- `g_sram` holds the magic `ASJIver3.30`;
- every block checksum is valid. This is a cheap sanity check that the image
  is a real Uniracers save and not mid-format.

With an existing save this is true on the first frame, long before PICK
TOUR. On a missing save the game formats SRAM during boot; the next check
after that applies the patch. Task 3.5 verifies that the menu, which reads
SRAM directly, then shows every tour.

The check is idempotent and costs a few hundred byte reads per frame. It
runs every frame, so a reformat (for example, a game reset that wipes the
save) is caught too.

### D3. Permanence is explicit

The host writes the patched SRAM on exit, so the player's `save.srm` keeps
`$10D3 = 3`. The README and the option's comment say so. The host also
prints one line the first time it raises the byte
(`[unlock] tour stage 1 -> 3`), so the change is visible in the log.

### D4. Configuration

`UnlockAllTours` lives in `[Emulation]`, a host-owned section the framework
accepts without warnings, so no framework change is needed. It is read with
the existing `WsParseBool` helper from `config.ini` then `config.local.ini`,
like `MiniMap`. `SNESRECOMP_UNLOCK_TOURS` (via `WsEnvStr`) overrides it.
A6 can move it into a game section later.

### D5. Tool and game share the definition

- `srm.py unlock IN OUT` applies the same rule as D1 to a copy, refusing
  `OUT == IN` and an invalid input.
- `uniracers_save.c` names `kSramMagic`, `kSramTourStage = 0x10D3`,
  `kSramTourStageAll = 3` and the block table in `variables.h`.
- A cross-check task compares the two outputs on the same input.

## Risks / Trade-offs

- [The game also derives the stage from progress and lowers it] → not seen:
  the stage survived a race, a save and a restart. Task 3.5 repeats this
  with the option on and then off.
- [Something else reads `$10D3` (attract mode, a "new tour" banner)] → it
  is a guest write, opt-in; `validate.sh` runs with the option off.
- [The player regrets the permanent unlock] → `work/save-backups/` holds
  copies; the README explains how to restore one.
- [Patching during the game's own SRAM write] → the check runs between
  frames and requires valid checksums first.

## Migration Plan

Nothing to migrate: off by default. To undo on a save, restore a backup.
`srm.py set` can also put `$10D3` back by hand.

## Open Questions

- The meaning of the 150-byte track table and of `$0236` (kept as
  hypotheses in the format doc; not needed here).
- The small icon under HUNTER at stage ≥3 on a fresh save (probably a default
  medal slot); cosmetic, to note in the doc.
