# Host integration

For the full **performance / stability checklist** (held present on stall,
starvation latch, catch-up policy, ICE/TURN, audio, rematch), see the
“Recommended host / recomp-engine patches” section in the [root README](../README.md).

## Required loop

```c
while (running) {
    rnet_session_pump(session);   /* recv, ICE poll, bootstrap, INPUT send */

    if (!rnet_session_is_running(session)) {
        /* still linking / waiting for START */
        continue;
    }

    uint32_t t = rnet_session_sim_tick(session);
    if (rnet_session_try_admit(session, t)) {
        /* Apply published pads for tick t, then step the authoritative sim once. */
        host_sim_step(t);
        rnet_session_advance(session);
    }
    /* else: stall — do not advance local sim */
}
```

**Rule:** only one authoritative sim tick may advance after a successful
`try_admit`. Do not sample pads for tick `T+1` until `advance` has run.

`try_admit` **latches** the local pad once per wire tick (re-admits reuse it)
and waits for **INPUT_CONFIRM** hash agreement across all slots before
`publish`. Remote INPUT frames are first-wins. If hashes disagree, admission
stalls permanently for the session; poll with `rnet_session_input_desync`.

On host shutdown call `rnet_session_send_bye` before destroy so the peer can
exit immediately. While waiting on admit, poll
`rnet_session_peer_disconnected(session, 1500)` (~1.5s silence or peer BYE)
and leave the session instead of spinning forever.

With three or more seats that aggregate check cannot see a single silent seat
(the others' traffic keeps it fresh). Use the per-seat calls —
`rnet_session_peer_slot_disconnected(session, slot, 1500)`,
`rnet_session_disconnected_peers(session, 1500)`,
`rnet_session_peer_gone(session, slot)`, `rnet_session_peer_rx_age_ms` — and,
if the match continues without that seat, `rnet_session_state_drop_peer` so
an open STATE transfer or probe barrier stops waiting on it. STATE with more
than two seats (per-receiver completion, concurrent MEMCARD uploads, N-party
probe barriers) is specified in `docs/protocol.md` "Multi-seat STATE".

## Host vtable

| Callback | Role |
|----------|------|
| `sample_local` | Fill opaque pad bytes for the current sim tick (called inside `try_admit`) |
| `publish` | Receive resolved inputs for all slots; apply before sim step |
| `now_ms` | Optional; defaults to platform monotonic ms |
| `on_signal` | ICE SDP/candidates toward your lobby (LAN-only may leave NULL) |

## Online launch: choosing the transport

`rnet_lobby_try_fill_launch` fills an `RNetLobbyJoinInfo`. Branch on it in this
order:

1. `transport_ice_hub == 1` -- **host relay over ICE** (below).
2. `transport_host == 1` -- host relay over the advertised UDP port: the host
   binds `bind_hostport` and runs `rnet_session_start_lan_hub` (3+ seats) /
   accepts the guest; guests dial `host_endpoint`.
3. `force_input_relay == 1` -- the lobby server's UDP relay (dial `peer_hostport`).
4. otherwise peer-to-peer ICE / LAN as before.

`transport_ice_hub` implies `transport_host == 1` (test `transport_ice_hub`
first), `host_endpoint` is empty, and `bind_hostport` is a placeholder: **no UDP
socket is bound and nothing is dialled.** Old servers and old clients never see
`relay_via`, so path 2-4 are unchanged.

### Host relay over ICE: what the engine calls

```c
rnet_lobby_set_relay_via_ice(1);          /* host pref, default 1; publishes relay_via "ice" */
...
RNetLobbyJoinInfo ji;
if (rnet_lobby_try_fill_launch(&ji) && ji.transport_ice_hub) {
    RNetSession *s = rnet_session_create(&cfg, &vt);   /* cfg.local_slot as for any match */
    if (rnet_lobby_is_host()) {
        RNetLobbyIceSeat seat[8];
        RNetIceAdoptSeat adopt[8];
        int n = rnet_lobby_ice_take_hub(seat, 8);       /* ownership of each agent -> you */
        if (n < 1) { report(rnet_lobby_ice_launch_error()); /* abort the match */ }
        for (i = 0; i < n; ++i) {
            adopt[i].slot  = /* YOUR session slot for lobby seat seat[i].lobby_slot */;
            adopt[i].agent = seat[i].agent;
        }
        if (rnet_session_start_ice_hub_adopt(s, adopt, n) != 0) { /* nothing adopted: */
            for (i = 0; i < n; ++i) rnet_host_ice_destroy_agent(seat[i].agent);
            /* abort */ }
    } else {
        RNetIceAgent *a = rnet_lobby_ice_take_guest_agent();   /* ownership -> you */
        if (!a || rnet_session_adopt_ice_agent(s, a) != 0) { /* destroy a if non-NULL; abort */ }
    }
    rnet_lobby_clear_launch_pending();                  /* after the take */
    /* then pump / try_admit exactly as for any session; on_signal may stay NULL */
}
```

Rules the engine must respect:

- **Take before `rnet_lobby_clear_launch_pending()`.** The client captured the
  agents when `launch` arrived; an untaken bundle is destroyed after 60 s, at the
  next launch, or on leave / disconnect (`rnet_lobby_ice_discard()` drops it now).
- **You own the session-slot mapping.** `RNetLobbyIceSeat.lobby_slot` is the
  guest's seat in the *lobby's* namespace; `RNetIceAdoptSeat.slot` is the
  *session* slot (1..`RNET_MAX_SLOTS`-1) your engine maps it to (host
  gallery-hosting offsets etc.). Slot 0 is the host and is never an adopted seat.
  The hub mask is built from exactly the seats you pass, and
  `rnet_session_start_ice_hub_adopt` fails (-1, nothing adopted) if any agent
  is NULL, duplicated, out of range or not COMPLETED.
- **No renegotiation, no signals.** Adopted agents are frozen; the session's
  dead-path timers start at adoption. Do not call `rnet_session_push_signal*`
  for them.
- **A not-connected seat refuses the launch.** If any seated guest (or a guest's
  own link to the host) is not COMPLETED, the client itself drops the launch:
  `launch_pending` stays 0, `join.last_error == "ice_not_connected"` and
  `rnet_lobby_ice_launch_error()` names the seat. Surface that string; never
  start a smaller room.
- Spectators are not on the ICE hub path in this version.
- `rnet_lobby_host_ice_status(&st)` (`RNetHostIceStatus`, `recomp_net/host_ice.h`)
  gives per-seat state for the waiting-room UI. Builds without
  `RNET_ENABLE_ICE` never publish `relay_via` (a guest of such a build reports
  `path: "fail"`).

Handover API summary:

| Call | Header |
|------|--------|
| `rnet_lobby_set_relay_via_ice(int)` / `rnet_lobby_relay_via_ice()` | `lobby_client.h` |
| `RNetLobbyJoinInfo.transport_ice_hub`, `RNetLobbyMatchCaps.relay_via_ice` | `lobby_client.h` |
| `rnet_lobby_ice_take_hub(RNetLobbyIceSeat *out, int max)` -> n or -1 | `lobby_client.h` |
| `rnet_lobby_ice_take_guest_agent(void)` -> `RNetIceAgent *` or NULL | `lobby_client.h` |
| `rnet_lobby_ice_launch_error(void)`, `rnet_lobby_ice_discard(void)` | `lobby_client.h` |
| `rnet_session_start_ice_hub_adopt(s, const RNetIceAdoptSeat *, n)` | `session.h` |
| `rnet_session_adopt_ice_agent(s, RNetIceAgent *)` | `session.h` |
| `rnet_host_ice_destroy_agent(RNetIceAgent *)` (release a taken, unused agent) | `host_ice.h` |

## N64 / PSX recomp notes

- Hook pad read so the runtime **does not** inject local-only input into the
  shared sim; use `publish` as the sole source of pads for locked ticks.
- Keep RNG, timers, and VI/frame pacing deterministic across peers; the library
  does not fix host desyncs.
- Prefer a single thread that owns both `pump` and sim advance, or protect the
  session with an external mutex (API is not internally locked).
- After LOAD: ready probe first (both applied), then each peer
  `hard_resync` (clears remotes) + `prime_delay_inputs` once at mutual ready.
  Keep the app barrier up until `try_admit` succeeds on **both** peers.

## Config

`RNetConfig` fields (`slot_count`, `local_slot`, `input_delay`,
`bundle_redundancy`, `session_id`, `protocol_magic`) must match across peers
except `local_slot`. Negotiate them out-of-band (lobby) before `create`.

## LAN address selection

Bind listeners to `0.0.0.0:port` and advertise a concrete address selected from
`rnet_ipv4_enumerate`. The returned interface labels let launchers distinguish
physical, VPN, and virtual adapters instead of silently choosing the wrong LAN.
See [address_discovery.md](address_discovery.md) for ordering and API details.
