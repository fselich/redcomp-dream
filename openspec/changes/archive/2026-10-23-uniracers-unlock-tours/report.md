# Uniracers unlock tours — report

Status: **done**. `[Emulation] UnlockAllTours = 1` (or
`SNESRECOMP_UNLOCK_TOURS=1`) shows all nine tours by raising one SRAM byte.
`srm.py unlock` writes the same change to a copy.

## Finding

PICK TOUR visibility is a single SRAM byte, `$10D3`, outside every
checksummed block:

| `$10D3` | Tours shown |
|---|---|
| 0 | Crawler, Shuffler, Walker, Hopper |
| 1 | + Jumper, Bounder (the player's save) |
| 2 | + Runner, Sprinter |
| ≥ 3 | + Hunter (Gold only) |

Within a shown tour every level and track can be picked (confirmed in game by
the player, and in patched copies).

Experiments on copies (`work/unlock-tours/`, headless, scratch directory):

| Edit on a fresh or player copy | Right column of PICK TOUR |
|---|---|
| track table `$0550` all `$00` | unchanged |
| 48-byte table `$05E8` all `$00` | unchanged |
| medal flags `$069C+n·$20` cleared / set | medals hidden / shown; tours unchanged |
| counters `$0230/$0232/$0236` swapped | unchanged |
| graft player `$073E–$1FFF` | Jumper, Bounder appear |
| graft `$1000–$1FFF` → `$10D1–$10D3` → `$10D3` | Jumper, Bounder appear |
| `$10D3` = 2, 3, 4, 5, `$FF` | table above |

On a fresh save with `$10D3 = 3`, Jumper/Wobble (Bronze) and Hunter/Griller
(Gold) raced normally. The value survived a race, the save on exit and a
restart. `$10D1` turned out to be the last level chosen.

Checksums: five blocks, each followed by the 16-bit sum of its words. They
hold on a fresh save and on two progressed saves. A copy with one corrupted
checksum was not reformatted by the game.

## Decisions taken during apply

- **Option applies to the player's own save** (user choice). The separate
  save root from the first design was dropped.
- **Modes reduced to on/off** (user choice): `complete` was not needed,
  because tours are the only gate.
- **Config key in `[Emulation]`:** a host-owned section the framework accepts
  silently. An unknown `[Uniracers]` section prints a warning at every start.
- **`WsConfigBool`** in `ws_frame.c` replaces the radar's private INI reader,
  and both options use it.

## Verification

| Check | Result |
|---|---|
| `validate.sh`, option off | `validation OK`. All cases SKIP: the references recorded by `uniracers-code-cleanup` are NONDET except the `logs` BG1 score, which passed. |
| Player copy, same route, option off vs on | off: saved file identical to the input. on: differs only at `$10D3` (01 → 03). Nine tours shown, medals unchanged. |
| No save, option on | the game formats SRAM, then `[unlock] tour stage 0 -> 3`; nine tours shown |
| Unlocked save restarted with option off | nine tours shown; no write |
| Corrupt `tracks` checksum, option on | no write (`$10D3` stays 1) |
| `srm.py unlock` vs game, same input | byte-identical |
| `srm.py unlock IN IN` | refused, input unchanged |
| Netplay | not run (needs a session); guarded by `snes_netplay_active()` |

The player's `game/build-local/saves/save.srm` was never written by any
test; backups are in `work/save-backups/`.

## Follow-ups

- `validate.sh` compares almost nothing now that every frame reference is
  NONDET. A deterministic capture (for example a fixed state load before the
  screenshot) would make it a real gate again.
- The track table, `$0236` and the 48-byte table at `$05E8` are still
  undecoded. They are not needed for unlocking.
