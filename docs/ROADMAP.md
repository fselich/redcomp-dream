# Roadmap

Open work, grouped by purpose. Effort is low / medium / high. Unless marked, an item is presentation-only and does not touch the game's simulation. Finished items are not listed; their history is in `openspec/changes/archive/`.

## Reliability

| # | Item | Effort | Goal |
|---|---|---|---|
| R1 | Validate all 45 tracks | medium | A menu route that reaches any track (tour and level selectors), the BG1 wrong-cell score and the rider OAM log per track, and a track × width report (wrong cells, masked cells, anchor drops, OAM fixes). The bugs found so far were all on tracks or widths nobody had tested: zoomed tracks (`$03ED ≠ 0`), tours after the first, the X-circular grid, widths above 16:9. An unlocked test save (`srm.py unlock`) gives access to every tour. The Hopper PICK TRACK transition and first usable-frame anchor are covered by `uniracers-ws-hopper-start`; the rest of the later tours still need the probe. |
| R2 | Width matrix | low | Run every validation at 16:9, 21:9, 2.39:1 and 512 px. |
| R3 | Diagnostic dump hotkey | medium | One key writes the last ~100 s of history (riders, camera, OAM, `$03ED`, race state, source anchor) to a file, so a rare glitch can be analysed without reproducing it live. Today the only tool is `tools/scripts/ws-ghost-xhigh/gate/grab.py`, which needs the game running with the debug server. |
| R4 | Live telemetry overlay | low/medium | Toggleable OSD with scroll, camera, anchor state, `$03ED`, cull window, OAM fixes and frame time. |
| R5 | Input movie recorder | medium | Record SRAM, inputs and periodic hashes to replay a bug exactly or share a run. |
| R6 | Local CI matrix | low/medium | R1 × R2 as one script with recorded hashes; the ROM stays outside the repository. |

## Widescreen

| # | Item | Effort | Note |
|---|---|---|---|
| W1 | Rival appears for a moment where it should not (tour 2, track 1) | — | Parked until R3 can capture it; hypotheses in `openspec/changes/archive/2026-10-22-uniracers-ws-ghost-xhigh/report.md`. |
| W2 | Wider than 512 px (up to the renderer limit, about 800) | medium | Needs the sprite side decided from the WRAM position (`PpuWsSetOamLeftHints`) instead of the 9-bit X; check first how a repeated BG2 (its map is 512 px wide) looks. |
| W3 | The start countdown can show at 4:3 for a moment | low | Seen during the race-state work; not investigated. |
| W4 | Two-player split screen | high | Done in OpenSpec change `uniracers-ws-two-player` (pending archive): 2P and VS widen each half as in 1P, with per-viewport BG1, HUD and rider OAM; radar excluded. Side-by-side composition remains a separate future change. |
| W5 | Menu-to-race transition | low/medium | The frame jumps from 256 to 342+ px on race entry: animate the width, or keep the width in menus with a decorative border. |
| W6 | Asymmetric margin toward the direction of travel | medium | Shift the wide crop so more pixels lie ahead, with a smooth transition on direction changes; the game camera is untouched. Must be switchable (motion sickness). |

## Gameplay and quality of life

| # | Item | Effort | Note |
|---|---|---|---|
| G1 | Off-screen rival indicator | low/medium | In progress: OpenSpec change `uniracers-rival-edge-indicator`. |
| G2 | Per-track times and splits | low | Best time, best lap and splits per track in a file beside the executable; in race, the delta to your best at each split. |
| G3 | Ghost of your best lap | medium | Record the player's position, pose and sprite each frame of the best run and draw it translucent with `PpuSetExtraObjects`. Pairs with G2. |
| G4 | Quick restart and practice | low | One key restarts the race (an automatic state at the start line), another repeats a section; check what the framework's rewind already covers. |
| G5 | Lower latency | low | `RunAhead` exists in the host config and is 0; validate that 1–2 frames keep the margin policy and the OAM fixes correct, then recommend it. |
| G6 | Extended HUD | low/medium | Speed, splits, delta to best, lap position. |
| G7 | Track gallery | low/medium | Shape, index, name and record of all 45 tracks from the decoded table. |
| G8 | Progress bar for long tracks | low/medium | A 128×16 strip with start, finish, player and rival for the 1024×16 tracks, where the local radar says little. |
| G9 | Radar zoom/orientation and best-lap ghost marker | medium | Builds on the radar and G3. |
| G10 | Jukebox and sound test | low/medium | Play the track music and effects. |
| G11 | Save-data manager | low | Backup, restore, import and export of saves and per-track states. |
| G12 | Practice checkpoints | medium/high | Restart from a corner, no rival, endless laps. Touches game state through save states. |

## Presentation

| # | Item | Effort | Note |
|---|---|---|---|
| P1 | Shader presets | low | The host already loads libretro-style GLSL presets (`OutputMethod = OpenGL`, `Shader = path.glslp`). Recommended order: `sharp-bilinear`/`pixellate` (integer scale with smoothed remainder; removes the column shimmer that non-integer nearest-neighbour scaling causes during fast scrolling), then a light CRT (`crt-lottes`, `crt-easymode`) without curvature. Avoid `xBRZ`/`ScaleFX` on the whole image: they smear the pre-rendered sprites. OpenGL output has never been tested with widescreen. |
| P2 | Per-layer shaders | medium | Let the renderer export a per-pixel layer mask so a custom shader can smooth or light only BG1 (the tubes) and leave the riders alone. Alternative: high-resolution replacement tiles for the track pieces. |
| P3 | 120/144 Hz motion interpolation | high | Draw-only interpolation between game frames: BG1/BG2 scroll and the two riders' positions, everything else from frame N. Costs one frame of latency (combine with G5), must skip camera jumps, and doubles the drawing cost, which has to be measured. Cheaper alternative: black frame insertion at 120 Hz (sharper motion, no extra smoothness). |
| P4 | HUD placement choice | low | HUD in the corners of the wide frame (current) or in its native place. |
| P5 | Palettes and accessibility presets | low | Colour-blind palettes, rider skins, and a "reduce motion" preset (no W6, no P3). |

## Ambitious

| # | Item | Effort | Note |
|---|---|---|---|
| X1 | Online 2P, leaderboards, shared ghosts | high | Depends on netplay and rollback being validated for this title. |
| X2 | Photo mode and replay theatre | medium (free camera: high) | Freeze, hide the HUD, zoom, capture; replay a recorded race. |
| X3 | Track editor or randomizer | high | Uses the decoded track table; needs the collision data, which is not decoded (see [`formats/TRACK_FORMAT.md`](formats/TRACK_FORMAT.md)). Touches game data. |

## Suggested order

1. R1 + R2: validate every track at several widths without touching the player's save.
2. R3 + R4 + R5: make rare bugs such as W1 reproducible.
3. G1 (in progress), then G2, G3 and W6.
4. The large items: W2, W4, P3.
