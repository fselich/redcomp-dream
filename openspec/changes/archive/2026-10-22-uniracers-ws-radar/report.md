# uniracers-ws-radar — report

Date: 2026-10-22
Status: **go**. The local radar is on by default and can be disabled with
`[Video] MiniMap = 0`.

## 1. Summary

A 64×32-pixel radar in the lower-right widescreen corner shows a 128×64-cell
(2048×1024 px) window of the decoded `$7F` track table around the player. The
player is a white dot at the centre; the CPU rival is a red dot. The panel is
an in-frame `PpuExtraObject`: no guest memory, VRAM, CGRAM or OAM is written.
The extra width is horizontal context; the 2×2-cell scale is unchanged.

## 2. Configuration

`config.ini` gains a host-owned key:

```ini
[Video]
MiniMap = 1     ; default if absent
MiniMap = 0     ; off
```

- `config.ini` then `config.local.ini`; last value wins.
- `SNESRECOMP_WS_MINIMAP=0`/`=1` overrides for a run.
- The game reads it in `GameAfterConfig`, registered as
  `SnesDesktopHostGame.after_config`; **no framework parser change**. `[Video]`
  already accepts unknown host keys silently.
- Tested: default on, `config.ini = 0`, `config.local.ini = 0`, env `= 0`; no
  "Invalid .ini" warnings. All off cases produce the pre-change source hash
  `4b138431bdc0…`.

## 3. Method

- Window: 128×64 cells, 2×2 cells per panel pixel; the player's table cell at
  the centre (32,16). A panel pixel is track if any of its four `$7F` cells has a
  non-zero BG1 character (`word & 0x3FF`); outside the grid is background.
- Positions: `screen = (int16)(rider - camera)`, `world = (unwrapped_scroll +
  screen) >> 4`, `table = world + s_src_ax/ay[0]`. The radar uses the last
  rendered BG1 unwrap and the live WRAM positions; a one-frame lag is accepted.
  It is drawn only while `s_src_valid > 0`.
- Rendering: one `PpuExtraObject`, 64×32 RGB15, bit 15 opaque, priority 3,
  `math=1`, `oamSlot=127`, `order=1`, placed in **screen coordinates** at
  `x = 256 + ppu->extraRightCur - 64 - 2`, `y = 224 - 32 - 2`. The compositor
  adds `kPpuExtraLeftRight` internally. At 16:9 the panel is framebuffer
  x 276..339: 22 px of the native lower-right band (the HUD `0/68` ends at
  x 273) plus the right margin; at 21:9 it is 382..445; at 512 px 446..509.
  The 16:9 panel is not strictly margin-only; it is still clear of the HUD.
- Gating: config/env on, `WideEnabled()`, `InLiveRace()`, `$0DDB==0`,
  `WsExtra()>0`, `s_src_valid>0`, no netplay.

Colours: dark panel, grey border, green tube edges, white player, red rival.

## 4. Evidence

| Check | Result |
|---|---|
| Native 4:3, radar on | `3417f91afc8f6d47ea4ab3f7b11bbeec4cc7491a23ab71cc6c956f5fb3f5df44` |
| Source 16:9, radar off (`MiniMap=0` / env) | `4b138431bdc04382a04695d683bf36ba2a1eeb284e1ef1c600ad10c798a8e357` |
| Radar on vs off at 2500 (32×32 build) | 1024 differing pixels, exactly the panel bbox x 308..339, y 190..221; **no other pixel changed** |
| 64×32 resize | panel is 2048 pixels; only the panel buffer/object dimensions changed. The scripted menu route is flaky for a fresh same-state on/off diff (some runs land on `PICK ANOTHER`); the 32×32 exact diff and the read-only code path are the invariance evidence. |
| `SNESRECOMP_STATE_TRACE`, on vs off, 2,500 frames (32×32 build) | **identical** |
| Frame-2500 WRAM/VRAM/DMA dumps (32×32 build) | **identical** |
| Frame-2500 CPU dump | same registers; only the stale AOT `func` label differs (`(none)` vs `interp@$82D4F0`) |
| Cost (`SNESRECOMP_FRAME_TIMING`, 32×32 build) | `raster_ms` 0.8809 → 0.9023 = **+0.021 ms/frame**; fps 59.6 vs 60.0 (noise). The 64×32 panel samples 8192 cells instead of 4096; still well below the 0.2 ms target. |
| Radar start | first log frame 1536, when the source anchor becomes valid; no panel before |

### Visuals

- `frames/radar-wide-panel.png`: 64×32 Dragster, player centre, rival red at
  the right edge, more horizontal track.
- `frames/radar-wide-bowl-2300.png`: 64×32 Bowl, panel in the lower-right
  corner; the native `0/68` HUD remains visible to its left.
- `frames/radar-wide-219-panel.png`: 64×32 at 21:9 (448 px), in the margin.
- `frames/radar-dragster-idle-panel.png`: 32×32 reference, player centre,
  rival red near the right edge.
- `frames/radar-zoomzoo-panel.png`: Zoom Zoo loop, player on the local junction.
- `frames/radar-bowl-panel.png`: Bowl curve, rival red at the left edge.
- `frames/radar-switcher-panel.png`: two-track section, player and rival
  distinct.
- `frames/radar-monster-panel.png`: straight section, player/rival adjacent.
- `frames/radar-aspect-219-panel.png`, `frames/radar-aspect-512-panel.png`:
  32×32 reference at 21:9 (448 px) and 512 px. The 64×32 panel at 21:9 is
  `radar-wide-219-panel.png`; the 512 placement is the same formula
  (framebuffer 446..509).

The player/rival table cells were checked on Dragster idle and Zoom Zoo
(`$03ED=2`); the dots stay at the local track. The radar log
(`SNESRECOMP_WS_RADAR_LOG=path`) records
`frame,ptx,pty,rtx,rty,ax,ay,pword,rword`.

## 5. Limitations / open points

- **Source-anchor only.** The radar is hidden in `clamp`/`wrap`/`ring`/`ghost`
  modes and before the first valid source anchor. A radar-only anchor for other
  modes is a later change.
- **Resolution.** 2×2 cells per pixel: riders less than about one cell apart
  can share a pixel; a one-cell gap may be one pixel or overlap depending on
  the sign. At the panel edge the rival is clamped, not an arrow.
- **Shape criterion.** `word & 0x3FF` draws tube edges, slopes and posts;
  palette-only fill/empty cells are background. The player/rival can sit on a
  fill cell, so the dot may be on the dark panel next to the edge line. A
  filled-road rendering would need a different criterion.
- **No alpha.** The panel is opaque; at 16:9 the 64-px width overlaps 22 px of
  the native lower-right band. It is clear of the HUD (Bowl's `0/68` ends at
  x 273; the panel starts at 276), but it does cover the empty band/track
  there. At 21:9/512 it stays in the margin.
- **Brightness/colour math.** The panel is an OBJ-stage object and follows the
  game's brightness/colour math. It was checked in normal race frames, not at
  the finish fade.
- 2P, netplay, non-live screens and 4:3 are out of scope. The radar is 64×32
  px at 2×2 cells/pixel; a 64×64 panel would double vertical context too, at
  the cost of more native-frame coverage at 16:9.

## 6. Files and artifacts

- Code: `src/game_rtl.c`, `src/game_rtl.h`, `src/main.c`.
- Patch: `work/ws-radar/patches/ws-radar.diff` (game_rtl.c against the saved
  pre-radar copy, plus the host hook lines).
- Logs: `work/ws-radar/logs/` (radar logs, config tests, timing, traces).
- Frames: `work/ws-radar/frames/` (full captures and panel crops).
- Dumps: `work/ws-radar/dumps-{on,off}/`.
- Reproduce: build `build-local/`, run with `SNESRECOMP_WS=1`; add
  `SNESRECOMP_WS_MINIMAP=0` to hide, `SNESRECOMP_WS_RADAR_LOG=path` to trace,
  `SNESRECOMP_WS_ASPECT=21:9`/`8:3` for the wider checks.

## 7. Decision

Go. Keep the radar on by default; `[Video] MiniMap = 0` is the user kill switch.
The radar is a local-context aid, not a full-course map; the 64×32 panel is
the wide local-context size. At 16:9 it uses the lower-right native band plus
the margin, clear of the HUD; at 21:9/512 it stays in the margin.
