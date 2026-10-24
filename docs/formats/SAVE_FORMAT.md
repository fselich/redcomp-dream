# Save format (battery SRAM)

The game keeps all progress in its 8 KiB battery SRAM (`saves/save.srm`) and
reads and writes it in place: a race-time WRAM dump holds none of it. The
ROM carries the template used to format a blank SRAM at `$83:8000`
(`ASJIver3.30`, the rival names, eight empty profiles, a word filter).

Tool: `tools/scripts/save/srm.py` (`dump`, `verify`, `diff`, `set`,
`unlock`). It only ever writes a copy.

Status tags: **[verified]** = observed on a fresh save and on progressed
saves, or confirmed by editing a copy and booting it; **[hypothesis]** =
consistent with the data but not tested.

## Checksummed blocks [verified]

Each block is followed by a 16-bit word holding the sum of the block's
little-endian words. The blocks are contiguous, except for the gap between
`names` and `progress`.

| Block | Range | Checksum at | Contents |
|---|---|---|---|
| `names` | `$000C–$016B` | `$016C` | rival names (16 bytes each) |
| `progress` | `$0230–$02AF` | `$02B0` | counters (below) |
| `records` | `$02B2–$054D` | `$054E` | record times per track, `$EA60` = none |
| `tracks` | `$0550–$05E5` | `$05E6` | 150-byte track table (below) |
| `tours` | `$05E8–$073B` | `$073C` | per-tour state, medal flags |

The magic `ASJIver3.30` sits at `$0000`. The game reads SRAM straight from
disk at boot and keeps an image whose checksums are valid. A copy with one
corrupted checksum (`tracks`) was not reformatted either: the game booted
and showed the same tours. So the checksums are not a boot-time gate. The
port's `UnlockAllTours` still refuses to write into such an image.

## Tour reveal stage: `$10D3` [verified]

Not covered by any checksum. PICK TOUR shows:

| `$10D3` | Tours shown |
|---|---|
| 0 | Crawler, Shuffler, Walker, Hopper (new game) |
| 1 | + Jumper, Bounder |
| 2 | + Runner, Sprinter |
| ≥ 3 | + Hunter (centre, bottom row; Gold level only) |

- Every level (Bronze/Silver/Gold) and every track of a shown tour can be
  picked. Nothing else gates a track.
- The game keeps the value across a race, the save on exit and a restart.
- In a progressed save (Bronze finished on the four starting tours) the value
  was 1.
- With `$10D3 ≥ 3` on a fresh save, a small medal icon shows under HUNTER.
  This is cosmetic and its source is unknown.

How it was found (on copies only; see `openspec/changes/archive/2026-10-23-uniracers-unlock-tours/report.md`):
- editing the medal flags, the counters and the track table changed nothing;
- grafting the player's `$1000–$1FFF` onto a fresh save showed Jumper and
  Bounder; narrowing down left `$10D3`;
- a value sweep (0–5, `$FF`) gave the table above.

## Other fields

| Offset | Meaning | Status |
|---|---|---|
| `$069C + n·$20` (n = 0..3) | medal shown under starting tour n in PICK TOUR | verified (setting it shows the bronze icon; clearing hides it, tours stay) |
| `$10D1` | last level chosen: 0 Bronze, 1 Silver, 2 Gold | verified (Jumper Bronze → 0, Hunter Gold → 2) |
| `$0230`, `$0232` | equal counters, 1 on a fresh one-race save, 21 on the progressed save | verified as values; do not drive PICK TOUR |
| `$0236` | 974 on the progressed save, 0 when fresh | unknown |
| `$0550` track table | 150 bytes, `$10` default, `$00` on 21 entries of the progressed save (groups of 5: the four starting tours' Bronze, plus one more) | hypothesis: 3 levels × 10 tour slots × 5 tracks, a per-track result. It does not gate selection. |
| `$05E8–$0617` | 48 bytes of `$10` | unknown |
| `$0740–$1FFF` | further state and scratch (HUD strings, a copy of the records at `$0829`) | mostly unknown; `$0829–$0E68` appears summed at `$0E69` |

## Unlocked test save

```sh
python3 -I tools/scripts/save/srm.py unlock IN.srm OUT.srm
```

`IN` can be any valid save. For a clean one, boot the game once with an
empty `saves/` in a scratch directory (about 700 frames, then quit) and take
the `save.srm` it writes on exit. Starting from a blank save the result shows all nine tours and no medals. Saves are ROM-derived and never committed.

The option `[Emulation] UnlockAllTours` (see [`CONFIG.md`](../CONFIG.md)) applies the same edit to the active save from `game/src/uniracers_save.c`: it raises `$10D3` to 3 only when the image has the magic and every block checksum is valid, and it never writes in netplay.
