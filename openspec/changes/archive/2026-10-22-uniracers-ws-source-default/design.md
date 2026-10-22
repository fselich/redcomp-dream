## Context

Current selection (`src/game_rtl.c`):

```text
WsOffwindowMode(): env SNESRECOMP_WS_OFFWINDOW  unset/0 -> 0, ring -> 1, source -> 2, ghost -> 3
GameDrawPpuFrame:  ow_mode = mode if (wide && live race) else 0
                   if !ow_mode && !repeat && getenv(SNESRECOMP_WS_TRACK_CLAMP): BG1 clamp
                   else (mode 0, no env): BG1 draws the raw 512-px ring in the margins (wrap)
```

Validated behaviour of `source`: `openspec/changes/uniracers-ws-source/report.md`.

## Goals / Non-Goals

**Goals:** normal play gets `source`; every earlier policy stays reachable by
one switch; no behaviour change with widescreen off.

**Non-Goals:** a `config.ini` key (env only, like the other `SNESRECOMP_WS_*`
switches); ultrawide (separate change `uniracers-ws-ultrawide`).

## Decisions

### D1 — Mode table

| `SNESRECOMP_WS_OFFWINDOW` | Mode | BG1 margins |
|---|---|---|
| unset / empty, `SNESRECOMP_WS_TRACK_CLAMP` unset | `source` (2) | table cells, ring-verified first, mask until anchored |
| unset / empty, `SNESRECOMP_WS_TRACK_CLAMP` set | clamp (4) | global BG1 clamp (compatibility) |
| `source` | 2 | as above |
| `ring` | 1 | verified ring cells, mask otherwise |
| `ghost` | 3 | ring + speculative lane (parked) |
| `clamp` | 4 | global BG1 clamp |
| `wrap`, `off`, `0` | 0 | raw ring (the old pre-fix look) |
| anything else | 2 | default, with one stderr warning |

Mode 4 is new: it is the old clamp, now selectable by name.
`SNESRECOMP_WS_TRACK_REPEAT` still wins for mode 0 only, as before.

### D2 — Logs

The `[ws-ow] active/inactive/scene reset` lines go to stderr only when
`SNESRECOMP_WS_OFFWINDOW_LOG` or `SNESRECOMP_WS_SOURCE_LOG` is set, so
default play stays quiet.

## Risks / Trade-offs

- [The ~100-frame race-start wait shows masked margins] → the native view shows
  no track then either; accepted (`uniracers-ws-source` report §2).
- [An unvalidated track behaves differently] → the per-frame self-check masks on
  disagreement; worst case is `ring` behaviour.

## Migration Plan

Default flip only. Rollback: `SNESRECOMP_WS_OFFWINDOW=clamp` (or `wrap`).
