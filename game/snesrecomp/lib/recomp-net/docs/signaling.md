# Signaling contract

recomp-net does **not** implement a lobby or matchmaking server. ICE credentials
and SDP/candidate exchange are owned by the host (or a future custom lobby).

## Messages (`RNetSignal`)

| Type | Direction | Payload |
|------|-----------|---------|
| `RNET_SIGNAL_LOCAL_SDP` | library → host | Local SDP offer/answer text |
| `RNET_SIGNAL_REMOTE_SDP` | host → library | Peer SDP |
| `RNET_SIGNAL_LOCAL_CANDIDATE` | library → host | Trickle candidate line |
| `RNET_SIGNAL_REMOTE_CANDIDATE` | host → library | Peer candidate |
| `RNET_SIGNAL_GATHERING_DONE` | library → host | Gathering finished |
| `RNET_SIGNAL_SET_CONTROLLING` | host → library | `flag != 0` ⇒ controlling gather-order hint (libjuice has no public set-role API) |

`text` is NUL-terminated and truncated at 2047 characters.

## Host duties

1. Pass `on_signal` in `RNetHostVTable` before `rnet_session_start_ice`.
2. Forward every outbound signal to the peer through your lobby channel.
3. Deliver inbound signals with `rnet_session_push_signal`.
4. Supply STUN/TURN in `RNetIceConfig` before gather. The library does not
   fetch credentials; snesrecomp’s WS lobby client mints Coturn creds via
   `get_turn_credentials` / `turn_credentials` and passes them into
   `snes_netplay_start` (or use `SNES_NET_TURN_*` env overrides).

## One-to-many: the ICE hub (host relay over ICE)

`rnet_session_start_ice_hub` gives the host one agent per guest seat. Signals
carry `RNetSignal.peer_slot` (0xFF = unaddressed, the 1:1 case): `on_signal`
reports the seat an outbound signal is for, and inbound guest signals are
delivered with `rnet_session_push_signal_from(session, slot, &sig)`. The host
answers, guests offer; there is no TURN. Frozen agents (COMPLETED) ignore
further `REMOTE_SDP` / `REMOTE_CANDIDATE`, in hub mode and in 1:1 sessions.

Agents that were connected *before* the match (the lobby waiting room) are
handed to a session with `rnet_session_start_ice_hub_adopt` (host) and
`rnet_session_adopt_ice_agent` (guest); no signalling happens after adoption.

## Lobby `signal` type ranges

All ICE signalling rides `op:signal` (`type`, `flag`, `text`, optional
`to_player_id`; the server stamps `from_player_id`). Ranges in use by this
library, which must not overlap:

| `type` | Owner |
|--------|-------|
| 1-6 | The game's own session ICE (bare `RNetSignalType`; broadcast when `to_player_id` is empty) |
| 100-102 | Waiting-room RTT ping / pong / report |
| 110, 111 | Mod transfer request / refusal |
| 121-126 | Mod-transfer ICE agent (`120 + RNetSignalType`) |
| **131-136** | **Host relay over ICE: `RNET_LOBBY_SIG_HOSTICE_BASE (130) + RNetSignalType`** |

Host-ICE signals are always addressed (`to_player_id`) and the receiver
dispatches by the sender's `from_player_id`, never by anything in the payload:
only a seated, non-spectator player is a peer, a guest accepts the host and
nobody else, and a signal from a seat other than the one an agent serves is
held rather than applied. `flag` carries the negotiation id (1..255) chosen by
the offering guest; a new id means a fresh agent. Signals that arrive before
the agent exists (the offerer starts gathering the moment it is seated) wait in
a per-peer hold and are replayed in order. Only the emitted types are valid on
the wire (1 LOCAL_SDP, 3 LOCAL_CANDIDATE, 5 GATHERING_DONE); the receiver maps
them to `REMOTE_*`. Type 6 (`SET_CONTROLLING`) is refused.

Path proof: when its agent reaches COMPLETED a guest sends
`{"op":"path_report","path":"direct","ice":"host|srflx|prflx"}` (refreshed every
45 s) and `path:"fail"` on FAILED or after 25 s without a connection (then
retries after 20 s with a new negotiation id). The host publishes
`match_caps.relay = "host"` plus `relay_via = "ice"` and sends no
`set_host_endpoint`; `automatch_queue` carries `"ice_relay": true`. The server
echoes `relay_via` in `launch` and `launch.match_caps`.

## Mapping to the WS lobby server

recomp-net-server’s WebSocket lobby treats each `RNetSignal` as a 1:1
envelope (`op:signal` type + flag + text). TURN minting is a separate
`get_turn_credentials` op (same HMAC shape as HTTP `/v1/turn-credentials`).

## LAN-only

Omit ICE: call `rnet_session_start_lan` and leave `on_signal` NULL. Manual file
exchange demo: `examples/ice_manual_signaling`.
