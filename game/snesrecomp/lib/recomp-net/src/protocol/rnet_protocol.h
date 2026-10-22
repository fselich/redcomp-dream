#ifndef RNET_PROTOCOL_H
#define RNET_PROTOCOL_H

#include "recomp_net/config.h"
#include "recomp_net/input.h"
#include "recomp_net/types.h"
#include "recomp_net/rollback.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RNET_PKT_HELLO 1
#define RNET_PKT_READY 2
#define RNET_PKT_START 3
#define RNET_PKT_INPUT 4
#define RNET_PKT_DELAY_SYNC 5
#define RNET_PKT_INPUT_CONFIRM 6
#define RNET_PKT_BYE 7
#define RNET_PKT_STATE_BEGIN 8
#define RNET_PKT_STATE_CHUNK 9
#define RNET_PKT_STATE_ACK 10
#define RNET_PKT_STATE_PROBE 11       /* host→guest: hash/size before transfer */
#define RNET_PKT_STATE_PROBE_REPLY 12 /* guest→host: match (skip xfer) or not */
/* GBA Multi transfer barrier (0-delay SEND exchange; not pad INPUT). */
#define RNET_PKT_SIO_MULTI_XFER 13
#define RNET_PKT_STATE_PROBE_DONE 14 /* host→guest: every seat matched; release parked guests */

/*
 * Rollback control range (reserved, additive). Delay-sync hosts never emit or
 * parse these; rollback-mode sessions use them for episode coordination. Hosts
 * map their own wire (e.g. BattleShip SYNETPEER_*) onto these when aligning
 * transports; recomp-net hosts may use them natively. See docs/rollback.md.
 */
#define RNET_PKT_RB_SYNC 20        /* correction tuple: (epoch, mismatch, load, target, slot) */
#define RNET_PKT_RB_SEAL_ROWS 21   /* peer-authority sealed input rows chunk */
#define RNET_PKT_RB_BASELINE 22    /* post-load state digests for the baseline gate */
#define RNET_PKT_RB_POST 23        /* post-replay digests (commit / deepen / abort decide) */
#define RNET_PKT_RB_FRAME_COMMIT 24 /* state/master-hash watermark agreement token */
#define RNET_PKT_RB_RESOLVED 25    /* resolved-through / shared frontier advertise */

/* Mod-set negotiation, run once before the match may start.
 *
 * The host is authoritative: it publishes the exact set every peer must run,
 * and a peer answers whether it can. Mods patch guest memory, so a peer that
 * cannot reproduce the host's set is running a different game and must not be
 * allowed to start rather than be discovered desyncing later.
 *
 * One packet, not a chunked transfer: a realistic set is a few hundred bytes
 * against a 1200-byte MTU, and a set too large to fit is refused loudly rather
 * than silently truncated -- two different sets can share a prefix. */
#define RNET_PKT_MODSET 26      /* host->peer: canonical effective mod set */
#define RNET_PKT_MODSET_ACK 27  /* peer->host: can/cannot honour it, and why */

#define RNET_MODSET_TEXT_MAX 1024u
#define RNET_MODSET_REASON_MAX 96u

/* MODSET_ACK status */
#define RNET_MODSET_OK 0u
#define RNET_MODSET_MISSING 1u   /* package absent entirely */
#define RNET_MODSET_VERSION 2u   /* present at a different version */
#define RNET_MODSET_OPTION 3u    /* option or value this build cannot honour */
#define RNET_MODSET_TOO_BIG 4u   /* set did not fit the packet */

#define RNET_MAX_PACKET 1200
/* Must be >= max input_delay (20) + 1 so the neutral delay prefix can fit in
 * one INPUT bundle. send_input_bundle also multi-packets if the window is
 * larger, but keeping this at 21 avoids a D>=8 boot deadlock. */
#define RNET_MAX_BUNDLE 21
/* Wire header is 28 bytes + 4-byte trailer checksum → payload ≤ 1168 in a
 * RNET_MAX_PACKET (1200). 1120 leaves headroom for ICE/TURN framing quirks. */
#define RNET_STATE_CHUNK_MAX 1120
/* PSX .pst + dual memcards need multi‑MB; chunked ACK path scales with this. */
#define RNET_STATE_MAX (8u * 1024u * 1024u)
#define RNET_STATE_MAX_CHUNKS ((RNET_STATE_MAX + RNET_STATE_CHUNK_MAX - 1u) / RNET_STATE_CHUNK_MAX)

typedef struct RNetWireFrame
{
    rnet_u32 tick;
    rnet_u16 size;
    rnet_u8 bytes[RNET_INPUT_MAX];
} RNetWireFrame;

/* Rollback seal-row wire frame: fixed 9 bytes (buttons 2 + sticks 2 + source +
 * predicted + valid + rx + ry; wire version 2), tick carried by packet
 * row_begin + index.
 * source: host pad type — 0 digital, 1 DualShock, 2 JogCon, 3 NeGcon
 * (RNetRbFrame.analog), copied verbatim. */
typedef struct RNetRbWireFrame
{
    rnet_u16 buttons;
    rnet_s8 stick_x;
    rnet_s8 stick_y;
    rnet_u8 source;
    rnet_u8 is_predicted;
    rnet_u8 is_valid;
    rnet_u8 rx;
    rnet_u8 ry;
} RNetRbWireFrame;

#define RNET_RB_SEAL_ROWS_WIRE_FRAME_BYTES 9u
/* Wire version, sent in HELLO's former reserved byte (0 from a peer that
 * predates it). Peers of different versions refuse to link: version 2 widened
 * the seal-row frame, which an older peer would misparse. */
#define RNET_WIRE_VERSION 2u
/* RNET_RB_SEAL_ROWS_CHUNK_MAX is public (recomp_net/rollback.h): a host that
 * chunks its own seal rows has to know the bound the send path enforces. */

rnet_u32 rnet_proto_checksum(const rnet_u8 *data, size_t len);

/* Encode helpers return byte count or -1. */
int rnet_proto_encode_hello(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u8 local_slot,
                            rnet_u8 slot_count, rnet_u8 delay);
int rnet_proto_encode_ready(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u8 local_slot);
int rnet_proto_encode_start(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u32 start_tick);
/* input_epoch: bumps on hard_resync so in-flight tips from a prior post-load
 * epoch cannot first-wins poison the new sim_tick=0 window (pad bytes). */
int rnet_proto_encode_input(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u8 local_slot,
                            rnet_u16 input_epoch, rnet_u32 ack_tick, const RNetWireFrame *frames,
                            int frame_count);
/* Multi-seat INPUT adds an acknowledgment for each source seat after the
 * frames. Two-seat callers keep using encode_input and its original bytes. */
int rnet_proto_encode_input_acks(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id,
                                 rnet_u8 local_slot, rnet_u16 input_epoch, rnet_u32 ack_tick,
                                 const RNetWireFrame *frames, int frame_count,
                                 const rnet_u32 *acks, rnet_u8 ack_count);
int rnet_proto_encode_delay_sync(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u8 new_delay,
                                 rnet_u32 effective_tick);
/* Agree on resolved pad hash for sim_tick before publish/advance. */
int rnet_proto_encode_input_confirm(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u8 local_slot,
                                    rnet_u16 input_epoch, rnet_u32 sim_tick, rnet_u32 input_hash);
/* Graceful peer leave (best-effort UDP). */
int rnet_proto_encode_bye(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u8 local_slot);

/* Host→guest savestate transfer (chunked, ACK'd). */
int rnet_proto_encode_state_begin(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u8 local_slot,
                                  rnet_u8 op, rnet_u8 slot, rnet_u32 xfer_id, rnet_u32 total_size,
                                  rnet_u32 payload_crc);
int rnet_proto_encode_state_chunk(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u8 local_slot,
                                  rnet_u32 xfer_id, rnet_u32 offset, const rnet_u8 *data, rnet_u16 size);
int rnet_proto_encode_state_ack(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u8 local_slot,
                                rnet_u32 xfer_id, rnet_u32 ack_bytes);
/* Hash probe: skip transfer when guest already has identical blob. */
int rnet_proto_encode_state_probe(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u8 local_slot,
                                  rnet_u8 op, rnet_u8 slot, rnet_u32 total_size, rnet_u32 payload_crc);
/* Same, with the byte that used to be pad carrying `replied_mask`: bit i =
 * seat i's reply to THIS probe already reached the prober, so seat i ignores
 * the retransmit. 0 (what every older encoder wrote) = everyone answers, so
 * the field is additive. With more than one receiver the prober keeps
 * retransmitting until the slowest seat answers, and without it a seat that
 * already answered a LOAD/BOOT ready probe (and cleared it) would see the
 * retransmit as a fresh probe and re-raise it to its app. */
int rnet_proto_encode_state_probe_ex(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id,
                                     rnet_u8 local_slot, rnet_u8 op, rnet_u8 slot, rnet_u32 total_size,
                                     rnet_u32 payload_crc, rnet_u8 replied_mask);
/* Reply echoes the probe's size+crc so a late coord ACK (size=0) cannot be
 * accepted as a hash/ready reply for a different probe with the same op/slot. */
int rnet_proto_encode_state_probe_reply(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id,
                                        rnet_u8 local_slot, rnet_u8 op, rnet_u8 slot, rnet_u8 match,
                                        rnet_u32 total_size, rnet_u32 payload_crc);
/* Host releases matching guests only after every expected seat replied "match". */
int rnet_proto_encode_state_probe_done(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id,
                                       rnet_u8 local_slot, rnet_u8 op, rnet_u8 slot,
                                       rnet_u32 total_size, rnet_u32 payload_crc);

/* GBA Multi: one peer's SIOMLT_SEND for transfer seq (mGBA-style barrier).
 * `confirm_pad` u16: lo = Confirm IRQ watermark (0..2+); hi = local VBlank
 * count mod 256 (soft peer-frame watermark). Peers gate CONN_ESTABLISHED
 * Multi until both report confirm>=2; hi byte bounds free-run skew. */
int rnet_proto_encode_sio_multi_xfer(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id,
                                     rnet_u8 local_slot, rnet_u8 unit_id, rnet_u32 seq, rnet_u16 send,
                                     rnet_u16 confirm_pad);

/* Rollback control packets (reserved range; rollback-mode sessions only).
 * RB_SYNC `op` (was "initiator"): RNET_RB_SYNC_OP_* — begin/extend, follower
 * NACK (target field carries the follower's frontier), or episode ABORT
 * (mismatch field carries the RNET_RB_ABORT_CLASS_* cooldown class, load
 * field the sender's realign tick). `flags`: RNET_RB_SYNC_FLAG_*. */
int rnet_proto_encode_rb_sync(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u8 local_slot,
                              rnet_u32 epoch_id, rnet_u32 mismatch_tick, rnet_u32 load_tick, rnet_u32 target_tick,
                              rnet_u8 corrected_slot, rnet_u8 initiator, rnet_u8 flags);
int rnet_proto_encode_rb_seal_rows(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id,
                                   rnet_u8 local_slot, rnet_u32 epoch_id, rnet_u32 mismatch_tick,
                                   rnet_u32 target_tick, rnet_u8 slot, rnet_u32 row_begin,
                                   const RNetRbWireFrame *rows, rnet_u16 row_count);
int rnet_proto_encode_rb_baseline(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id,
                                  rnet_u8 local_slot, rnet_u32 epoch_id, rnet_u32 load_tick,
                                  rnet_u32 digest_master, rnet_u32 digest_a, rnet_u32 digest_b, rnet_u32 digest_c);
int rnet_proto_encode_rb_post(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id, rnet_u8 local_slot,
                              rnet_u32 epoch_id, rnet_u32 target_tick, rnet_u32 digest_master,
                              rnet_u32 input_digest, rnet_u8 match);
int rnet_proto_encode_rb_frame_commit(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id,
                                      rnet_u8 local_slot, rnet_u32 through_tick, rnet_u32 state_hash);
int rnet_proto_encode_modset(rnet_u8 *out, size_t cap, rnet_u32 magic,
                             rnet_u32 session_id, rnet_u8 local_slot,
                             const char *text);
int rnet_proto_encode_modset_ack(rnet_u8 *out, size_t cap, rnet_u32 magic,
                                 rnet_u32 session_id, rnet_u8 local_slot,
                                 rnet_u8 status, const char *reason);
int rnet_proto_encode_rb_resolved(rnet_u8 *out, size_t cap, rnet_u32 magic, rnet_u32 session_id,
                                  rnet_u8 local_slot, rnet_u32 resolved_through);

typedef struct RNetDecodedPacket
{
    rnet_u16 type;
    rnet_u32 session_id;
    rnet_u8 local_slot;
    rnet_u8 slot_count;
    rnet_u8 delay;
    rnet_u8 wire_version; /* HELLO: RNET_WIRE_VERSION of the sender, 0 = older */
    rnet_u32 start_tick;
    rnet_u32 ack_tick;
    rnet_u8 ack_count;
    rnet_u32 acks[RNET_MAX_SLOTS];
    rnet_u8 new_delay;
    rnet_u32 effective_tick;
    rnet_u32 confirm_sim_tick;
    rnet_u32 confirm_hash;
    rnet_u16 input_epoch; /* Input and rollback generation (hard_resync). */
    int frame_count;
    RNetWireFrame frames[RNET_MAX_BUNDLE];
    /* STATE_* */
    rnet_u8 state_op;
    rnet_u8 state_slot;
    rnet_u32 state_xfer_id;
    rnet_u32 state_total_size;
    rnet_u32 state_payload_crc;
    rnet_u32 state_offset;
    rnet_u16 state_chunk_size;
    rnet_u32 state_ack_bytes;
    rnet_u8 state_chunk[RNET_STATE_CHUNK_MAX];
    rnet_u8 state_probe_match; /* PROBE_REPLY only; size/crc echoed in
                                * state_total_size / state_payload_crc */
    rnet_u8 state_probe_replied; /* PROBE only: seats whose reply already
                                  * landed (0 from older encoders) */
    /* RB_* rollback control */
    rnet_u32 rb_epoch_id;
    rnet_u32 rb_mismatch_tick;
    rnet_u32 rb_load_tick;
    rnet_u32 rb_target_tick;
    rnet_u8 rb_corrected_slot;
    rnet_u8 rb_initiator;
    rnet_u8 rb_flags; /* RB_SYNC only (uses the former pad byte) */
    rnet_u8 rb_slot;
    rnet_u32 rb_row_begin;
    rnet_u16 rb_row_count;
    RNetRbWireFrame rb_rows[RNET_RB_SEAL_ROWS_CHUNK_MAX];
    rnet_u32 rb_digest_master;
    rnet_u32 rb_digest_a;
    rnet_u32 rb_digest_b;
    rnet_u32 rb_digest_c;
    rnet_u32 rb_input_digest;
    rnet_u8 rb_match;
    rnet_u32 rb_through_tick;
    rnet_u32 rb_state_hash;
    rnet_u32 rb_resolved_through;
    /* MODSET / MODSET_ACK */
    char modset_text[RNET_MODSET_TEXT_MAX];
    rnet_u16 modset_len;
    rnet_u8 modset_status;
    char modset_reason[RNET_MODSET_REASON_MAX];
    /* SIO_MULTI_XFER */
    rnet_u8 sio_unit_id;
    rnet_u32 sio_xfer_seq;
    rnet_u16 sio_send;
    rnet_u8 sio_confirm; /* Confirm IRQ watermark (pad lo byte) */
    rnet_u8 sio_vblank;  /* local VBlank mod 256 (pad hi byte) */
} RNetDecodedPacket;

/* Add a generation trailer to rollback control packets. */
int rnet_proto_is_rb_control(rnet_u16 type);
int rnet_proto_tag_rb_epoch(rnet_u8 *buf, size_t size, size_t cap, rnet_u16 epoch);

/* Returns 0 on success. */
int rnet_proto_decode(const rnet_u8 *data, size_t len, rnet_u32 expect_magic, RNetDecodedPacket *out);

#ifdef __cplusplus
}
#endif

#endif /* RNET_PROTOCOL_H */
