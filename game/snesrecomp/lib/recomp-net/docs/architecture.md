# Architecture

```text
Host game
  │  sample_local / publish / advance / on_signal
  ▼
RNetSession          (FSM + admission)
  ├── RNetInputRing  (per-slot history, indexed by wire tick)
  ├── RNetProtocol   (HELLO / READY / START / INPUT / DELAY_SYNC)
  └── RNetTransport  (LAN UDP  |  ICE send/recv mux  |  ICE hub: one agent per guest seat)
         └── RNetIceAgent (optional, libjuice)
```

## Session phases

| Phase | Meaning |
|-------|---------|
| `IDLE` | Created; ICE gathering may still be in progress |
| `LINKING` | Datagram path up; exchanging `HELLO` |
| `READY` | Peers advertising `READY`; waiting for full barrier |
| `RUNNING` | Slot 0 emitted `START` (or remote `START` received); sim advances |

Slot **0** is the sim authority: it sends `START` once every slot has
signaled ready, and again to any seat whose `READY` still arrives after it
started -- a seat sends `READY` only until it has seen `START`, so that seat's
`START` was lost. `START` used to go out exactly once; on a lossy link one
dropped copy left that seat in `READY` for good while the rest of the room ran
and waited on its input (`rb_driver_test` 4seat-loss2, 2 of 10 full runs,
2026-09-25; a forced single-`START` drop reproduces it every time).

## Delay-sync admission

For sim tick `T` and committed delay `D`:

1. Host calls `rnet_session_try_admit(s, T)`.
2. Library samples local input and stores it at **wire tick** `T + D`.
3. Admission succeeds only when every **remote** slot has a valid ring row for
   wire tick `T + D`.
4. On success, `publish(T, by_slot, …)` is invoked with gameplay-indexed
   samples; host runs one sim step, then `rnet_session_advance`.
5. On failure, return `0` and keep calling `rnet_session_pump` (ingress /
   retransmit continue while the sim stalls).

There is no prediction window in v1.

## Transport mux

- `rnet_session_start_lan` binds UDP and targets a peer `host:port`.
- `rnet_session_start_ice` (when `RNET_ENABLE_ICE`) wires transport callbacks
  to the ICE agent; session stays `IDLE` until ICE reaches `COMPLETED`, then
  enters `LINKING` and runs the same bootstrap/admission path over ICE
- With TURN credentials and without `force_relay`, `rnet_session_pump` may
  restart ICE once with relay-only candidates on `FAILED`, general stall
  (default 5s, `RNET_ICE_RELAY_FALLBACK_MS`), early when remotes stay
  RFC1918-only (default 2.5s, `RNET_ICE_RELAY_PRIVATE_MS`), or completed
  non-relay with no session packets (~6s, `RNET_ICE_RELAY_DEAD_MS`). Opt out:
  `RNET_ICE_NO_RELAY_FALLBACK=1`.
- A 1:1 agent is **frozen** once COMPLETED (a repeated `REMOTE_SDP` /
  `REMOTE_CANDIDATE` no longer rebuilds it). With TURN configured the freeze
  waits until session traffic has been seen, so the dead-path restart above and
  a peer's restart offer still work; a local `force_relay` restart unfreezes.
- `force_relay` / Force TURN strips non-relay lines from local/remote SDP and
  trickle, skips STUN, and gates `juice_send` until CONNECTED/COMPLETED.
  libjuice still gathers local host (no transport-policy API), so a
  host↔relay pair can still win on LAN; host↔prflx should not when remotes
  are stripped.

## Online topology

Which path carries an online match is decided by the lobby server at `launch`
(`transport`), from what every seat proved in the waiting room. Three paths
exist; a match always connects because the server falls back to its relay
unless every guest proved the host path.

| `launch.transport` | Path | Client side |
|--------------------|------|-------------|
| `sfu` / relay (a `relay_endpoint`) | **Lobby UDP SFU star.** Every peer sends to one advertised server endpoint; the server fans opaque datagrams (magic + `session_id`) to every other registered seat. No guest-guest mesh. This is the fallback and what an old client always gets. | `rnet_session_start_lan*` against the relay endpoint |
| `host`, no `relay_via` | **Host relay over an advertised UDP port** (`host_relay.h`). The host holds a UPnP / NAT-PMP / STUN-mapped port, each guest probes it and reports `path_report direct|fail`; the match then runs on the host's LAN hub. | `rnet_session_start_lan_hub` on the host, `rnet_session_start_lan` on guests |
| `host`, `relay_via: "ice"` | **Host relay over ICE** (`host_ice.h`). No port is advertised. The host runs one answerer agent per seated guest, each guest one offerer agent to the host; the agents connected in the waiting room *are* the match transport. | `rnet_session_start_ice_hub_adopt` on the host, `rnet_session_adopt_ice_agent` on guests |

Sim authority is **slot 0** in every case (session host); guests may
rearrange among seats 1..N-1. Match traffic includes delay-sync and rollback
opcodes when the host uses them.

### Host relay over ICE

```text
 guest 1 --offer--> [lobby `signal`, type 130+n, to_player_id] --> host agent(seat 1)  (answers)
 guest 2 --offer--> [lobby `signal`, type 130+n, to_player_id] --> host agent(seat 2)  (answers)
        COMPLETED --> {"op":"path_report","path":"direct","ice":"host|srflx|prflx"}
 launch (transport host, relay_via ice) --> agents handed to the sessions, nothing renegotiated
 host session = RNET_TRANSPORT_ICE_HUB: datagrams from seat i fan out to every other seat
```

- Roles are fixed: the host **answers**, guests **offer**. Hub agents never use
  TURN and never force-relay; there is no fixed bind port (N agents cannot
  share one), each agent binds an ephemeral port. A guest that cannot connect
  directly reports `path: "fail"` and the server's relay carries the match.
- Agents are added and removed as members join, leave or move seats. A guest
  that moves seat (or retries after a failure) starts a fresh negotiation with
  a new id carried in `signal.flag`; the host rebinds that seat to a fresh
  agent and ignores anything stamped with an older id.
- An agent is **frozen at COMPLETED**: a repeated `REMOTE_SDP` /
  `REMOTE_CANDIDATE` is a no-op, never a rebuild of a live link. The same rule
  applies to a 1:1 agent in `rnet_session_start_ice` once it is COMPLETED (see
  below for the TURN-fallback exception).
- Handover: at `launch` the lobby client captures the COMPLETED agents; the
  engine takes them (`rnet_lobby_ice_take_hub` /
  `rnet_lobby_ice_take_guest_agent`) and the session adopts them. The session's
  dead-path timers run from the moment of adoption, not from when the agent
  connected, so a long wait in the room costs nothing. A seat whose agent is not
  COMPLETED makes the launch fail with a reason; it is never a smaller room.
- Spectators (gallery seats) are not on the hub path in this version.

## Host-as-relay (LAN hub)

For LAN/direct 3+ seats without the lobby SFU, the lobby owner calls
`rnet_session_start_lan_hub` (empty peer). Guests call `rnet_session_start_lan`
with the host endpoint. Transport hub role is independent of sim `local_slot`.
The hub learns seats from packet `local_slot` and fans out raw datagrams to
every other known seat (local star). Online, the same hub runs behind the
advertised-port host relay; the ICE variant above fans out the same way over
per-seat agents (`rnet_session_start_ice_hub`).
