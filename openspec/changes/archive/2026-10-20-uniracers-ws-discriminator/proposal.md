# Proposal: Uniracers live-race discriminator (Phase E2)

## Why

The 16:9 preview works, but the wide mode is gated only by a developer flag, so
menus and transitions widen too. `WIDESCREEN_PATTERNS.md` P5/P6 requires a
proven "live race" discriminator before backgrounds, HUD and objects are built;
an HDMAEN or PPU mirror is explicitly not enough. This change finds, verifies
and wires that discriminator so wide mode is active only during a live race and
4:3 everywhere else.

## What Changes

- Capture WRAM dumps and traces for menu, live race and pause.
- Search for a byte/word that is stable in live race and different in menus,
  transitions and pause; exclude PPU/HDMA mirrors.
- Verify the candidate across states with the debug server or interpreter
  traces, including the pause path.
- Wire the gate into `PrepareFrame`/`GameDrawPpuFrame`: 342×224 only when the
  wide flag is on **and** the game is in live race; 4:3 otherwise.
- Verify menu and pause stay 4:3, race is 16:9, and simulation is unchanged.
- Report the address, values, verification and residual risk.

## Non-goals

- HDMA builder/P-U1, cull/OAM, spawn/staging, final HUD.
- Changing simulation, collision, RNG or input.
- Player-facing widescreen polish.

## Capabilities

### New Capabilities

- `recomp-ws-discriminator`: a verified live-gameplay gate for widescreen mode,
  proven across menu, race and pause.

### Modified Capabilities

None.

## Impact

- Work under `work/ws-discriminator/`; the `ws-spike` and baseline workspaces are
  preserved.
- Changes the project host gate and reuses the existing wide-mode flag.
- 4:3 remains the default outside live race.
- ROM-derived output stays in `work/`.
