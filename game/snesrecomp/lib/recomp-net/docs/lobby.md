# Lobby protocol (client-facing)

`recomp-net` is a delay-sync library only — it does **not** include a lobby
server. The open-source control plane lives in the sibling repo
[`recomp-net-server`](https://github.com/RetroPortingToolKit/recomp-net-server).

Authoritative wire documentation for the MotK / psxrecomp WebSocket JSON
protocol:

- https://github.com/RetroPortingToolKit/recomp-net-server/blob/main/docs/WS_LOBBY.md
- Architecture: https://github.com/RetroPortingToolKit/recomp-net-server/blob/main/docs/HOW_IT_WORKS.md

Default client URL: `ws://netplay.retcomm.net:8765`  
Override with env `RNET_LOBBY_URL`. An engine may register its older spelling
as an alias (`RNetLobbyConfig.legacy_env_prefix`): snesrecomp registers
`SNES_NET_`, so `SNES_NET_LOBBY_URL` keeps working; psxrecomp's own client
still reads `PSX_NET_LOBBY_URL`.
Local bring-up: `ws://127.0.0.1:8765` (run `recomp-net-server` yourself).

## The client: `recomp_net/lobby_client.h`

The protocol client lives here (`src/lobby/rnet_lobby_client.c`), lifted from
snesrecomp's `snes_lobby_client.c` so every engine links one copy instead of
forking it (psxrecomp's `psx_lobby_client.c` is the remaining fork; see
"Engines still carrying a fork" below). It speaks every op of `WS_LOBBY.md`
and `AUTOMATCH.md` the SNES client spoke, plus the peer-to-peer mod transfer
that rides the seated `signal` relay.

What stayed engine-specific became configuration:

| Was hard-coded | Now |
| --- | --- |
| `SNES_GAME_VERSION` fallback | `rnet_lobby_set_game_identity` / `RNetLobbyConfig.game_version`; `"dev"` when unset |
| `SNES_NET_LOBBY_URL`, `SNES_NET_GAME_VERSION` | `RNET_LOBBY_URL`, `RNET_LOBBY_GAME_VERSION`, plus `legacy_env_prefix` aliases |
| player seat ceiling 4 | `RNetLobbyConfig.max_players` (array size 8) |
| automatch `max_slots:2` | `RNetLobbyConfig.automatch_slots` (default 2) |
| chat-report `platform:"snes"` | `RNetLobbyConfig.platform` |
| widescreen keys in `match_caps` | `RNetLobbyMatchCaps.ext` + `RNetLobbyCapsCodec` (engine writes/parses its own keys) |

`RNetLobbyMatchCaps` carries only the keys every engine shares:
`input_delay`, `input_prediction` (sent only when the host set it),
`rollback`, `force_turn`, `force_input_relay`, the mod plan (`mod_plan`),
the effective mod set (`mod_set`) and the cosmetic grant
(`mod_cosmetic_allow`). Spectators are a `create` option
(`rnet_lobby_set_allow_spectators`), not a caps key.

**Outbound writes are frame-atomic** (snesrecomp#104, fixed here
2026-09-25). The lobby socket is non-blocking; `rnet_ws_write_text` used to
give up on would-block after part of a frame had gone, its callers ignored
that, and the next frame followed the fragment -- the server then parsed
payload as headers. After the handshake every frame now goes through an
`RNetWsTx` (`recomp_net/rnet_ws.h`): appended whole, flushed in order, the
unsent tail (including the rest of a half-sent frame) kept for the next
`rnet_lobby_pump`. The client stays connected across would-block and
disconnects only on a hard socket error or a backlog past
`RNET_WS_TX_CAP_DEFAULT` (256 KiB: a server that has stopped reading), with a
log line naming which. The fixed 8-slot queue now only holds frames written
before the handshake. `lobby_ws_backlog_test` forces would-block mid-frame on
a socketpair and checks the byte stream is exactly the queued frames; the
same socket driven the old way breaks after 6 of 240 frames.
`rnet_ws_write_text` remains for blocking sockets only.

The launcher-facing half -- `RecompLauncherCNetplayCallbacks` over this client
and the LAN modules -- is recomp-ui's optional `recomp_launcher_netplay`
module (`src/netplay/recomp_netplay_host.h`), not part of this library.

### Engines still carrying a fork

psxrecomp `runtime/src/psx_lobby_client.c` (common ancestor psx 31015cea). It
has what this client lacks -- waiting-room RTT over the ICE path
(`ice_rtt`), `path_report`, `set_host_endpoint` / STUN, an asynchronous
connect -- and lacks what this client has (mod plan/transfer,
`desync_report`). Folding it in means porting those four features here first,
then deleting the fork.

Host `match_caps` (opaque JSON on create/start) are echoed to guests so
sim-affecting settings stay aligned; see the server `WS_LOBBY.md`.

Lobbies also carry `game_name` + `game_version` (release pin). Create/join
must match; `list` can filter by either. Empty version normalizes to `dev`.

## Summary (for host integrators)

- One WebSocket per player; text frames are JSON objects with an `"op"` field.
- Server assigns `player_id` on connect (`welcome`).
- Hosts `create` lobbies; guests `list` / `join` (optional password).
- Server returns `session_id`, slot map, and rewritten `host_endpoint` /
  `guest_endpoint` for LAN (or ICE `signal` relay).
- After handoff, peers use **this** library (`rnet_session_*`) for INPUT
  exchange. The lobby server is not on the input path.

Open-source clients (this library's `rnet_lobby_client`, psxrecomp's
`psx_lobby_client`) implement the protocol; they do not embed the server.

## Local LAN room registry

`recomp_net/lan_lobby.h` provides the small, server-independent room registry
used by launchers running multiple instances on one machine. A LAN/Direct IP
host publishes an `RNetLanLobby` beside its configuration; launchers may merge
that row with the remote WebSocket list for discovery. Online hosts publish
only to the lobby server and must not dual-publish here (join/member/start
handling differs per channel). Joining claims the guest slot but does not
start the game; both launchers continue showing their shared room until the
host calls `rnet_lan_lobby_set_started()`.

Host kick clears the guest seat via `rnet_lan_lobby_kick()` while keeping the
room published; the joiner drops out when the registry no longer lists them.

Lobby create port policy is owned by **recomp-ui** (`launcher_udp_port.*`)
before `create()`: LAN/Direct IP requires the exact UDP port; online scans
preferred..preferred+31 and rewrites the endpoint. `rnet_udp_port_available` /
`rnet_udp_find_free_port` remain available for hosts that need the same probe
outside the launcher.

The registry does not carry input or replace `rnet_session_start_lan()`.

### Direct IP waiting room and the rematch

`recomp_net/lan_direct.h` is the cross-machine seat claim (`RNETDJ1` datagrams
on the host's game port). A launch closes both ends' sockets -- the game
session takes the port -- so a rematch after a soft return re-seats the guest
from scratch: the host re-opens with the joiner seat freed, and the guest asks
again with `rnet_lan_direct_guest_join_begin` + `rnet_lan_direct_guest_join_poll`,
the non-blocking form of `rnet_lan_direct_guest_join` (which is built on it).
`join_poll` re-sends `JOIN_REQ` every 400 ms while nobody answers, so a guest
that is back before its host keeps asking; the caller owns the deadline.

- `START` and the registry file carry `RNetLanLobby.session_id`, the fresh
  per-match id the HOST allocates (no server does on LAN; a rematch must not
  reuse the last match's id). Both are trailing optional fields: an older
  reader ignores them, and an older writer reads as 0.
- The host answers a `JOIN_REQ` from the already-seated guest's address with
  `JOIN_OK` again (its first answer was lost) instead of `full`, and honours
  `LEAVE` only from the seated guest's address, as `SWAPREQ` / `CHATREQ` do.

`tests/lan_rematch_test.c` walks the lifecycle on loopback.
