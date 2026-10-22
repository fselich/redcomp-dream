# Non-horizontal track margins (Phase E8) — report

Date: 2026-10-20
Change: `uniracers-ws-track-margins`
Outcome: **Safe clamp is the default.** The disconnected fragments are gone.
A shadow-history `fill` experiment is implemented behind
`SNESRECOMP_WS_TRACK=fill`: it is promising on some slopes (frame 4000) but still
produces disconnected pieces on loops (frame 5000), so fill is **not accepted**
and clamp remains the safe mode.

## Reproduction

Zoom Zoo, race start, hold `right`:

- native 4:3 at frame 5000: continuous S-curve;
- wide 16:9 at frame 5000: same S-curve plus a detached blue/green segment at
  the right edge;
- BG1-only at 5000: the detached segment is in the **track layer**;
- PPU: BG1 hScroll 776, `bgXsc[0]=0x0C` = 32×32 (256×256) tilemap.

The extra 43 px margins sample the wrapped 256-wide map, so they show another
track section, not a continuation.

## DMA/streaming

Frame 5000 has 29 PPU DMA transfers, mostly 32-byte chunks to `$2118`
(`0x6020..0x6cd0`), plus two small writes. There is no retained buffer for the
true off-native columns in VRAM or the low WRAM window; the map is a 256-wide
ring.

## Fix

BG1 is clamped to the native 256 columns when widescreen is active in a live
race:

```c
PpuSetWidescreenLayerClamp(ppu, 1u << 0);
```

`SNESRECOMP_WS_TRACK` selects:

- default/`clamp`: safe native track, background fills the margins;
- `off`: pre-change natural sampling, reproduces the fragment for comparison;
- `fill`: reserved; currently falls back to clamp because no true source exists.

HUD and cull switches remain independent.

## Evidence

| Frame | Native 4:3 | Wide clamp |
|---|---|---|
| 4000 | continuous curve | continuous curve, background margins |
| 5000 | continuous S-curve | continuous S-curve, background margins |

- `frames/native-5000.png` — native continuous.
- `frames/wide-5000.png` — old wide fragment.
- `frames/bg1-5000.png` — fragment isolated to BG1.
- `frames/clamp-final-4000.png`, `frames/clamp-final-5000.png` — clamp result.

## Fill experiment (partial)

`SNESRECOMP_WS_TRACK=fill` configures the framework's `ws_shadow` layer 0 with:

- world key from WRAM `$041D/$041F`, scroll from the PPU;
- retain history, west/east keep 8, capture 32, reject east echo;
- `WsShadowExtendEdges` for missing margin columns.

Results:

- frame 4000: track continues through the margins, no obvious wrap fragment;
- frame 5000: the S-curve/loop still shows disconnected pieces; the edge
  extension copies a track-edge column into a separate horizontal piece.

So fill is an experiment, not a release mode. It needs either a true ROM
stream/level prefetch or a more precise history/edge policy before it can
replace clamp. Artifacts: `frames/fill-4000/5000.png`, `fill2-*`, `fill3-*`.

## Limitation

The track does not extend into the 43 px margins; it remains a native 256-wide
track centered in a wide background. This is honest to the engine's 256×256 BG1
map. True 16:9 track coverage requires a stream/level-data prefetch and is not
claimed here.

## Decision

**Clamp accepted** as the safe fix for the broken fragments; **fill experimental**. The old validation
result is corrected: non-horizontal tracks are no longer accepted as natural
wide sampling.

## Next

Optional: decode the BG1 stream source and replace clamp with a true margin
fill. Otherwise, final per-track validation can proceed with the clamp
limitation recorded.
