## Context

Race layers in mode 1 (`BGMODE = 0x19`):

| Layer | Content | Width behaviour |
|---|---|---|
| BG1 | track, 16×16 tiles, 512-px ring | margins served by `source` from the `$7F` table at any width (`uniracers-ws-source`) |
| BG2 | background, map `bgXsc[1] = 0x73` (64×64 entries of 8×8 = 512×512 px) | wraps at 512 px; must be checked visually (static pattern vs streamed) |
| BG3 | HUD | elastic band in `WsApplyHud` with 43-px segment constants |
| OBJ | riders, CPU ghost, effects | game cull widened by `WS_CULL_MARGIN 43` on `$0421/$0423`; OAM X decode (below) |

Hard-coded 16:9 constants today:

- `src/main.c` `PrepareFrame`: `*frame_w = 342`.
- `src/game_rtl.c`: `WS_CULL_MARGIN 43`; HUD segments `{17,64,-39,8}` and
  `{193,256,236,299}` (`-39 = -43+4`, `299 = 256+43`).

Framework facts:

- `SnesDisplayAspect_ComputeAdaptiveFrame(native, height, max_width,
  target_aspect, display_aspect)` (`desktop/display_aspect.h`, reachable from
  `config.h`) returns `{width, extra}` with an even width and the configured
  pixel aspect (4:3 CRT = 7:6, 8:7 square = 1:1, 1:1 frame = 7:8).
- The host sets `g_ws_extra = (frame_w − 256) / 2` from `prepare_frame`
  (`desktop/host_main.c`). `widescreen.h` declares it.
- `PpuDecodeOamX` (`snes/ppu.c` ~l.2756): `x ≥ 256 + extraRightCur → x − 512`.
  Representable sprite X is therefore `[E − 256, 256 + E)`. The left margin
  needs `−E`, so it works only while `E ≤ 128`.
- `kPpuExtraLeftRight = 272` (PPU budget per side).

## Goals / Non-Goals

**Goals:**

- Race width from the target aspect, up to 512 px.
- Every margin-dependent policy derived from E.
- 16:9 output bit-identical to today when the target resolves to 16:9.

**Non-Goals:**

- Widths above 512 px (OAM aliasing; would need game-driven OAM left-hints).
- 2P split screen; menus (stay 256).
- A `config.ini` key (env override only, like other `SNESRECOMP_WS_*`).

## Decisions

### D1 — Width policy (`PrepareFrame`)

```text
if !(wide && live race): 256
target = SNESRECOMP_WS_ASPECT  ("W:H" | decimal | "auto"; default "auto")
auto   = max(16/9, drawable_w / drawable_h)     (16/9 when the size is unknown)
frame  = SnesDisplayAspect_ComputeAdaptiveFrame(256, 224, 512, target, g_config.display_aspect)
```

- Never narrower than 16:9, so windowed/4:3 users keep today's field.
- On a 16:9 screen with the default 4:3 CRT pixels: 16/9 → width 342, E = 43,
  as today.
- On 21:9: width 448, E = 96. At the 512 cap: E = 128 (≈ 2.67:1 with 7:6 pixels).

Alternative rejected: always following the window. A 4:3 or portrait window
would shrink the field below 16:9 and change today's behaviour.

### D2 — Margin E for game policies

`game_rtl.c` reads `g_ws_extra` (via `widescreen.h`) through a helper
`WsExtra()`, clamped to 0..128.

- **Cull:** `$0421 −= E`, `$0423 += E` (was 43).
- **HUD:** segments `{17, 64, 4−E, 51−E}` (left group, 1:1, 47 px wide) and
  `{193, 256, 193+E, 256+E}` (timer, 1:1).

### D3 — BG2

No code change is expected: a 512-px map shows no duplicate seam up to 512 px
if it is a uniform repeating pattern. If captures show a seam or stale
content at 21:9/24:9, record it and clamp BG2 (layer clamp) as the fallback.

## Risks / Trade-offs

- [BG2 not a pure pattern] → visual check; fallback BG2 clamp.
- [CPU rider/objects culled differently at large E] → the cull only gates
  drawing (`$0421/$0423` are read only by the five CMP sites, per the existing
  comment). Check that riders appear in the far margins.
- [HUD groups too far apart on 24:9] → acceptable; they sit at the frame edges
  as in 16:9.
- [Window resize mid-race changes E] → `PrepareFrame` runs per present; cull
  and HUD follow next frame.

## Migration Plan

Default `auto`: 16:9 screens see no change; wider screens widen automatically.
Rollback: `SNESRECOMP_WS_ASPECT=16:9`.
