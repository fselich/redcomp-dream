## 1. Setting and menu

- [ ] 1.1 Add `RivalIndicator` (`[Widescreen]`, default `1`, env `SNESRECOMP_WS_RIVAL`, class `live`) to `ws_settings.c`/`.h`
- [ ] 1.2 Add the `RIVAL INDICATOR <ON/OFF>` row to `ws_menu.c` and its footer help
- [ ] 1.3 Reduce the menu row height so all twelve settings plus Save/Close fit above the footer
- [ ] 1.4 Check load/save: the key round-trips through `config.ini`, the menu toggles it live, and a locked env/local value is shown and not written

## 2. Geometry and presentation

- [ ] 2.1 Create `game/src/ws_rival.c`/`.h`: read `$0411/$0415`, `$0413/$0417`, `$0419/$041D`; compute player/rival screen points and the current visible rectangle
- [ ] 2.2 Implement the ray→border intersection, the 4 px inset and the eight-direction classification
- [ ] 2.3 Implement the Chebyshev distance buckets (`1.5×`, `4×` frame width) and the 1–3 inward dots
- [ ] 2.4 Pre-render the 16×16 RGB15 arrow/dot bitmaps for eight directions and three distance levels (red with dark outline)
- [ ] 2.5 Add the shared extra-object list so the radar and the indicator are both appended before one `PpuSetExtraObjects` call (slots 127 and 126)
- [ ] 2.6 Register the indicator in `GameDrawPpuFrame`, gate it on live 1P race / no netplay / setting on, and add the new files to `game/CMakeLists.txt`

## 3. Validation

- [ ] 3.1 Add a geometry unit test: eight directions, edge vs corner, inside-frame = no arrow, inset and zero/negative vectors
- [ ] 3.2 Verify the indicator is presentation-only: state trace, `.wram.bin`, `.vram.bin` and `.oam.bin` identical with the indicator on and off at the same frame
- [ ] 3.3 Capture 4:3, 16:9, 21:9 and 512 px on Dragster and Zoom Zoo; only the arrow/dots bounding box differs between on and off
- [ ] 3.4 Verify the dot buckets change with distance and that a rival visible in the 21:9 margin produces no arrow
- [ ] 3.5 Verify the original BG3 `<<<`/`>>>` arrows are untouched, the radar and indicator coexist, and 2P/netplay hide the indicator

## 4. Docs

- [ ] 4.1 Update `README.md`: the new `[Widescreen] RivalIndicator` key, the menu row, and that it coexists with the game's `<<<`/`>>>`
- [ ] 4.2 Update `docs/UNIRACERS_MEJORAS.md` C2: mark the first version in progress/done and point to this change
