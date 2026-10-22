# Uniracers AOT promotion (Phase D) — report

Date: 2026-10-20
Change: `uniracers-aot-promotion`
Outcome: **conservative promotion accepted with aligned-frame equivalence**.
Interpreted instructions dropped **7.91%**; visuals/state match, but fixed-frame
hashes shift by a stable **2 frames**. `CameraScroll` is emitted and registered
but still runs in the interpreter.

## Verdict

- 16 roots, **39 AOT variants**, 5 banks, 0 LLE-only; executable 1,869,664 bytes.
- Interpreted instructions: **40,960,774 → 37,722,540** (−3,238,234, **−7.91%**).
- Discoveries: 594 → 542. Hot instruction PCs: 14,017 → 12,540.
- `FrameDisplay` and 10 of 11 top race targets are AOT.
- `CameraScroll` has an emitted AOT body and a `dispatch_v2.c` entry but is never
  reached; the full `$81AE7D–$81AE9F` range remains interpreted.
- No tripwires, crashes, or new warnings.

## Baseline preserved

| Item | Value |
|---|---|
| Baseline project | `work/boot-spike/project/UniracersSNESRecomp` (untouched) |
| Baseline executable | 1,430,712 bytes |
| Frame 2400 SHA-256 | `0bd41144740f9ebe70a452e538519d86ba6b386b4503e0e692c866bf994cb260` |
| Frame 3000 SHA-256 | `b77dd93ffd89244afe0f5aca84c44054040412545879a40dacf24f911bd8adf7` |
| Interpreted instructions | 40,960,774 |
| Discoveries / hot PCs | 594 / 14,017 |

## Promotion set

A profile-only attempt promoted **274 roots, 462 variants, 422 AOT, 8 banks**:
a blanket promotion of the capture. It was rejected and narrowed to explicit
symbols with the observed M/X variants:

| Target | PC | M/X | Result |
|---|---|---|---|
| `FrameDisplay` | `$80:85A5` | 1,0 | AOT |
| `CameraScroll` | `$81:AE7D` | 0,0 | emitted, **not reached** |
| `Race_8182E6` | `$81:82E6` | 0,1 | AOT |
| `Race_8185BB` | `$81:85BB` | 0,1 | AOT |
| `Race_81B664` | `$81:B664` | 0,0 | AOT |
| `Race_818A4A` | `$81:8A4A` | 1,0 | AOT |
| `Race_81B668` | `$81:B668` | 0,0 | AOT |
| `Race_82A6F1` | `$82:A6F1` | 0,0 | AOT |
| `Race_83ED33` | `$83:ED33` | 0,0 | AOT |
| `Race_83EF10` | `$83:EF10` | 0,0 | AOT |
| `Race_81B77B` | `$81:B77B` | 0,0 | AOT |
| `Race_8184D2` | `$81:84D2` | 0,0 | AOT |

Excluded and confirmed not promoted: the frame-wait loop `$82D4EE` and the
asset VRAM upload `$82B1F5`.

Final manifest: 16 roots / 39 variants (all AOT) / banks `$00` 4, `$80` 4,
`$81` 22, `$82` 7, `$83` 2.

## Build

Configure + build exit 0 in 5 s. Regeneration exit 0 in 1 s. Executable
1,869,664 bytes. No build errors.

## Replay equivalence

The promoted frame hashes at 2400 and 3000 do **not** match the baseline
exactly. A frame window shows a stable offset:

| Baseline frame | CRC | Promoted frame | CRC |
|---:|---|---:|---|
| 2395 | `b07e14ba` | 2397 | `b07e14ba` |
| 2396 | `39aa5139` | 2398 | `39aa5139` |
| 2399 | `ed9e0548` | 2401 | `ed9e0548` |
| 2400 | `7f3f69a3` | 2402 | `7f3f69a3` |
| 2405 | `a0251deb` | 2407 | (outside window) |

Promoted frame **N = baseline frame N−2**. Visual metrics at a fixed frame:
non-black 97.0–97.2%, 42–44 colors, luminance ~145; pixel diff 0.5–1.0%. The
scene, timer and HUD are identical. The offset is a timing artifact of AOT, not
a rendering regression.

Accepted per the aligned-equivalence gate: documented `k=2`, stable across the
window, identical visual/state content, no tripwires.

## Measured effect

| Metric | Baseline | Promoted | Delta |
|---|---:|---:|---:|
| Interpreted instructions | 40,960,774 | 37,722,540 | −3,238,234 (−7.91%) |
| Guest cycles | 147,552,108 | 135,673,083 | −11,879,025 |
| Discoveries | 594 | 542 | −52 |
| Hot instruction PCs | 14,017 | 12,540 | −1,477 |

The reduction is real but modest: the hottest single loop is still the
frame-wait loop `$82D4EE` (`BIT $57; BPL`), which was deliberately excluded.

## Camera dispatch gap

`CameraScroll_M0X0` is declared, emitted (`bank81_v2.c`) and registered in
`dispatch_v2.c` at `0x81AE7D`. Yet the coverage still attributes **19,201
interpreted instructions** to `$81AE7D–$81AE9F` (1,477 per race frame across
13 instruction PCs), i.e. the whole routine.

The only direct call site in ROM is `$9D:83E2` (`JMP $AE7D`), which is outside
the analyzed banks; the actual route reach is therefore indirect. The
interpreter's indirect-call bridge does not enter the AOT body even though the
table entry exists. This must be resolved before a camera hook can be anchored
in generated C.

## Widescreen hand-off

Measured status of the framework's `docs/WIDESCREEN_PATTERNS.md`:

| Pattern | Status |
|---|---|
| P1 scroll phase from PPU | applies; use `g_ppu->hScroll/vScroll`, not `$7E:2026` |
| P2 margin prefill | applies; BG1/BG2 margins, per HDMA line |
| P2b bounded arenas | likely N/A; continuous track, verify |
| P2c HUD rigid/elastic | applies; BG3 `RACE`/timer, measure in isolation |
| P3 periodic fold | verify; BG2 parallax may be periodic |
| P4 key to rendered scroll | applies; mirrors advance before the PPU write |
| P5/P6 gameplay discriminator | **missing** |
| P7 cull windows | **missing** |
| P8 OAM reject compare | **missing** (OAM setup known at `$808665/$808669`) |
| P9–P12 spawn/activation | **missing** |
| P13 staging lead | **missing** |
| P14 kill switches | planned |
| P15 previews stand down | planned |
| P16 4:3 bit-identical | **offset-aware**: AOT shifts fixed-frame hashes by 2 |
| P-U1 HDMA per-line extension | **new, blocked on the table builder** |
| P-U2 BG2 = BG1/2 parallax | **new**, preserve per-layer offsets |

Order of work for the widescreen change: P16 aligned baseline → P5/P6
discriminator → P1–P4 + P-U1/P-U2 backgrounds/HDMA → P2c HUD → P7/P8 cull/OAM
→ P9–P12 spawn → P13 staging → P14–P16 verification.

Two blockers are now explicit:

1. **Camera AOT dispatch** — the camera hook has no generated-C anchor until the
   indirect bridge enters `CameraScroll_M0X0`.
2. **AOT timing** — P16 cannot be bit-identical at fixed frames until cycle
   accounting is aligned; until then, compare aligned frames.

## What this does not prove

- That 7.91% is enough for a native port; the wait loop remains interpreted.
- That the promoted code is cycle-identical to the interpreter.
- That any widescreen, culling, spawn or HUD behavior works.
- That the camera can be hooked until the indirect dispatch gap is fixed.
- That audio or gameplay is correct.

## Recommendation

Next change: **`uniracers-aot-dispatch`** (or a focused Phase D.1) to make the
interpreter's indirect-call bridge enter emitted AOT bodies, starting with
`CameraScroll`; add a replay check with the aligned-frame gate. In parallel,
decide whether AOT cycle accounting will be fixed or whether P16 permanently
uses aligned-frame comparison.

After that, the widescreen change can start with P16 → P5/P6 → P1–P4 +
P-U1/P-U2. The HDMA builder, cull/OAM and spawn/staging remain the missing
engine paths.

## Artifacts

- Workspace: `work/aot-promotion/` (gitignored).
- Project: `work/aot-promotion/project/UniracersSNESRecomp`.
- Baseline: `work/boot-spike/project/UniracersSNESRecomp`.
- Metrics: `metrics/baseline.json`, `metrics/promotion-input.json`,
  `metrics/promotion-manifest.json`, `metrics/promoted2-ingest.json`,
  `metrics/promotion-delta2.txt`, `metrics/frame-diff.txt`.
- Frames: `frames/promoted-2400.ppm`, `frames/promoted2-3000.ppm`,
  `frames/base-window/`, `frames/prom-window2/`.
- Coverage: `coverage2/uniracers/*.json`, `*.jsonl`.
- This report contains no generated C, no ROM bytes, and no game data.
