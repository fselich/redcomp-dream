# uniracers-ws-ultrawide — report

Date: 2026-10-21
Status: **done, go**. The race view widens with the screen up to 512 px.
16:9 is unchanged.

## 1. Change

Patch: `work/ws-ultrawide/patches/ws-ultrawide.diff`
(pre-change files `main.c.before`, `game_rtl.c.before`).

- **`src/main.c` `PrepareFrame`:**
  - target aspect = `SNESRECOMP_WS_ASPECT` (`W:H`, decimal or `auto`), default
    `auto` = max(16:9, window aspect);
  - width from `SnesDisplayAspect_ComputeAdaptiveFrame(256, 224, 512, target,
    DisplayAspect)`;
  - outside a live widescreen race, 256.
- **`src/game_rtl.c`:**
  - `WsExtra()` = `g_ws_extra` clamped to 0..128;
  - object cull margin = E (was the constant 43);
  - HUD elastic segments `{17,64,4−E,51−E}` and `{193,256,193+E,256+E}`
    (identical to the old constants at E = 43).

Why 512 px: `PpuDecodeOamX` maps 9-bit OAM X into `[E−256, 256+E)`. The left
margin `−E` stays representable only while E ≤ 128.

## 2. Results (Dragster 2500 unless noted; `work/ws-ultrawide/`)

| Run | Width | SHA-256 (16) | CPU s / 2,500 frames |
|---|---:|---|---:|
| native (WS off) | 256 | `3417f91afc8f6d47` (unchanged) | 15.20 |
| `ASPECT=16:9` | 342 | `4b138431bdc04382` (= pre-change 16:9 default) | 15.76 |
| `auto`, headless | 342 | `4b138431bdc04382` | 15.33 |
| `ASPECT=21:9` | 448 | `b1faaddcc0c42f66` | 15.30 |
| `ASPECT=32:9` (capped) | 512 | `a3c5765032a515c7` | 15.67 |

Cost does not change measurably with width.

BG1 probe (default `source` policy), `logs/validation.txt`:

| Track | 21:9 wrong / masked | 512 px wrong / masked |
|---|---|---|
| Dragster 2300–2500 | 0 / 0 | 0 / 0 |
| Zoom Zoo 3300–3505 | 0 / 0 | 0 / 0 |
| Monster 2000–2300 | 0 / 0 | 0 / 0 |

Visual review (`frames/*-21x9-*.png`, `frames/*-32x9-*.png`, `ws512-2500.png`):

- track continuous to both edges;
- BG2 background continuous (its 512-px map has no visible seam up to 512 px);
- HUD groups at the frame edges, unscaled;
- riders and objects appear in the wide margins (Zoom Zoo 512: a rider entering
  at the top left).

## 3. Residual risks

- **Sprites:** no ground-truth oracle like the BG1 probe exists. Aliasing is
  excluded by construction for objects that pass the game's `$0421/$0423`
  cull, whose window is now exactly `[−E, 256+E)`. Objects drawn outside that
  cull (effects, markers) are only checked visually.
- **Windowed play:** on a narrower-than-16:9 window, `auto` keeps 16:9
  (letterboxed), as before.
- **Above 512 px** (e.g. 32:9 monitors): the field stops at 512 px and the
  presenter pillarboxes the rest. Going further needs game-driven OAM
  left-hints (`PpuWsSetOamLeftHints`) from object positions; that is a separate
  change.
