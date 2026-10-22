# 1P track matrix — selection and confirmation

Date: 2026-10-20
Change: `uniracers-ws-validation`

The PICK TRACK screen header `CRAWLER` is the league/heading, not a track. The
five 1P tracks are, in order:

| # | Track | Down presses from default | NOW PLAYING text | Evidence |
|---|---|---|---|---|
| 1 | Dragster | 0 | `RACING ON DRAGSTER` | `frames/tracks/dragster.png` |
| 2 | Zoom Zoo | 1 | `OVER 3 LAPS ON ZOOM ZOO` | `frames/tracks/zoomzoo.png` |
| 3 | Bowl | 2 | `DOING STUNTS ON BOWL` | `frames/tracks/bowl.png` |
| 4 | Switcher | 3 | `RACING ON SWITCHER` | `frames/tracks/switcher.png` |
| 5 | Monster | 4 | `OVER 3 LAPS ON MONSTER` | `frames/tracks/monster.png` |

Selection method: stop on PICK TRACK (`route-stoptrack.txt`), tap Down the
indicated number of times, tap B, then read NOW PLAYING. The debug controller
tap is reliable; a single scripted `press down` can be missed by the menu
timing, so use the debug path for validation runs.

All five tracks are reachable in 1P. None is unreachable.
