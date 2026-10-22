# Uniracers widescreen HUD (Phase E6) — report

Date: 2026-10-20
Change: `uniracers-ws-hud`
Outcome: **P2c complete for the top HUD band.** The rigid `RACE`/`0/3` and timer
groups are copied 1:1 to the widened edges; the centre prompt, arrows and lap
time stay under the authentic clamp. 4:3 and HUD=0 are unchanged.

## Track identity correction

- **Dragster is the first track.**
- **Zoom Zoo is the second track.**
- The earlier route selected Dragster; the user's screenshots show Zoom Zoo.
- Both tracks were measured. Zoom Zoo uses `0/3`/`1/3`; Dragster uses `RACE`.
- The archived `uniracers-ws-spawn` N/A was measured on Dragster (first track)
  only. Zoom Zoo still needs a separate spawn/staging recheck; that is a future
  change, not a rewrite of the archived change.

## Measured native spans (256×224, frame 3500)

| Track | Group | Rows | Columns |
|---|---|---|---|
| Dragster | `RACE` | 19–29 | 17–46 |
| Dragster | timer | 19–29 | 193–238 |
| Dragster | prompt | 83–93 | 65–174 |
| Dragster | arrows | 112–126 | 208–231 |
| Dragster | lap time | 163–173 | 105–158 |
| Zoom Zoo | `0/3` | 17–30 | 17–38 |
| Zoom Zoo | timer | 17–30 | 193–238 |
| Zoom Zoo | arrows | 112–126 | 40–63 |
| Zoom Zoo | lap time | 163–173 | 105–158 |

The top band has a large transparent gap; it is not a continuous bar.

## Policy

BG3 top band rows 17–31 uses elastic slot 0 with two 1:1 rigid segments:

```c
{ src 17..64,  dst -39..8   }   /* left group + 4 px inset */
{ src 193..256, dst 236..299 }  /* timer + right inset */
```

`PpuSetWidescreenBg3Widen(ppu, 17)` is required so the elastic band is
consulted for BG3. Rows 31–224 keep `PpuSetWidescreenLayerClampBand`, so the
prompt, arrows and lap time remain centred.

The plain anchor band was rejected: it shifts `[0,left_end)`, which clipped the
left glyphs. The elastic rigid segments preserve the glyph run exactly.
`SNESRECOMP_WS_HUD=0` clears the band and restores the layer-wide clamp.

## Results

| Frame | Wide, HUD on | Wide, HUD off | Native |
|---|---|---|---|
| Zoom Zoo 3500 | `0/3` at 4 px, timer at 278–324 | `0/3` at 60, timer at 236–281 | authentic 256 |
| Dragster 3500 | `RACE` at 4 px, timer at 278–324 | clamp | authentic 256 |

- Glyphs are byte-identical 1:1 segments; no resampling.
- The middle gap shows the world; no lower-layer hole.
- A first attempt with the left edge at x0 clipped the R of `RACE`; adding the
  4 px inset fixed it. This is recorded in task 6.4.
- `SNESRECOMP_WS_HUD` is independent of `SNESRECOMP_WS_CULL`.
- WS-off 256×224 frame is unchanged.

Hashes: `metrics/hud-final-hashes.txt`.

## Artifacts

- `work/ws-hud/hud-layout.md` — measured layouts and policy.
- `frames/zoomzoo-bg3-native/`, `frames/zoomzoo-bg3-wide/`
- `frames/zoomzoo2/` — PICK TRACK / NOW PLAYING Zoom Zoo confirmation.
- `frames/hud-test/` — policy on/off, Dragster, Zoom Zoo, native.
- `scripts/route-zoomzoo.txt` — selects Zoom Zoo (Down on PICK TRACK).
- `metrics/hud-spans.txt`, `metrics/hud-final-hashes.txt`.

No generated C, frames or ROM data are in the repository; all derived output
stays under the gitignored `work/ws-hud/`.

## Next

1. Re-check P9–P13 on **Zoom Zoo** (the archived N/A was Dragster-only) as a
   separate change.
2. Final polish/validation across tracks.
