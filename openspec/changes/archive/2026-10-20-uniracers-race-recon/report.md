# Uniracers race reconnaissance (Phase C) — report

Date: 2026-10-20
Change: `uniracers-race-recon`
Outcome: deterministic 1P race reached; renderer mapped as **Mode 1 + BG
parallax + HDMA per-line scroll**, not Mode 7. No widescreen was implemented.

## Verdict

The route works and is deterministic, the race capture is complete, and the
renderer path is concrete enough to plan a 16:9 prototype. The main camera path
is identified at `$81AE7D–$81AE9B`, which integrates camera velocities into the
BG1/BG2 scroll mirrors. The HDMA per-line scroll path is confirmed
(`$210F`/`$210D`), but the code that builds the HDMA tables is still unresolved.
Culling is not yet identified.

## Route

Button mapping: **PS5 Cross (✕) = SNES B** in the host's default gamepad map.

| Frame (approx.) | Screen | Action |
|---:|---|---|
| 640 | main menu, `1P` selected | B |
| 790 | `PICK YOUR UNI`, first selected | B |
| 900 | `PICK TOUR`, first selected | B |
| 1080 | `PICK TRACK`, first selected | B |
| 1260 | `NOW PLAYING`, `RACE` selected | B |
| 1500 | black (race load) | — |
| 1800 | race, timer 0:01 | — |
| 3000 | race, timer 0:21 | — |

Determinism: two runs to frame 2400 produced byte-identical frames
(SHA-256 `0bd41144740f9ebe70a452e538519d86ba6b386b4503e0e692c866bf994cb260`).

## Capture

| Item | Value |
|---|---|
| Budget | 3,000 frames (≈50 s) |
| Exit / time | 0 / 51 s |
| Simulated speed | 59.996 fps |
| Coverage checkpoint / journal | 2,597,610 / 474,498 bytes |
| State dump | frame 2015 (`race.regs.json`, VRAM/CGRAM/OAM/WRAM) |
| Frame | `frames/race-3000.ppm` |

Coverage:

| Metric | Race (3,000 f) | Phase B menu (900 f) |
|---|---:|---:|
| Interpreted instructions | 40,960,774 | 10,641,670 |
| Hot instruction PCs | 14,017 | 1,505 |
| Dynamic discoveries | 594 | 107 |
| Observed hits | 928,666 | 505,071 |
| Fallback reason | `missing_entry` (594/594) | `missing_entry` (107/107) |
| Site kinds | 580 `call_gap`, 14 `goto_gap` | 102 `call_gap`, 5 `goto_gap` |

Race-only: **12,517 hot PCs** and **488 discoveries** that do not appear in the
Phase B menu capture. Discoveries with `first_frame >= 1525` are the true race
engine; entries at frames 643–822 are input-driven menu/selection code.

## Renderer map

Evidence comes from the PPU state dump at frame 2015 and executed-write sites
intersected with the coverage.

| Property | Evidence |
|---|---|
| Mode | **Mode 1**, `bgmode=$19` (mode bits = 1), `m7sel=0` |
| Layers | `tm=$17` → BG1 + BG2 + BG3 + OBJ; `ts=$10` → OBJ on subscreen |
| Scroll | `hscroll=[152,76,0,0]`, `vscroll=[204,102,0,0]`; BG2 = BG1/2 (parallax) |
| HDMA | `hdmaen_last=$3C` (channels 2–5) |
| HDMA ch3 | writes `$210F` (BG2HOFS), mode 3 |
| HDMA ch4 | writes `$210D` (BG1HOFS), mode 3 |
| HDMA ch2 | writes `$2107` (BG1SC) |
| HDMA ch5 | writes `$2131` (CGADSUB) |
| Mode 7 | not used during the race (`m7sel=0`); M7A writes at `$81B6D3`/`$81B6FF` are inert or unrelated |

Confirmed executed sites:

| Site | Write | Role | Cost |
|---|---|---|---:|
| `$808622` | `$2100` INIDISP | per-frame display setup | 1,475 |
| `$80863B` | `$212C` TM | layer enable | 1,475 |
| `$80883C` | `$2106` MOSAIC | per-frame display setup | 1,475 |
| `$8087D9` | `$420C` HDMAEN | enables HDMA channels 2–5 | 1,470 |
| `$8087DE` | `$2130` CGWSEL | color-math window | 1,470 |
| `$808665/$808669` | `$2102/$2103` | OAM address | 1,470 |
| `$808691/$808699` | `$4362/$4364` | DMA/HDMA channel 6 setup | 1,470 |
| `$80875C/$808764/$808769` | `$4344/$4340/$4341` | ch4 A-bus bank / DMAP / BBAD | 1,470 |
| `$82B8C4/$82B8C9/$82B8CE` | `$4300/$4301/$4306` | ch0 DMA setup (VRAM upload) | 1,470 |
| `$83CC6F` | `$420D` MEMSEL | FastROM enable | 1,475 |
| `$80920C` | `$2105` | mode setter (also-menu) | 1 |
| `$80E250/$80E254` | `$210F` BG2HOFS | direct BG2 scroll write | 156 |

The per-frame block is at `$8086xx–$8087xx`: it sets INIDISP, TM, MOSAIC, OAM
address, HDMA channel pointers, HDMAEN and CGWSEL, then returns to the game.

### Camera path (confirmed)

`$81AE7D–$81AE9B` (M0X0) is the per-frame scroll update:

```
LDA $7E202A ; CLC ; ADC $04F9 ; STA $7E202A ; LSR ; STA $7E2032   ; BG1 V, BG2 V = /2
LDA $7E2026 ; CLC ; ADC $04F5 ; STA $7E2026 ; LSR ; STA $7E202E   ; BG1 H, BG2 H = /2
```

The WRAM values at frame 2015 match the PPU exactly:

| WRAM | Value | PPU register |
|---|---:|---|
| `$7E:2026` | 152 | BG1HOFS |
| `$7E:202A` | 204 | BG1VOFS |
| `$7E:202E` | 76 | BG2HOFS |
| `$7E:2032` | 102 | BG2VOFS |

BG2 is derived as BG1/2, which is the parallax. `$04F5`/`$04F9` are the camera
velocity inputs. A second direct BG2 scroll path exists at `$80E23B–$80E254`
(`$90 + $A1` → `$210F`, written twice).

### HDMA tables (partially resolved)

The PPU state records current A-bus pointers `$7E:8262` (ch4, BG1HOFS) and
`$7E:8280` (ch3, BG2HOFS). The per-frame block loads the ch4 A1T from WRAM
`$2046` and the bank from `#$7E` (`$808755–$80875F`), and the dump shows
`$2046=$9870`. The WRAM around the current pointers was zero at the dump
boundary, so the table is probably consumed and rebuilt every frame; its base
and builder were not resolved.

## Hot code

| Site | Instructions | Share | What it is |
|---|---:|---:|---|
| `$82D4EE` and `$82D4F0` | 4,306,947 each | 10.5% each (~21% combined) | `BIT $57; BPL $D4EE` — frame/vblank wait loop |
| `$82B1F5` / `$82B1FC` | 58,384 each | 0.14% | VRAM upload loop (`STA $2118`, `JSR $B293`, `STA $2119`) |
| `$83F2xx` cluster (M0X1) | ~100k each | ~0.25% each | shift/DEX/BMI loop — math or checksum routine |
| `$818A79` | 2,950 | <0.01% | race entry: `JSL $81B664`, table walk, `CMP $04F1` |
| `$8384D1` | 4,230 hits | — | route-only call into WRAM routine `$000199` (frames 643–822) |

The wait loop explains why the interpreter is fast enough: most of the frame is
spent waiting for the NMI, while the game logic runs in the interrupt. AOT
promotion should start with the display/camera/HDMA paths, not the wait loop.

## Camera, viewport and culling

**Camera/viewport candidate (confirmed):** `$81AE7D–$81AE9B` updates the scroll
mirrors `$7E:2026/$202A/$202E/$2032`; the values match the PPU. This is the
first lever for a wider view: the margins need camera/scroll data beyond the
native 256 px, and the BG tilemaps need authored data there.

**HDMA curve candidate (partially resolved):** ch3/ch4 write `$210F/$210D` per
line. The scroll tables produce the track's curves; their builder and base are
unresolved. Extending them for 16:9 is likely the hardest renderer-level step.

**Culling (not identified):** OAM setup is at `$808665/$808669`, but the object
selection/culling logic was not located. The race table loop at `$818A79` is a
candidate for object/track processing; it needs targeted tracing.

## Widescreen feasibility

**Renderer-level:** feasible in principle. The framework's 2D doctrine applies
more directly than expected:

- P1: take scroll phase from the PPU, not a WRAM mirror, for the margin seams.
- P2: prefill margin columns before they are visible.
- P2c: anchor or stretch the BG3 HUD.
- OAM limits: extend the object window for the unicycle and rivals.

The concrete work is to extend the BG1/BG2 margin data and the per-line HDMA
scroll tables so the extra columns follow the same curves.

**Engine-level:** unresolved. Camera clamps, track streaming and object culling
still need mapping; these are the likely per-game RTL work.

**AOT prerequisite:** the race code is 100% interpreted. The framework's
widescreen hooks are injected into generated C, so the display block
(`$8086xx–$8087xx`), the camera update (`$81AE7D`) and the HDMA builder must be
promoted to AOT before hooks can be placed. The race capture is a ready-made
profile manifest for that promotion.

## What this does not prove

- That 16:9 works, looks correct, or preserves gameplay.
- That the HDMA table builder has been found (it has not).
- That object culling or track streaming has been mapped.
- That the race is completable or that audio is correct.
- That AOT promotion will preserve the deterministic frame hashes.

## Recommendation

1. **`uniracers-aot-promotion`** — feed the race capture as a profile manifest,
   promote the display block, camera update and the top race entries, rebuild,
   and replay the same 3,000-frame route. Compare frame hashes and the
   discovery set.
2. **`uniracers-widescreen-spike`** — with the hot functions native, prototype
   margin sampling plus HDMA table extension, and compare 4:3 vs 16:9.
3. **Focused follow-up** — trace WRAM writes to the HDMA tables and OAM to
   resolve the curve builder and culling.

## Artifacts

- Workspace: `work/race-recon/` (gitignored).
- Scripts: `scripts/route-1p.txt`, `scripts/route-1p-dump.txt`, `scripts/route-notes.md`.
- Coverage: `coverage/uniracers/uniracers_1792507129_p1068106_s1.json` and `.jsonl`.
- Dump: `dumps/race.regs.json`, `race.wram.bin`, `race.vram.bin`, `race.ppuw.tsv`, `race.dma.tsv`.
- Metrics: `metrics/race-ingest.json`, `metrics/race-vs-menu.json`, `metrics/ppu-write-sites.json`, `metrics/hdma-pointer.txt`, `metrics/wram-map.txt`.
- Frames: `frames/race-3000.ppm`, `frames/det-1-2400.ppm`, `frames/det-2-2400.ppm`.
- This report contains no generated C, no ROM bytes, and no game data.
