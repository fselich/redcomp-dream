# Protocol

All multi-byte integers are **little-endian**. Every packet ends with a 32-bit
FNV-1a-style checksum over the preceding bytes (`rnet_proto_checksum`).

## Common header

| Field | Size | Notes |
|-------|------|-------|
| magic | u32 | Default `0x524E4554` (`RNET`) |
| type | u16 | Packet id |
| session_id | u32 | Must match config |

## Packet types

### HELLO (1)

`local_slot : u8`, `slot_count : u8`, `delay : u8`, `pad : u8`

Peers discover each other and confirm slot layout / advertised delay.

### READY (2)

`local_slot : u8`, `pad : u8×3`

Barrier before start. Session marks the sender ready.

### START (3)

`start_tick : u32`

Emitted by slot 0 when all slots are ready. Sets `sim_tick` and enters
`RUNNING`.

### INPUT (4)

`local_slot : u8`, `frame_count : u8`, `input_epoch : u16` (LE), `ack_tick : u32`,
then `frame_count` frames:

| Field | Size |
|-------|------|
| tick | u32 (wire tick) |
| size | u16 |
| bytes | `size` (≤ `RNET_INPUT_MAX`) |

Bundles retransmit recent local wire rows (`bundle_redundancy`).
`input_epoch` bumps on `hard_resync` (post-load); receivers drop other-epoch
packets so in-flight tips cannot first-wins into the new `sim_tick=0` window.

### DELAY_SYNC (5)

`new_delay : u8`, `pad : u8×3`, `effective_tick : u32`

Optional mid-session delay change. Receivers queue the change when
`effective_tick` is still in the future and commit when `sim_tick` reaches
that tick (both peers via `rnet_session_advance` / `set_sim_tick`). Applied
immediately when already past `effective_tick` or while not RUNNING.
`rnet_session_request_delay_change` schedules + emits; MotK uses this for
always-on adaptive delay bumps after sustained prediction-runway freezes.

### INPUT_CONFIRM (6)

`local_slot : u8`, `input_epoch : u16` (LE), `pad : u8`, `sim_tick : u32`,
`input_hash : u32`

Peers agree on the resolved pad set for `sim_tick` before publish/advance.
`input_hash` is `rnet_proto_checksum` over `sim_tick` (LE u32) followed by each
slot's `size` (LE u16) and `bytes`. Session latches local/remote wire rows
(first-wins) so late retransmits cannot change the hash mid-confirm.
Mismatch flags an input desync; agreement across all slots allows admission.
Same `input_epoch` rule as INPUT.

### BYE (7)

`local_slot : u8`, `pad : u8×3`

Graceful leave. Best-effort UDP (hosts may retransmit a few times on shutdown).
Peer marks the sender gone and can exit without waiting for the RX timeout.
The receiver records it twice: the aggregate `peer_gone` (any seat's BYE,
`rnet_session_peer_disconnected`) and per seat by `local_slot`
(`rnet_session_peer_gone(s, slot)`); see "Per-seat liveness" below.

### STATE_BEGIN (8) / STATE_CHUNK (9) / STATE_ACK (10)

Host→guest chunked blob transfer (savestate / memcard / SRAM). Cap:
`RNET_STATE_MAX` (8 MiB). Chunk payload ≤ `RNET_STATE_CHUNK_MAX` (1120;
fits in `RNET_MAX_PACKET` with header+checksum). ICE/TURN uses AIMD pacing
(start ~32 KiB / 16 chunks, max 256 KiB / 64 chunks, sticky warm-start) so
multi‑MB MotK `.pst` transfers do not crawl on Force TURN.

**BEGIN:** `local_slot`, `op`, `slot`, `pad`, `xfer_id : u32`, `total_size : u32`,
`payload_crc : u32` (`rnet_proto_checksum` over the full blob).

**CHUNK:** `local_slot`, pad×3, `xfer_id`, `offset : u32`, `size : u16`, pad u16,
`data[size]`.

**ACK:** `local_slot`, pad×3, `xfer_id`, `ack_bytes : u32` (contiguous bytes from 0).

Guest marks ready only after full contiguous receive **and** CRC match. Admit
stalls for the whole transfer. With more than one receiver the sender waits
for **every** receiver's full ACK — see "Multi-seat STATE" below.

`xfer_id` of a MEMCARD upload is `0x40000000 | ((seat - 1) << 24) | serial`
(serial in the low 24 bits). The host's ACK carries only the id and is
broadcast, so two guests uploading at once must never share one; seat 1 has 0
in bits 24..29, which is exactly the id a two-seat guest always sent.

`op`: `0=SAVE`, `1=LOAD`, `2=SRAM`, `3=RB_KF`, `4=BOOT`, `5=MEMCARD`.

`MEMCARD` is the one **guest→host** op: a seated guest uploads its own memory
card so the host can fold it into the SRAM blob it then broadcasts. `slot`
carries the sender's seat. Only the host opens a receive for it; other guests
see the same broadcast and drop it (the sender tracks only the host's ACKs).
It is never probed — the host has nothing to hash it against.

### STATE_PROBE (11) / STATE_PROBE_REPLY (12)

Hash-agree before transfer. Host announces; guest replies; skip BEGIN/CHUNK when
identical.

**PROBE:** `local_slot`, `op`, `slot`, `replied : u8`, `total_size : u32`,
`payload_crc : u32`.

`replied` (formerly pad, always 0) is a seat bitmask: bit i = seat i's reply to
*this* probe already reached the prober, so seat i ignores the retransmit.
0 means "every receiver answers" — what every older encoder wrote and what a
two-seat prober still always sends (it stops retransmitting at the first
reply), so the field is additive and needs no version bump. An older guest
ignores it and behaves as before.

- `op=SAVE`, `total_size == 0`: coordinate local save first (guest ACKs when its
  local write is done). Does **not** stall admit (deferred saves must still
  reach a block boundary).
- `op=LOAD`, `total_size == 0`: post-load ready rendezvous (not a content hash).
  Does **not** stall INPUT (late applier still needs tip rows); the app freezes
  sim until mutual ready + `hard_resync`.
- Hash probes (`total_size != 0`): stall until agree or transfer.

**PROBE_REPLY:** `local_slot`, `op`, `slot`, `match : u8`, `total_size : u32`,
`payload_crc : u32`. The size/crc **echo the probe being answered** so a late
SAVE-coord or LOAD-ready ACK cannot satisfy a subsequent hash probe that shares
the same `op`/`slot`.

On hash miss the host starts STATE_BEGIN.

After a LOAD restore, both peers ACK a ready probe, then each calls
`rnet_session_hard_resync` (clear local **and** remote rings, `sim_tick → 0`)
and `rnet_session_prime_delay_inputs` once at mutual ready — not at apply
time. Both stay in the app load barrier until `try_admit` succeeds (fresh
tip + INPUT_CONFIRM). Ready-probe retransmit interval is 8 ms.

## Multi-seat STATE (3–8 seats)

With more than two seats the transport is a fan-out (lobby relay or
`rnet_session_start_lan_hub`): every datagram a seat sends reaches every other
seat. STATE uses that — a chunk is sent once and serves every receiver — but
tracks completion, window and retransmission **per receiver**. With two seats
every rule below reduces to the single-peer behaviour, on the wire and at the
API.

### Expected seats

A host transfer or probe waits on its *expected seats*: every occupied seat
(`RNetConfig.occupied_mask`; 0 = all of `[0, slot_count)`) except the
sender's own. While `rnet_session_set_rb_peer_slot(s, slot)` scoping is on
(the rollback driver sets it with exactly one peer) the expected seat is that
one slot, because STATE packets from any other seat are filtered there. A
MEMCARD upload's one expected seat is the host (0). Observers are never
expected: their ACKs and probe replies are ignored (they still receive the
fan-out, but a transfer does not wait for them). `rnet_session_state_begin`
and `rnet_session_state_probe` return -1 when there is no expected seat.

A seat that leaves is **not** dropped automatically — not even on BYE. The
transfer or probe stalls until the host calls
`rnet_session_state_drop_peer(s, slot)`, which removes the seat and may
complete it. Decide with the per-seat liveness calls below.

### Sender (outbound transfer)

- `ack[seat]` per receiver; a receiver's ACK counts only while it is expected
  and only for the current `xfer_id`.
- Complete (`take_ready` on the sender) when **every** expected seat ACKed
  the whole blob. Before, the sender kept the maximum over any ACK, so the
  fastest guest completed the transfer for all of them.
- Window: `[min_ack, min_ack + cwnd)` — anchored on the slowest receiver.
  ICE additive increase applies when the slowest receiver advances (N
  receivers ACKing must not grow cwnd N times as fast).
- BEGIN is retransmitted (40 ms LAN / 80 ms ICE) until **every** expected
  seat has ACKed past 0. Before, it stopped at the first ACK, so a seat that
  lost BEGIN never opened a receive.
- ACK timeout per receiver: a receiver whose ACK has not advanced for the
  timeout rewinds the cursor to its own watermark. AIMD backs off when the
  slowest receiver times out or a rewind happened (a fast receiver merely
  waiting for the window to reach its gap is not loss).

### Receiver (inbound transfers)

- One receive per **source seat**. A new BEGIN from seat X replaces only X's
  receive, so several guests' MEMCARD uploads reach the host at once and each
  is delivered separately. Before, every BEGIN cleared all transfer state, so
  simultaneous uploads clobbered one another.
- Why per-source receive rather than host-serialized uploads with a
  busy/accept reply: it needs no new opcode or retry protocol (old guests
  work unchanged), no guest waits on another's upload, and loss of a
  "busy"/"accept" datagram cannot strand an upload — every upload is driven
  by its own sender's BEGIN/chunk retransmit, exactly the two-seat path. The
  cost is one bitmap (~940 B) per seat.
- Supersede (unchanged two-seat rule): a new BEGIN from the **one** expected
  receiver of our outbound transfer replaces that transfer, because that seat
  only starts its own after consuming ours (a guest's MEMCARD receipt after
  the host's proposal; the host's BOOT after a guest's upload). An outbound
  transfer with other receivers still pending keeps running. Likewise a
  prober drops its probe on a BEGIN from its one expected seat.
- A chunk or BEGIN for a transfer this receiver already finished is answered
  with a full ACK (chunks paced to one per 20 ms), so a sender that missed
  the final ACK completes instead of retransmitting forever.

### Probe barrier

The host's probe is retransmitted every 8 ms until **every** expected seat
replied; the PROBE `replied` byte tells seats that already answered to
ignore the retransmit (otherwise a seat that answered and cleared a LOAD/BOOT
ready probe would be handed it again every 8 ms). A reply counts only from an
expected seat for the current probe generation; the latest answer per seat
wins.

### API (session.h)

| Call | Semantics |
|------|-----------|
| `rnet_session_state_begin(s, op, slot, data, size)` | Unchanged signature. Host: to every expected seat. Guest: MEMCARD to seat 0. -1 while any transfer is open, for an observer, or with no expected seat. |
| `rnet_session_state_take_ready(s, &op, &slot, &data, &size)` | Unchanged. With several transfers ready: the outbound one first, then inbound by ascending source seat. |
| `rnet_session_state_take_ready_from(s, &from, &op, &slot, &data, &size)` | Same, plus `from` = source seat (our own `local_slot` for our outbound transfer's completion). |
| `rnet_session_state_finish(s, hard_resync)` | Finishes the transfer the last `take_ready` reported (else the one it would report); with nothing ready it aborts every open transfer, as before. |
| `rnet_session_state_finish_from(s, from_slot, hard_resync)` | Finish (ready) / abort (not ready) one: `from_slot == local_slot` = outbound, another seat = that seat's inbound, `RNET_STATE_FROM_ALL` (-1) = all. |
| `rnet_session_state_busy(s)` | 1 while a probe waits on replies / the app, or any open transfer is incomplete. |
| `rnet_session_state_drop_peer(s, slot)` | Stop waiting on `slot` in the open outbound transfer and the open host probe. 1 if anything changed. |
| `rnet_session_state_progress(s, slot, &acked, &total)` | Sender: that expected receiver's contiguous ACK. |
| `rnet_session_state_pending_receivers(s)` | Sender: bitmask of expected receivers not yet fully ACKed. |
| `rnet_session_state_inbound_mask(s)` | Receiver: bitmask of source seats with an inbound transfer open. |
| `rnet_session_state_probe(s, op, slot, size, crc)` | Unchanged signature; waits on every expected seat. -1 with no expected seat. |
| `rnet_session_state_probe_take_reply(s, &match)` | 1 when **every** expected seat replied; `match` = 1 only if all matched. |
| `rnet_session_state_probe_take_reply_from(s, slot, &match)` | 1 when that expected seat replied; its answer. Non-consuming. |
| `rnet_session_state_probe_replies(s, &expect, &replied, &match)` | Host probe open: masks of expected / replied / replied-match seats. |

`RNetSessionStats` (appended fields): `state_expect_mask`, `state_done_mask`,
`state_rx_mask`, `state_probe_expect_mask`, `state_probe_reply_mask`,
`peer_gone_mask`, `peer_rx_age_ms[RNET_MAX_SLOTS]`. `state_bytes_acked` on
the sender is the slowest receiver's ACK.

A typical N-seat startup (the GBA shape): each guest `state_begin(MEMCARD,
seat)`; the host loops `take_ready_from` → store card for `from` →
`finish_from(from)` until it has one per guest (`inbound_mask` shows uploads in
flight); then `state_begin(BOOT)` to all, completion when every guest holds
it; then `state_probe(SAVE, 0, 0, hash)` as an N-party ready barrier
(`probe_take_reply`, or `probe_replies` to see who is missing). A guest whose
upload's final ACK was lost sees BOOT supersede the upload, as with two seats.

## Per-seat liveness

The aggregate calls keep their two-seat meaning: any valid packet stamps the
silence timer and any seat's BYE trips `rnet_session_peer_disconnected`. In a
room of four a silent seat therefore never trips it while the others talk.
Per seat (sender slot from the packet header, which a relay forwards
unchanged; START counts as seat 0; DELAY_SYNC has no sender and counts for
none):

| Call | Semantics |
|------|-----------|
| `rnet_session_peer_gone(s, slot)` | 1 when that seat sent BYE. |
| `rnet_session_peer_gone_mask(s)` | Bit i = seat i sent BYE. |
| `rnet_session_peer_rx_age_ms(s, slot)` | ms since that seat's last valid packet (session clock); `RNET_PEER_RX_NEVER` if never, for our own seat, or out of range. |
| `rnet_session_peer_slot_disconnected(s, slot, timeout_ms)` | One seat's `peer_disconnected`: BYE, or silent ≥ `timeout_ms` after its first packet; never heard: only past the ≥ 90 s link budget. `timeout_ms == 0` = BYE only. Unoccupied seats / our own: 0. |
| `rnet_session_disconnected_peers(s, timeout_ms)` | Bitmask of the above. |

`rnet_session_touch_peer_liveness` also stamps every occupied remote seat
that has not sent BYE (even when the aggregate is already gone).

## Wire compatibility

No new packet types, no changed layouts. Two additive value changes, both
producing the pre-multi-seat bytes with two seats in the default layout:
the PROBE `replied` byte (0 unless a seat already answered, which a two-seat
prober never retransmits after) and the MEMCARD `xfer_id` seat bits (0 for
seat 1). One behavioural addition visible on the wire: a receiver re-ACKs
chunks of a transfer it already finished (paced, ignored by any sender that
is not waiting on it).

## Wire vs sim

Hosts reason in **sim ticks**. Inputs on the wire are indexed by
`wire = sim + D`. Admission for sim `T` requires remote rows at wire `T + D`,
then INPUT_CONFIRM hash agreement on the resolved set.
