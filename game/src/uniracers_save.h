/* Battery-SRAM helpers and the opt-in tour unlock.
 *
 * Owns the checksummed block table and `UnlockAllTours`. With the option on
 * (and outside netplay) it raises the active save's tour reveal stage to "all
 * nine tours" -- a guest SRAM write, permanent for that save. With the option
 * off it writes nothing. See docs/UNIRACERS_SAVE_FORMAT.md and
 * openspec/changes/uniracers-unlock-tours.
 */
#ifndef UNIRACERS_SAVE_H
#define UNIRACERS_SAVE_H

/* Magic present and every block checksum valid. */
int SaveImageValid(void);

/* Read [Emulation] UnlockAllTours and SNESRECOMP_UNLOCK_TOURS. */
void SaveAfterConfig(void);

/* Between frames: raise the tour stage if the option is on. */
void SaveUnlockTick(void);

#endif /* UNIRACERS_SAVE_H */
