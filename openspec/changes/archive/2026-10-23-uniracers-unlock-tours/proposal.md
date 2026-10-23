## Why

Uniracers reveals its nine tours in stages as the player earns medals: the
four starting tours, then Jumper and Bounder together, then Runner and
Sprinter, then Hunter. There is no in-game way to play every track from the
start. That blocks:

- casual play and practice on any track (`docs/UNIRACERS_MEJORAS.md` C11);
- the 45-track validation (A1), which needs a save with every tour open.

Research on copies of a save (tasks 1.1–1.3) found that a single SRAM byte,
`$10D3`, holds the reveal stage, outside every checksummed block. Once a
tour is visible, every level and track in it can be picked.

## What Changes

- Document the SRAM progress format in `docs/UNIRACERS_SAVE_FORMAT.md`:
  - the five checksummed blocks (16-bit word sum stored after each block);
  - the reveal stage `$10D3`;
  - the per-tour medal flags `$069C + n·$20`;
  - the last chosen level `$10D1`;
  - what is still unknown (the 150-byte track table).
- `tools/scripts/save/srm.py`:
  - `dump`, `verify`, `diff`, `set` (already written in 1.2);
  - `unlock IN OUT`, which writes a copy with `$10D3 ≥ 3`. It never
    overwrites its input.
- A game option `UnlockAllTours = 0 | 1` under `[Emulation]` in
  `config.ini`; `SNESRECOMP_UNLOCK_TOURS` overrides it for one session.
  - When on, the host raises `$10D3` to 3 in the **active save** as soon as
    the game has a valid SRAM image.
  - This is **permanent** for that save: the game stores the byte on exit,
    and turning the option off later does not hide the tours again.
  - Medals, records and the rest of the progress stay untouched.
- Off by default: with the option off, the binary behaves byte for byte as
  today.

## Capabilities

### New Capabilities

- `recomp-save-unlock`: the documented SRAM progress format, the SRAM tool,
  and the opt-in tour unlock.

### Modified Capabilities

None.

## Impact

- New `docs/UNIRACERS_SAVE_FORMAT.md`; `tools/scripts/save/srm.py`.
- `game/src/`: a small `uniracers_save.[ch]` (option read, unlock check)
  called from `GameAfterConfig` and once per frame. The SRAM offsets are
  named in `variables.h`.
- This is a **guest-state change** (SRAM). It is opt-in and limited to one
  byte that only ever goes up.
- Uses `game/tools/validate.sh` to show the default build is unchanged.
- No change under `game/snesrecomp/`.
- Out of scope:
  - a separate save root;
  - marking medals or tracks as finished (the `complete` mode was dropped:
    it is not needed for play or for A1);
  - an in-game menu toggle (A6b).
