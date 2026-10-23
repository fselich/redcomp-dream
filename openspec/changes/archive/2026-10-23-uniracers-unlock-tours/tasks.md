## 1. Research on copies (no game code)

- [x] 1.1 Back up the player save (`saves/save.srm`, `.bak`, `work/save-backups/`) and work only on copies, running a copy of the executable in a scratch directory
- [x] 1.2 Write `tools/scripts/save/srm.py` with `dump` and `verify`. `verify` checks the four known word-sum checksums ($016C, $054E, $05E6, $073C) on the fresh dump and the player save; find the exact start of the $054E block
- [x] 1.3 Find the PICK TOUR rule. On copies, set the per-tour flags ($069C + n·$20) one at a time, then whole Bronze/Silver/Gold track groups, and record which tours appear (including the ninth) and which levels open in PICK TRACK
- [x] 1.4 Write `docs/UNIRACERS_SAVE_FORMAT.md`: blocks and checksums, `$10D3` stages, medal flags, `$10D1`, the unknowns (track table, `$0236`), how each fact was found, and the regeneration steps for an unlocked test save

## 2. SRAM tool

- [x] 2.1 Add `srm.py unlock IN OUT` (raise `$10D3` to 3), refusing `OUT == IN` and invalid input
- [x] 2.2 Check: `verify OUT` passes and `diff IN OUT` shows only `$10D3`
- [x] 2.3 Generate the A1 test save (fresh save + unlock) into `work/` and note it in the format doc

## 3. Game option

- [x] 3.1 Name `kSramMagic`, the block table, `kSramTourStage` and `kSramTourStageAll` in `variables.h`
- [x] 3.2 Add `game/src/uniracers_save.[ch]`: read `UnlockAllTours` from `[Emulation]` (`config.ini`, then `config.local.ini`) and `SNESRECOMP_UNLOCK_TOURS`; `SaveImageValid`; `SaveUnlockTick` (raise only, valid image only, not in netplay, one log line on first raise)
- [x] 3.3 Call the reader from `GameAfterConfig` and the tick from `GameAfterRunFrame`; add the file to `game/CMakeLists.txt`; build
- [x] 3.4 Add a commented `UnlockAllTours = 0` under `[Emulation]` in the shipped config template, if the project has one (none exists: the framework writes the default `config.ini`; documented in the README instead)

## 4. Verification (scratch directory, copies only)

- [x] 4.1 `validate.sh` with the option off: every case matches its reference
- [x] 4.2 Copy of the player save + option on: PICK TOUR shows nine tours, medals unchanged, saved file differs only at `$10D3`
- [x] 4.3 No save + option on: SRAM is formatted, then `$10D3 = 3`, and PICK TOUR shows nine tours
- [x] 4.4 Restart the unlocked save with the option off: nine tours still visible
- [x] 4.5 Corrupt one checksum in a copy and boot with the option on: nothing written
- [x] 4.6 Cross-check: `srm.py unlock` output equals the game's SRAM after the first patch frame

## 5. Close

- [x] 5.1 Document the option in `README.md` (Run section): permanent effect, how to restore a backup; mark C11 and the A1 save in `docs/UNIRACERS_MEJORAS.md`
- [x] 5.2 Write `report.md`: the format findings, the experiment table and the verification results
