#include "recomp_net/recomp_net.h"
#include "recomp_net/startup_profiles.h"

#include "ice/rnet_ice_internal.h"
#include "input/rnet_rings.h"
#include "platform/rnet_platform.h"
#include "protocol/rnet_protocol.h"
#include "transport/rnet_transport.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char *rnet_version_string(void)
{
    return "0.1.0";
}

rnet_u32 rnet_checksum(const void *data, size_t len)
{
    return rnet_proto_checksum((const rnet_u8 *)data, len);
}

typedef enum RNetSessionPhase
{
    RNET_PHASE_IDLE = 0,
    RNET_PHASE_LINKING,
    RNET_PHASE_READY,
    RNET_PHASE_RUNNING
} RNetSessionPhase;

/* Outbound STATE transfer. Chunks go out once and reach every receiver (the
 * relay / hub fans them out), but completion, the send window and
 * retransmission are all per receiver: the transfer is done only when every
 * seat in expect_mask has ACKed the whole blob, the window is anchored on the
 * SLOWEST receiver, and a receiver whose ACK stops advancing rewinds the
 * cursor to its own watermark. With one receiver this is exactly the former
 * single-peer sender. */
typedef struct RNetStateTx
{
    int active;
    int ready;
    rnet_u8 op;
    rnet_u8 slot;
    rnet_u32 xfer_id;
    rnet_u32 total;
    rnet_u32 crc;
    rnet_u8 *buf;
    rnet_u32 expect_mask;                  /* receivers the transfer waits on */
    rnet_u32 ack[RNET_MAX_SLOTS];          /* contiguous ACK per receiver */
    rnet_u64 ack_timer_ms[RNET_MAX_SLOTS]; /* last progress/timeout; 0 = none */
    rnet_u32 send_cursor;
    rnet_u64 last_tx_ms;
    rnet_u64 last_begin_ms;
    rnet_u64 last_progress_log_ms;
    rnet_u32 last_progress_acked;
    rnet_u64 start_ms;
    /* AIMD pacing for STATE_CHUNK (esp. ICE/TURN — juice drops on flood). */
    rnet_u32 cwnd;       /* in-flight byte budget */
    rnet_u32 chunks_cap; /* max chunks emitted per pump */
    rnet_u32 ack_timeout_ms;
} RNetStateTx;

/* Inbound STATE transfer from one source seat. */
typedef struct RNetStateRx
{
    int active;
    int ready;
    rnet_u8 op;
    rnet_u8 slot;
    rnet_u32 xfer_id;
    rnet_u32 total;
    rnet_u32 crc;
    rnet_u32 contiguity; /* bytes from 0 received */
    rnet_u8 *buf;
    rnet_u64 last_ack_ms;
    rnet_u64 start_ms;
    rnet_u8 bits[(RNET_STATE_MAX_CHUNKS + 7u) / 8u];
} RNetStateRx;

#define RNET_STATE_TAKEN_TX RNET_MAX_SLOTS

typedef struct RNetIceHubBridge
{
    struct RNetSession *s;
    int slot;
} RNetIceHubBridge;

struct RNetSession
{
    /* Fixed startup agreement. Reserved RB_SYNC ops are consumed here, never
     * by the rollback driver's episode queue. Kept across rollback/resync. */
    struct {
        int active, refused;
        rnet_u32 qualification, occupied, offers, acks, acknowledged_by;
        rnet_u32 selected, transcript;
        RNetProfileCaps caps[RNET_MAX_SLOTS];
        rnet_u64 last_tx;
        int dirty;
    } profiles;
    RNetConfig cfg;
    RNetHostVTable host;
    RNetTransport transport;
    RNetIceAgent *ice;
    /* ICE hub (host-as-relay): one agent per guest seat in ice_hub_mask. */
    RNetIceAgent *ice_hub[RNET_MAX_SLOTS];
    RNetIceHubBridge ice_hub_bridge[RNET_MAX_SLOTS];
    rnet_u32 ice_hub_mask;
    RNetSessionPhase phase;
    RNetInputRing local_ring;
    RNetInputRing remote_rings[RNET_MAX_SLOTS];
    rnet_u8 peer_ready[RNET_MAX_SLOTS];
    rnet_u8 local_ready;
    rnet_u8 start_sent;
    rnet_u8 delay;
    /* Mid-session DELAY_SYNC: apply when sim_tick reaches effective_tick. */
    int delay_pending;
    rnet_u8 delay_pending_value;
    rnet_u32 delay_pending_effective;
    rnet_u64 delay_pending_last_tx_ms;
    rnet_u32 sim_tick;
    rnet_u32 highest_remote_ack;
    /* Multi-seat INPUT carries one contiguous acknowledgment per source.
     * UINT32_MAX means the receiver has not yet seen tick zero. */
    rnet_u32 remote_contiguous_ack[RNET_MAX_SLOTS];
    rnet_u32 peer_ack_tick[RNET_MAX_SLOTS];
    rnet_u8 peer_ack_seen[RNET_MAX_SLOTS];
    rnet_u64 last_hello_ms;
    rnet_u64 last_ready_ms;
    rnet_u64 last_input_ms;
    rnet_u32 last_input_tip;
    int last_input_tip_valid;
    int is_sim_authority; /* local_slot == 0 sends START */
    /* 1 when this session owns no seat: it simulates every slot from the
     * wire, contributes no input, and never becomes sim authority. */
    int is_observer;
    /* Sender id on the wire. Equal to cfg.local_slot for a seated peer; for
     * an observer it is a slot in the RELAY's namespace, which is what makes
     * the relay recognise it as a spectator and refuse to forward it. Never
     * used to index a per-seat array -- see the guards below. */
    rnet_u8 wire_slot;
    /* Resolved hashes and peer confirmations are prepared up
     * to D ticks ahead, so strict agreement normally completes before admit. */
    rnet_u32 published_tick[RNET_HISTORY_LENGTH];
    rnet_u32 published_hash[RNET_HISTORY_LENGTH];
    rnet_u8 published_valid[RNET_HISTORY_LENGTH];
    rnet_u32 peer_history_tick[RNET_HISTORY_LENGTH][RNET_MAX_SLOTS];
    rnet_u32 peer_history_hash[RNET_HISTORY_LENGTH][RNET_MAX_SLOTS];
    rnet_u8 peer_history_valid[RNET_HISTORY_LENGTH][RNET_MAX_SLOTS];
    rnet_u64 confirm_last_sent_ms[RNET_HISTORY_LENGTH];
    int input_desync;
    rnet_u32 desync_tick;
    rnet_u32 desync_local_hash;
    rnet_u32 desync_remote_hash;
    /* Peer liveness. Aggregate: any valid packet stamps last_peer_rx_ms and a
     * BYE from anyone sets peer_gone -- the two-seat view, kept as it was.
     * Per seat: the same two facts indexed by the packet's sender slot, so a
     * seat that went silent in a room of four is visible instead of hiding
     * behind the others' traffic. Indexed by wire slot; seats are < 8. */
    rnet_u64 last_peer_rx_ms;
    rnet_u64 session_start_ms;
    int peer_gone;
    rnet_u64 peer_rx_ms[RNET_MAX_SLOTS]; /* 0 = never heard from */
    rnet_u32 peer_gone_mask;             /* bit i = seat i sent BYE */
    /* Chunked blob transfer. One outbound (tx) at a time; inbound (rx) is
     * per SOURCE seat so several guests can upload MEMCARD to the host at
     * once without one BEGIN clobbering another's receive. */
    RNetStateTx tx;
    RNetStateRx rx[RNET_MAX_SLOTS];
    /* Which transfer the last take_ready reported: -1 none, RNET_MAX_SLOTS =
     * tx, else rx[source]. state_finish finishes that one. */
    int state_taken;
    int state_stall_sim; /* probe + all transfers stall admit until finished */
    rnet_u32 state_next_xfer_id;
    /* Incoming transfer history is per sender, independent of our outgoing
     * receipts. Survives state_clear/hard_resync so a reordered BEGIN cannot
     * resurrect an old transfer or displace a newer one. */
    struct {
        rnet_u32 id, total, crc;
        rnet_u8 op, slot;
        int finished;
        rnet_u64 last_reack_ms; /* paces re-ACKs of a finished transfer */
    } state_received[RNET_MAX_SLOTS];
    /* Survives state_clear — warm-start the next ICE transfer in-session. */
    rnet_u32 state_sticky_cwnd;
    rnet_u32 state_sticky_chunks;
    /* Hash probe before transfer (host announce → guest reply). The prober
     * waits for a reply from every seat in expect_mask. */
    int state_probe_active;
    int state_probe_sender;
    rnet_u32 state_probe_expect_mask; /* host: seats that must answer */
    rnet_u32 state_probe_reply_mask;  /* host: seats that answered */
    rnet_u32 state_probe_match_mask;  /* host: seats that answered "match" */
    int state_probe_pending;     /* guest: awaiting app reply */
    int state_probe_match;
    rnet_u8 state_probe_op;
    rnet_u8 state_probe_slot;
    rnet_u32 state_probe_size;
    rnet_u32 state_probe_crc;
    rnet_u64 state_probe_last_tx_ms;
    /* Guest: answered "match" to a SAVE/SRAM/RB_KF hash probe and holds its
     * input until the host's STATE_PROBE_DONE (every seat matched) or a
     * STATE_BEGIN (someone missed). Re-sends the reply while parked. */
    int state_probe_parked;
    /* Host: the last probe generation released with STATE_PROBE_DONE, so a
     * parked guest whose DONE was lost is released again on its retry. */
    int state_probe_released_valid;
    rnet_u8 state_probe_released_op;
    rnet_u8 state_probe_released_slot;
    rnet_u32 state_probe_released_size;
    rnet_u32 state_probe_released_crc;
    /* When set, pump must not emit INPUT bundles. Used across LOAD apply/ready
     * so pre-resync tip rows cannot clobber the post-hard_resync epoch
     * (tick % RNET_HISTORY_LENGTH collisions). Cleared by prime_delay_inputs. */
    int input_send_suppress;
    /* Bumped on hard_resync. INPUT/CONFIRM carry this; other-epoch packets are
     * dropped so in-flight tips from a prior sim_tick=0 era cannot first-wins. */
    rnet_u16 input_epoch;
    /* Diagnostics (JSONL / HUD). */
    RNetAdmitStall last_stall;
    rnet_u32 consecutive_stalls;
    rnet_u32 admit_ok_count;
    rnet_u32 stall_streaks;
    rnet_u64 stall_started_ms;
    rnet_u32 last_admit_wait_ms;
    rnet_u32 max_admit_wait_ms;
    rnet_u32 packets_rx;
    rnet_u32 input_bundle_sends;
    /* §56 pipeline diagnostics: monotonic arrival stamp per remote wire row
     * (first-wins, mirrors remote_rings latching). Consumption slack =
     * now - arrival when the row is finally needed at admit. */
    rnet_u64 remote_arr_ms[RNET_MAX_SLOTS][RNET_HISTORY_LENGTH];
    rnet_u32 remote_arr_tick[RNET_MAX_SLOTS][RNET_HISTORY_LENGTH];
    /* ICE TURN auto-fallback timers (monotonic ms). */
    rnet_u64 ice_attempt_ms;
    rnet_u64 ice_completed_ms;
    /* Peer RB_FRAME_COMMIT queue (host drains via take_*). Sized for more than
     * one peer: every peer sends one per tick, and an INCREMENTAL host does not
     * drain while it replays, so four seats fill 64 entries in about twenty
     * ticks. Each entry keeps its sender -- a hash chain per peer needs it. */
#define RNET_RB_FC_QUEUE 256
    /* PSX-Link group scoping: when >= 0, rollback EPISODE/STATE packets
     * (SYNC, BASELINE, POST, STATE_*) are accepted only from this slot —
     * episode coordination is per-console-group in link sessions. FRAME_COMMIT
     * is NOT filtered (all seats emit the same machine-level pair fold), and
     * input-plane packets (INPUT, INPUT_CONFIRM, SEAL_ROWS, RESOLVED) are
     * session-wide. -1 = accept all (default). */
    int rb_peer_slot;
    rnet_u32 rb_fc_tick[RNET_RB_FC_QUEUE];
    rnet_u32 rb_fc_hash[RNET_RB_FC_QUEUE];
    rnet_u8 rb_fc_from[RNET_RB_FC_QUEUE];
    int rb_fc_q_head; /* next write */
    int rb_fc_q_tail; /* next read */
    int rb_fc_q_count;

    /* Peer RB episode control queues (small FIFOs). 32, not 8: with more than
     * two seats every peer's IDENT, COMMIT, QUIESCE and BEGIN land in the same
     * queue between two drains, and a hub relays them in bursts. A full queue
     * refuses (and logs) rather than overwriting. */
#define RNET_RB_CTRL_QUEUE 32
    struct {
        rnet_u32 epoch_id, mismatch_tick, load_tick, target_tick;
        rnet_u8 corrected_slot, initiator, flags;
        rnet_u8 from; /* sender's wire slot (packet header) */
    } rb_sync_q[RNET_RB_CTRL_QUEUE];
    int rb_sync_head, rb_sync_tail, rb_sync_count;

    struct {
        rnet_u32 epoch_id, mismatch_tick, target_tick, row_begin;
        rnet_u8 slot;
        rnet_u16 row_count;
        RNetRbWireFrame rows[RNET_RB_SEAL_ROWS_CHUNK_MAX];
        rnet_u8 from;
    } rb_seal_q[RNET_RB_CTRL_QUEUE];
    int rb_seal_head, rb_seal_tail, rb_seal_count;

    struct {
        rnet_u32 epoch_id, load_tick, digest_master, digest_a, digest_b, digest_c;
        rnet_u8 from;
    } rb_base_q[RNET_RB_CTRL_QUEUE];
    int rb_base_head, rb_base_tail, rb_base_count;

    struct {
        rnet_u32 epoch_id, target_tick, digest_master, input_digest;
        rnet_u8 match;
        rnet_u8 from;
    } rb_post_q[RNET_RB_CTRL_QUEUE];
    int rb_post_head, rb_post_tail, rb_post_count;
    /* Sender slot of the most recent rb_* take (-1 = none yet). A two-seat
     * host has one peer and never needs it; an N-seat episode has to know
     * WHICH peer a BASELINE / POST / COMMIT answers for, or the first reply
     * would stand in for every peer's. See rnet_session_rb_last_take_from. */
    int rb_last_from;
    /* Episode-control datagrams refused because their queue was full. These
     * used to vanish without a trace; a dropped BEGIN or POST then surfaced
     * seconds later as a watchdog abort that named the wrong cause. */
    rnet_u32 rb_ctrl_dropped;

    /* Mod-set handshake. Latest-only by design: see the header. The ACK is
     * latest-only PER SEAT: with more than two seats every guest answers the
     * host, and a single slot let the second answer overwrite the first --
     * the host then waited out its bound for a guest that had already
     * confirmed, and refused the match. */
    rnet_u8 peer_wire_version_mismatch; /* a HELLO of another wire version (0xFF = 0) */
    char modset_text[RNET_MODSET_TEXT_MAX];
    rnet_u8 modset_pending;
    char modset_ack_reason[RNET_MAX_SLOTS][RNET_MODSET_REASON_MAX];
    rnet_u8 modset_ack_status[RNET_MAX_SLOTS];
    rnet_u32 modset_ack_pending; /* bit i = seat i's answer is waiting */
    /* RESOLVED is a watermark, so the queue holds at most one entry per
     * sender (the highest): with more than two seats the receiver must know
     * WHICH peer has resolved how far, not just that somebody has. */
    struct {
        rnet_u32 through;
        rnet_u8 from;
    } rb_resolved_q[RNET_RB_CTRL_QUEUE];
    int rb_resolved_head, rb_resolved_tail, rb_resolved_count;

    /* Peer GBA Multi SEND barrier (0-delay; not pad INPUT). */
#define RNET_SIO_XFER_QUEUE 32
    struct {
        rnet_u32 seq;
        rnet_u16 send;
        rnet_u16 confirm_pad; /* lo=confirm IRQ, hi=vblank mod 256 */
        rnet_u8 unit_id;
    } sio_xfer_q[RNET_SIO_XFER_QUEUE];
    int sio_xfer_head, sio_xfer_tail, sio_xfer_count;
};

static rnet_u64 session_now(RNetSession *s)
{
    if (s->host.now_ms != NULL)
    {
        return s->host.now_ms(s->host.ctx);
    }
    return rnet_os_monotonic_ms();
}

const char *rnet_admit_stall_name(RNetAdmitStall reason)
{
    switch (reason) {
    case RNET_ADMIT_OK: return "ok";
    case RNET_ADMIT_NOT_RUNNING: return "not_running";
    case RNET_ADMIT_STATE_XFER: return "state_xfer";
    case RNET_ADMIT_SIM_MISMATCH: return "sim_mismatch";
    case RNET_ADMIT_DESYNC: return "desync";
    case RNET_ADMIT_WAIT_LOCAL_INPUT: return "wait_local_input";
    case RNET_ADMIT_WAIT_REMOTE_INPUT: return "wait_remote_input";
    case RNET_ADMIT_WAIT_CONFIRM: return "wait_confirm";
    default: return "unknown";
    }
}

static void note_admit_stall(RNetSession *s, RNetAdmitStall reason)
{
    rnet_u64 now;
    if (s == NULL)
        return;
    s->last_stall = reason;
    if (s->consecutive_stalls == 0)
        s->stall_streaks++;
    s->consecutive_stalls++;
    now = session_now(s);
    if (s->stall_started_ms == 0)
        s->stall_started_ms = now ? now : 1;
    s->last_admit_wait_ms = (rnet_u32)(now - s->stall_started_ms);
    if (s->last_admit_wait_ms > s->max_admit_wait_ms)
        s->max_admit_wait_ms = s->last_admit_wait_ms;
}

static void note_admit_ok(RNetSession *s)
{
    rnet_u64 now;
    if (s == NULL)
        return;
    now = session_now(s);
    if (s->stall_started_ms != 0) {
        s->last_admit_wait_ms = (rnet_u32)(now - s->stall_started_ms);
        if (s->last_admit_wait_ms > s->max_admit_wait_ms)
            s->max_admit_wait_ms = s->last_admit_wait_ms;
        s->stall_started_ms = 0;
    } else {
        s->last_admit_wait_ms = 0;
    }
    s->last_stall = RNET_ADMIT_OK;
    s->consecutive_stalls = 0;
    s->admit_ok_count++;
}

static void send_raw(RNetSession *s, const rnet_u8 *buf, int len);

/* Additive RB_SYNC startup ops, deliberately separate from episode state.
 * Offers are copied once. Retries do not observe or change host capabilities. */
#define PROFILE_OFFER RNET_RB_SYNC_OP_PROFILE_OFFER
#define PROFILE_ACK RNET_RB_SYNC_OP_PROFILE_ACK

static rnet_u32 profile_fold(rnet_u32 h, rnet_u32 word)
{
    int byte;
    for (byte = 0; byte < 4; ++byte) {
        h = (h ^ (word & 255u)) * 16777619u;
        word >>= 8;
    }
    return h;
}

static void profiles_choose(RNetSession *s)
{
    rnet_u32 common = ~(rnet_u32)0, h = 2166136261u;
    int i;
    if (s->profiles.offers != s->profiles.occupied || s->profiles.acks) return;
    h = profile_fold(h, s->profiles.qualification);
    h = profile_fold(h, s->cfg.slot_count);
    h = profile_fold(h, s->profiles.occupied);
    for (i = 0; i < s->cfg.slot_count; ++i) {
        const RNetProfileCaps *c = &s->profiles.caps[i];
        if (!(s->profiles.occupied & (1u << i))) continue;
        common &= c->profiles;
        h = profile_fold(h, (rnet_u32)i);
        h = profile_fold(h, c->cpu_features);
        h = profile_fold(h, c->backends);
        h = profile_fold(h, c->profiles);
        h = profile_fold(h, c->architecture);
    }
    if (!(common & 1u)) { s->profiles.refused = 1; return; }
    for (i = 31; i > 0 && !(common & (1u << i)); --i) {}
    s->profiles.selected = (rnet_u32)i;
    s->profiles.transcript = profile_fold(h, (rnet_u32)i);
    s->profiles.acks = 1u << s->cfg.local_slot;
    s->profiles.acknowledged_by = s->profiles.acks;
    s->profiles.dirty = 1;
}

static void profiles_receive(RNetSession *s, const RNetDecodedPacket *p)
{
    rnet_u32 bit;
    if (!s->profiles.active || s->profiles.refused ||
        p->local_slot >= s->cfg.slot_count || p->local_slot == s->cfg.local_slot)
        return;
    bit = 1u << p->local_slot;
    if (!(s->profiles.occupied & bit)) return;
    if (p->rb_epoch_id != s->profiles.qualification || p->rb_flags != 0u) {
        s->profiles.refused = 1;
        return;
    }
    if (p->rb_initiator == PROFILE_OFFER) {
        RNetProfileCaps caps;
        caps.cpu_features = p->rb_mismatch_tick;
        caps.backends = p->rb_load_tick;
        caps.profiles = p->rb_target_tick;
        caps.architecture = p->rb_corrected_slot;
        if (!(caps.profiles & 1u) || ((s->profiles.offers & bit) &&
            memcmp(&caps, &s->profiles.caps[p->local_slot], sizeof(caps)) != 0)) {
            s->profiles.refused = 1; /* changed offer: never renegotiate */
            return;
        }
        s->profiles.caps[p->local_slot] = caps;
        s->profiles.offers |= bit;
        profiles_choose(s);
    } else if (s->profiles.offers == s->profiles.occupied) {
        if (p->rb_mismatch_tick != s->profiles.selected ||
            p->rb_load_tick != s->profiles.transcript ||
            p->rb_corrected_slot != 0u ||
            !(p->rb_target_tick & bit) ||
            (p->rb_target_tick & ~s->profiles.occupied)) {
            s->profiles.refused = 1;
            return;
        }
        if (!(s->profiles.acks & bit)) s->profiles.dirty = 1;
        s->profiles.acks |= bit;
        if (p->rb_target_tick & (1u << s->cfg.local_slot))
            s->profiles.acknowledged_by |= bit;
    }
}

static void profiles_drive(RNetSession *s)
{
    rnet_u64 now;
    const RNetProfileCaps *c;
    if (!s->profiles.active || s->profiles.refused || s->phase != RNET_PHASE_RUNNING)
        return;
    if (!s->profiles.dirty && s->profiles.acknowledged_by == s->profiles.occupied)
        return;
    now = session_now(s);
    if (!s->profiles.dirty && s->profiles.last_tx && now - s->profiles.last_tx < 100u)
        return;
    c = &s->profiles.caps[s->cfg.local_slot];
    rnet_session_send_rb_sync(s, s->profiles.qualification, c->cpu_features,
        c->backends, c->profiles, (rnet_u8)c->architecture, PROFILE_OFFER, 0u);
    if (s->profiles.acks)
        rnet_session_send_rb_sync(s, s->profiles.qualification, s->profiles.selected,
            s->profiles.transcript, s->profiles.acks, 0u, PROFILE_ACK, 0u);
    s->profiles.last_tx = now ? now : 1u;
    s->profiles.dirty = 0;
}

int rnet_session_profiles_begin(RNetSession *s, rnet_u32 qualification,
                                const RNetProfileCaps *caps)
{
    int i;
    if (!s || !caps || !qualification || !(caps->profiles & 1u) ||
        caps->architecture > 255u || s->is_observer || s->profiles.active ||
        !rnet_config_slot_occupied(&s->cfg, s->cfg.local_slot) ||
        s->phase != RNET_PHASE_IDLE || s->sim_tick != 0u) return -1;
    for (i = 0; i < s->cfg.slot_count; ++i)
        if (rnet_config_slot_occupied(&s->cfg, (rnet_u8)i))
            s->profiles.occupied |= 1u << i;
    s->profiles.active = 1;
    s->profiles.qualification = qualification;
    s->profiles.caps[s->cfg.local_slot] = *caps;
    s->profiles.offers = 1u << s->cfg.local_slot;
    s->profiles.dirty = 1;
    profiles_choose(s);
    return 0;
}

int rnet_session_profiles_status(const RNetSession *s, rnet_u32 *profile,
                                 rnet_u32 *transcript)
{
    if (!s || !s->profiles.active || s->profiles.refused) return -1;
    if (s->profiles.acks != s->profiles.occupied) return 0;
    if (profile) *profile = s->profiles.selected;
    if (transcript) *transcript = s->profiles.transcript;
    return 1;
}

int rnet_session_profiles_peer(const RNetSession *s, int seat, RNetProfileCaps *out)
{
    if (!s || !out || seat < 0 || seat >= s->cfg.slot_count ||
        !(s->profiles.offers & (1u << seat))) return 0;
    *out = s->profiles.caps[seat];
    return 1;
}

/* An episode-control queue was full, so this datagram is refused. Said out
 * loud: the first one and every 64th after, so a storm cannot flood the log
 * but a single loss is never invisible. */
static void rb_ctrl_note_drop(RNetSession *s, const char *what)
{
    s->rb_ctrl_dropped++;
    if (s->rb_ctrl_dropped == 1u || (s->rb_ctrl_dropped % 64u) == 0u)
    {
        fprintf(stderr,
                "recomp_net: %s dropped -- episode-control queue full "
                "(%d entries, %u dropped this session); the host is not "
                "draining rb_* messages between pumps\n",
                what, RNET_RB_CTRL_QUEUE, (unsigned)s->rb_ctrl_dropped);
    }
}
/* Episode-control datagrams are retransmitted until answered, and a host
 * that is busy in an inline replay drains them only at present edges. With
 * three peers each resending POST / SEAL_ROWS every pump, a 32-entry FIFO
 * filled with copies of the same few messages and then refused the next new
 * one (1,344 drops on a 4-seat WAN race). A retransmit of a message already
 * waiting is therefore coalesced into it, and a full queue first sheds its
 * oldest entry when that entry belongs to an older episode than the incoming
 * one (the host would discard it on take anyway). */
static int rb_epoch_before(rnet_u32 a, rnet_u32 b)
{
    return (rnet_s32)(a - b) < 0;
}
#define RB_Q_AT(tail, i) (((tail) + (i)) % RNET_RB_CTRL_QUEUE)

static void send_input_bundle(RNetSession *s);
static void apply_pending_delay(RNetSession *s);
static void emit_delay_sync(RNetSession *s, rnet_u8 new_delay, rnet_u32 effective_tick);
static void seed_delay_prefix(RNetSession *s);
static void state_clear(RNetSession *s);
static void state_probe_clear(RNetSession *s);
static int state_any_active(const RNetSession *s);
static void state_drive_sender(RNetSession *s);
static void state_drive_probe(RNetSession *s);
static void state_on_begin(RNetSession *s, const RNetDecodedPacket *pkt);
static void state_on_chunk(RNetSession *s, const RNetDecodedPacket *pkt);
static void state_on_ack(RNetSession *s, const RNetDecodedPacket *pkt);
static void state_on_probe(RNetSession *s, const RNetDecodedPacket *pkt);
static void state_on_probe_reply(RNetSession *s, const RNetDecodedPacket *pkt);
static void state_on_probe_done(RNetSession *s, const RNetDecodedPacket *pkt);
static void state_send_probe_done(RNetSession *s);

#if defined(RNET_ENABLE_ICE)
static void ice_emit_bridge(const RNetSignal *msg, void *user)
{
    RNetSession *s = (RNetSession *)user;
    if ((s != NULL) && (s->host.on_signal != NULL) && (msg != NULL))
    {
        s->host.on_signal(msg, s->host.ctx);
    }
}

static void ice_hub_emit_bridge(const RNetSignal *msg, void *user)
{
    RNetIceHubBridge *b = (RNetIceHubBridge *)user;
    if ((b != NULL) && (b->s != NULL) && (b->s->host.on_signal != NULL) && (msg != NULL))
    {
        RNetSignal out = *msg;
        out.peer_slot = (rnet_u8)b->slot;
        b->s->host.on_signal(&out, b->s->host.ctx);
    }
}

static int ice_send_bridge(void *ice_ctx, const rnet_u8 *buf, size_t len)
{
    return rnet_ice_agent_send((RNetIceAgent *)ice_ctx, buf, len);
}

static int ice_recv_bridge(void *ice_ctx, rnet_u8 *buf, size_t cap, size_t *out_len)
{
    return rnet_ice_agent_recv((RNetIceAgent *)ice_ctx, buf, cap, out_len);
}
#endif /* RNET_ENABLE_ICE */

static int remote_tick_in_live_window(const RNetSession *s, rnet_u8 slot, rnet_u32 tick)
{
    rnet_u32 tip;
    rnet_u32 slop;
    rnet_u32 lo;
    rnet_u32 hi;
    rnet_u32 remote_hi;
    rnet_u32 ancient;
    if (s == NULL || s->phase != RNET_PHASE_RUNNING)
    {
        return 1;
    }
    tip = rnet_wire_tick_from_sim(s->sim_tick, s->delay);
    slop = (rnet_u32)s->cfg.bundle_redundancy + 8u;
    if (slop < 8u)
    {
        slop = 8u;
    }
    lo = (s->sim_tick > slop) ? (s->sim_tick - slop) : 0u;
    hi = tip + slop;
    if (tick >= lo && tick <= hi)
    {
        return 1;
    }
    /* Rematch / asymmetric boot: the faster peer invents up to P ahead, then
     * pcap_freeze. By then lo = sim-slop can sit above the stalled peer tip
     * (e.g. remote=5, sim=15, lo=7) so tip+1 is dropped forever and freeze
     * never clears. Accept gap-filling tips that extend the remote watermark
     * even when below lo; still reject ancient hard_resync residue. */
    if (slot >= s->cfg.slot_count)
    {
        return 0;
    }
    remote_hi = rnet_ring_highest_valid(&s->remote_rings[slot]);
    ancient = 64u;
    if (tick > remote_hi && tick <= hi &&
        (s->sim_tick <= ancient || tick + ancient >= s->sim_tick))
    {
        return 1;
    }
    return 0;
}

static void store_remote_frame(RNetSession *s, rnet_u8 slot, const RNetWireFrame *frame)
{
    RNetInputSample sample;
    RNetInputSample existing;
    if ((s == NULL) || (frame == NULL) || (slot >= s->cfg.slot_count) || (slot == s->cfg.local_slot))
    {
        return;
    }
    /* Drop previous-epoch residue after hard_resync (sim_tick→0). Those ticks
     * share ring slots with the new tip via tick%HISTORY and first-wins would
     * otherwise keep remotes_ready_for_sim failing until the peer stops. */
    if (!remote_tick_in_live_window(s, slot, frame->tick))
    {
        return;
    }
    /* First-wins: later retransmits must not overwrite a latched wire row. */
    if (rnet_ring_get(&s->remote_rings[slot], frame->tick, &existing))
    {
        return;
    }
    memset(&sample, 0, sizeof(sample));
    sample.tick = frame->tick;
    sample.size = frame->size;
    if (frame->size > 0)
    {
        memcpy(sample.bytes, frame->bytes, frame->size);
    }
    sample.valid = 1;
    rnet_ring_store(&s->remote_rings[slot], &sample);
    if (s->cfg.slot_count > 2)
    {
        rnet_u32 next = s->remote_contiguous_ack[slot] + 1u;
        while (rnet_ring_get(&s->remote_rings[slot], next, &existing))
        {
            s->remote_contiguous_ack[slot] = next;
            if (next == 0xffffffffu)
                break;
            ++next;
        }
    }
    {
        rnet_u32 idx = frame->tick % RNET_HISTORY_LENGTH;
        rnet_u64 now = session_now(s);
        s->remote_arr_tick[slot][idx] = frame->tick;
        s->remote_arr_ms[slot][idx] = now ? now : 1u;
    }
}

static rnet_u32 hash_resolved_inputs(rnet_u32 sim_tick, const RNetInputSample *by_slot, int slots)
{
    rnet_u8 buf[4 + RNET_MAX_SLOTS * (2 + RNET_INPUT_MAX)];
    size_t n = 0;
    int i;

    buf[n++] = (rnet_u8)(sim_tick & 0xFFu);
    buf[n++] = (rnet_u8)((sim_tick >> 8) & 0xFFu);
    buf[n++] = (rnet_u8)((sim_tick >> 16) & 0xFFu);
    buf[n++] = (rnet_u8)((sim_tick >> 24) & 0xFFu);
    for (i = 0; i < slots; ++i)
    {
        rnet_u16 sz = by_slot[i].size;
        if (sz > RNET_INPUT_MAX)
        {
            sz = RNET_INPUT_MAX;
        }
        buf[n++] = (rnet_u8)(sz & 0xFFu);
        buf[n++] = (rnet_u8)((sz >> 8) & 0xFFu);
        if (sz > 0)
        {
            memcpy(buf + n, by_slot[i].bytes, sz);
            n += sz;
        }
    }
    return rnet_proto_checksum(buf, n);
}

static void send_input_confirm_tick(RNetSession *s, rnet_u32 tick,
                                    rnet_u32 hash)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    int len;
    rnet_u64 now;

    if (s == NULL) return;
    len = rnet_proto_encode_input_confirm(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id, s->wire_slot,
                                          s->input_epoch, tick, hash);
    send_raw(s, buf, len);
    now = session_now(s);
    s->confirm_last_sent_ms[tick % RNET_HISTORY_LENGTH] = now;
}

static void handle_decoded(RNetSession *s, const RNetDecodedPacket *pkt)
{
    int i;
    if ((s == NULL) || (pkt == NULL))
    {
        return;
    }
    if (rnet_proto_is_rb_control(pkt->type) && pkt->input_epoch != s->input_epoch)
        return;
    if (pkt->session_id != s->cfg.session_id)
    {
        return;
    }
    switch (pkt->type)
    {
    case RNET_PKT_HELLO:
        if (pkt->slot_count != s->cfg.slot_count)
        {
            break;
        }
        if (pkt->wire_version != RNET_WIRE_VERSION)
        {
            /* A different wire (e.g. the 7-byte seal row) would misparse
             * mid-match: never link with it. */
            if (s->peer_wire_version_mismatch == 0)
                fprintf(stderr,
                        "recomp-net: peer slot %u speaks wire version %u, this "
                        "build %u — not linking (update both to the same build)\n",
                        (unsigned)pkt->local_slot, (unsigned)pkt->wire_version,
                        (unsigned)RNET_WIRE_VERSION);
            s->peer_wire_version_mismatch = pkt->wire_version ? pkt->wire_version : 0xFFu;
            break;
        }
        /* Peer is alive; move toward READY once we have exchanged HELLO. */
        if (s->phase == RNET_PHASE_LINKING)
        {
            s->phase = RNET_PHASE_READY;
        }
        /* Guest adopts host delay before RUNNING (host slot 0 is authoritative). */
        if (s->cfg.local_slot != 0 && pkt->local_slot == 0)
        {
            rnet_u8 hello_delay = pkt->delay;
            if (hello_delay >= 2 && hello_delay <= 20 && hello_delay != s->delay)
            {
                if (s->phase != RNET_PHASE_RUNNING || s->sim_tick == 0)
                {
                    s->delay = hello_delay;
                    s->cfg.input_delay = hello_delay;
                    if (s->phase != RNET_PHASE_RUNNING)
                        seed_delay_prefix(s);
                }
                else if (s->sim_tick > 0)
                {
                    static int delay_mismatch_logged;
                    if (!delay_mismatch_logged)
                    {
                        fprintf(stderr,
                                "recomp-net: HELLO delay mismatch local=%u peer=%u "
                                "after RUNNING — not adopting\n",
                                (unsigned)s->delay, (unsigned)hello_delay);
                        delay_mismatch_logged = 1;
                    }
                }
            }
        }
        break;
    case RNET_PKT_READY:
        if (pkt->local_slot < s->cfg.slot_count)
        {
            s->peer_ready[pkt->local_slot] = 1;
            /* START goes out once (maybe_bootstrap). A seat only sends READY
             * until it has seen START, so a READY that reaches the authority
             * after it started is a seat whose START was lost -- and nothing
             * else would ever move that seat to RUNNING: the rest of the room
             * starts, waits on its input, and the match never begins. Answer
             * it with START again (tick 0, as the first one said; a seat that
             * is already running ignores it). Paced by the peer's READY
             * (100 ms). Seats only: an observer's wire slot is at or past
             * slot_count and never reaches here. */
            if (s->is_sim_authority && s->start_sent && s->phase == RNET_PHASE_RUNNING &&
                rnet_config_slot_occupied(&s->cfg, pkt->local_slot))
            {
                rnet_u8 sbuf[RNET_MAX_PACKET];
                int slen = rnet_proto_encode_start(sbuf, sizeof(sbuf), s->cfg.protocol_magic,
                                                   s->cfg.session_id, 0);
                send_raw(s, sbuf, slen);
            }
        }
        break;
    case RNET_PKT_START:
        if (s->phase != RNET_PHASE_RUNNING)
        {
            s->sim_tick = pkt->start_tick;
            s->phase = RNET_PHASE_RUNNING;
            seed_delay_prefix(s);
            send_input_bundle(s);
        }
        break;
    case RNET_PKT_INPUT:
        /* Drop prior-epoch tips (rapid rematch hard_resync → sim=0). */
        if (pkt->input_epoch != s->input_epoch)
        {
            break;
        }
        for (i = 0; i < pkt->frame_count; ++i)
        {
            store_remote_frame(s, pkt->local_slot, &pkt->frames[i]);
        }
        if (s->cfg.slot_count > 2 && pkt->local_slot < s->cfg.slot_count &&
            pkt->ack_count == s->cfg.slot_count &&
            rnet_config_slot_occupied(&s->cfg, pkt->local_slot))
        {
            rnet_u32 ack = pkt->acks[s->cfg.local_slot];
            if (!s->peer_ack_seen[pkt->local_slot] ||
                (ack != 0xffffffffu &&
                 (s->peer_ack_tick[pkt->local_slot] == 0xffffffffu ||
                  ack > s->peer_ack_tick[pkt->local_slot])))
            {
                s->peer_ack_tick[pkt->local_slot] = ack;
                s->peer_ack_seen[pkt->local_slot] = 1;
            }
        }
        else if (s->cfg.slot_count == 2 && pkt->ack_tick > s->highest_remote_ack)
        {
            s->highest_remote_ack = pkt->ack_tick;
        }
        break;
    case RNET_PKT_DELAY_SYNC:
        /* Immediate when already past effective_tick (or not RUNNING yet).
         * Otherwise queue so both peers commit on the same sim tick. */
        if (pkt->effective_tick <= s->sim_tick || s->phase != RNET_PHASE_RUNNING)
        {
            s->delay = pkt->new_delay;
            s->cfg.input_delay = pkt->new_delay;
            s->delay_pending = 0;
        }
        else
        {
            s->delay_pending = 1;
            s->delay_pending_value = pkt->new_delay;
            s->delay_pending_effective = pkt->effective_tick;
        }
        break;
    case RNET_PKT_INPUT_CONFIRM:
        if (pkt->input_epoch != s->input_epoch)
        {
            break;
        }
        if (pkt->local_slot < s->cfg.slot_count)
        {
            rnet_u32 index = pkt->confirm_sim_tick % RNET_HISTORY_LENGTH;
            s->peer_history_tick[index][pkt->local_slot] = pkt->confirm_sim_tick;
            s->peer_history_hash[index][pkt->local_slot] = pkt->confirm_hash;
            s->peer_history_valid[index][pkt->local_slot] = 1;
            if (s->published_valid[index] &&
                s->published_tick[index] == pkt->confirm_sim_tick &&
                s->published_hash[index] != pkt->confirm_hash)
            {
                s->input_desync = 1;
                s->desync_tick = pkt->confirm_sim_tick;
                s->desync_local_hash = s->published_hash[index];
                s->desync_remote_hash = pkt->confirm_hash;
            }
        }
        break;
    case RNET_PKT_BYE:
        if (pkt->local_slot != s->wire_slot)
        {
            /* Aggregate: any peer's BYE, as before. Per seat: who said it. */
            s->peer_gone = 1;
            if (pkt->local_slot < RNET_MAX_SLOTS)
            {
                s->peer_gone_mask |= 1u << pkt->local_slot;
            }
        }
        break;
    case RNET_PKT_STATE_BEGIN:
        if (s->rb_peer_slot >= 0 && (int)pkt->local_slot != s->rb_peer_slot)
        {
            break;
        }
        state_on_begin(s, pkt);
        break;
    case RNET_PKT_STATE_CHUNK:
        if (s->rb_peer_slot >= 0 && (int)pkt->local_slot != s->rb_peer_slot)
        {
            break;
        }
        state_on_chunk(s, pkt);
        break;
    case RNET_PKT_STATE_ACK:
        if (s->rb_peer_slot >= 0 && (int)pkt->local_slot != s->rb_peer_slot)
        {
            break;
        }
        state_on_ack(s, pkt);
        break;
    case RNET_PKT_STATE_PROBE:
        if (s->rb_peer_slot >= 0 && (int)pkt->local_slot != s->rb_peer_slot)
        {
            break;
        }
        state_on_probe(s, pkt);
        break;
    case RNET_PKT_STATE_PROBE_REPLY:
        if (s->rb_peer_slot >= 0 && (int)pkt->local_slot != s->rb_peer_slot)
        {
            break;
        }
        state_on_probe_reply(s, pkt);
        break;
    case RNET_PKT_STATE_PROBE_DONE:
        if (s->rb_peer_slot >= 0 && (int)pkt->local_slot != s->rb_peer_slot)
        {
            break;
        }
        state_on_probe_done(s, pkt);
        break;
    case RNET_PKT_SIO_MULTI_XFER:
        if (pkt->local_slot != s->wire_slot)
        {
            rnet_u16 pad = (rnet_u16)(pkt->sio_confirm |
                                      ((rnet_u16)pkt->sio_vblank << 8));
            if (s->sio_xfer_count < RNET_SIO_XFER_QUEUE)
            {
                s->sio_xfer_q[s->sio_xfer_head].seq = pkt->sio_xfer_seq;
                s->sio_xfer_q[s->sio_xfer_head].send = pkt->sio_send;
                s->sio_xfer_q[s->sio_xfer_head].unit_id = pkt->sio_unit_id;
                s->sio_xfer_q[s->sio_xfer_head].confirm_pad = pad;
                s->sio_xfer_head = (s->sio_xfer_head + 1) % RNET_SIO_XFER_QUEUE;
                s->sio_xfer_count++;
            }
            else
            {
                /* Drop oldest so a live Cable Club burst can still progress. */
                s->sio_xfer_tail = (s->sio_xfer_tail + 1) % RNET_SIO_XFER_QUEUE;
                s->sio_xfer_count--;
                s->sio_xfer_q[s->sio_xfer_head].seq = pkt->sio_xfer_seq;
                s->sio_xfer_q[s->sio_xfer_head].send = pkt->sio_send;
                s->sio_xfer_q[s->sio_xfer_head].unit_id = pkt->sio_unit_id;
                s->sio_xfer_q[s->sio_xfer_head].confirm_pad = pad;
                s->sio_xfer_head = (s->sio_xfer_head + 1) % RNET_SIO_XFER_QUEUE;
                s->sio_xfer_count++;
            }
        }
        break;
    case RNET_PKT_RB_FRAME_COMMIT:
        /* Not rb_peer_slot-filtered: PSX-Link seats all emit the same
         * machine-level pair fold, so every peer's commit is comparable. */
        if (pkt->local_slot != s->wire_slot)
        {
            if (s->rb_fc_q_count < RNET_RB_FC_QUEUE)
            {
                s->rb_fc_tick[s->rb_fc_q_head] = pkt->rb_through_tick;
                s->rb_fc_hash[s->rb_fc_q_head] = pkt->rb_state_hash;
                s->rb_fc_from[s->rb_fc_q_head] = pkt->local_slot;
                s->rb_fc_q_head = (s->rb_fc_q_head + 1) % RNET_RB_FC_QUEUE;
                s->rb_fc_q_count++;
            }
            else
            {
                /* Drop oldest so the watermark can still track the tip. */
                s->rb_fc_q_tail = (s->rb_fc_q_tail + 1) % RNET_RB_FC_QUEUE;
                s->rb_fc_q_count--;
                s->rb_fc_tick[s->rb_fc_q_head] = pkt->rb_through_tick;
                s->rb_fc_hash[s->rb_fc_q_head] = pkt->rb_state_hash;
                s->rb_fc_from[s->rb_fc_q_head] = pkt->local_slot;
                s->rb_fc_q_head = (s->rb_fc_q_head + 1) % RNET_RB_FC_QUEUE;
                s->rb_fc_q_count++;
            }
        }
        break;
    case RNET_PKT_RB_SYNC:
        if (pkt->rb_initiator == PROFILE_OFFER || pkt->rb_initiator == PROFILE_ACK)
        {
            profiles_receive(s, pkt);
            break;
        }
        if (s->rb_peer_slot >= 0 && (int)pkt->local_slot != s->rb_peer_slot)
        {
            break;
        }
        if (pkt->local_slot != s->wire_slot)
        {
            int i, dup = 0;
            for (i = 0; i < s->rb_sync_count; ++i)
            {
                int k = RB_Q_AT(s->rb_sync_tail, i);
                if (s->rb_sync_q[k].from == pkt->local_slot &&
                    s->rb_sync_q[k].epoch_id == pkt->rb_epoch_id &&
                    s->rb_sync_q[k].mismatch_tick == pkt->rb_mismatch_tick &&
                    s->rb_sync_q[k].load_tick == pkt->rb_load_tick &&
                    s->rb_sync_q[k].target_tick == pkt->rb_target_tick &&
                    s->rb_sync_q[k].corrected_slot == pkt->rb_corrected_slot &&
                    s->rb_sync_q[k].initiator == pkt->rb_initiator &&
                    s->rb_sync_q[k].flags == pkt->rb_flags)
                {
                    dup = 1;
                    break;
                }
            }
            if (dup)
                break;
        }
        if (pkt->local_slot != s->wire_slot &&
            s->rb_sync_count < RNET_RB_CTRL_QUEUE)
        {
            s->rb_sync_q[s->rb_sync_head].epoch_id = pkt->rb_epoch_id;
            s->rb_sync_q[s->rb_sync_head].mismatch_tick = pkt->rb_mismatch_tick;
            s->rb_sync_q[s->rb_sync_head].load_tick = pkt->rb_load_tick;
            s->rb_sync_q[s->rb_sync_head].target_tick = pkt->rb_target_tick;
            s->rb_sync_q[s->rb_sync_head].corrected_slot = pkt->rb_corrected_slot;
            s->rb_sync_q[s->rb_sync_head].initiator = pkt->rb_initiator;
            s->rb_sync_q[s->rb_sync_head].flags = pkt->rb_flags;
            s->rb_sync_q[s->rb_sync_head].from = pkt->local_slot;
            s->rb_sync_head = (s->rb_sync_head + 1) % RNET_RB_CTRL_QUEUE;
            s->rb_sync_count++;
        }
        else if (pkt->local_slot != s->wire_slot)
        {
            rb_ctrl_note_drop(s, "RB_SYNC");
        }
        break;
    case RNET_PKT_RB_SEAL_ROWS:
        if (pkt->local_slot != s->wire_slot)
        {
            int i, dup = 0;
            for (i = 0; i < s->rb_seal_count; ++i)
            {
                int k = RB_Q_AT(s->rb_seal_tail, i);
                if (s->rb_seal_q[k].from == pkt->local_slot &&
                    s->rb_seal_q[k].epoch_id == pkt->rb_epoch_id &&
                    s->rb_seal_q[k].mismatch_tick == pkt->rb_mismatch_tick &&
                    s->rb_seal_q[k].target_tick == pkt->rb_target_tick &&
                    s->rb_seal_q[k].slot == pkt->rb_slot &&
                    s->rb_seal_q[k].row_begin == pkt->rb_row_begin &&
                    s->rb_seal_q[k].row_count ==
                        (pkt->rb_row_count > RNET_RB_SEAL_ROWS_CHUNK_MAX
                             ? RNET_RB_SEAL_ROWS_CHUNK_MAX : pkt->rb_row_count))
                {
                    dup = 1;
                    break;
                }
            }
            if (dup)
                break;
            while (s->rb_seal_count >= RNET_RB_CTRL_QUEUE &&
                   rb_epoch_before(s->rb_seal_q[s->rb_seal_tail].epoch_id, pkt->rb_epoch_id))
            {
                s->rb_seal_tail = (s->rb_seal_tail + 1) % RNET_RB_CTRL_QUEUE;
                s->rb_seal_count--;
            }
        }
        if (pkt->local_slot != s->wire_slot &&
            s->rb_seal_count < RNET_RB_CTRL_QUEUE)
        {
            s->rb_seal_q[s->rb_seal_head].epoch_id = pkt->rb_epoch_id;
            s->rb_seal_q[s->rb_seal_head].mismatch_tick = pkt->rb_mismatch_tick;
            s->rb_seal_q[s->rb_seal_head].target_tick = pkt->rb_target_tick;
            s->rb_seal_q[s->rb_seal_head].row_begin = pkt->rb_row_begin;
            s->rb_seal_q[s->rb_seal_head].slot = pkt->rb_slot;
            s->rb_seal_q[s->rb_seal_head].row_count = pkt->rb_row_count;
            if (pkt->rb_row_count > 0)
            {
                rnet_u16 n = pkt->rb_row_count;
                if (n > RNET_RB_SEAL_ROWS_CHUNK_MAX)
                    n = RNET_RB_SEAL_ROWS_CHUNK_MAX;
                memcpy(s->rb_seal_q[s->rb_seal_head].rows, pkt->rb_rows,
                       sizeof(RNetRbWireFrame) * n);
                s->rb_seal_q[s->rb_seal_head].row_count = n;
            }
            s->rb_seal_q[s->rb_seal_head].from = pkt->local_slot;
            s->rb_seal_head = (s->rb_seal_head + 1) % RNET_RB_CTRL_QUEUE;
            s->rb_seal_count++;
        }
        else if (pkt->local_slot != s->wire_slot)
        {
            rb_ctrl_note_drop(s, "RB_SEAL_ROWS");
        }
        break;
    case RNET_PKT_RB_BASELINE:
        if (s->rb_peer_slot >= 0 && (int)pkt->local_slot != s->rb_peer_slot)
        {
            break;
        }
        if (pkt->local_slot != s->wire_slot)
        {
            int i, dup = 0;
            for (i = 0; i < s->rb_base_count; ++i)
            {
                int k = RB_Q_AT(s->rb_base_tail, i);
                if (s->rb_base_q[k].from == pkt->local_slot &&
                    s->rb_base_q[k].epoch_id == pkt->rb_epoch_id &&
                    s->rb_base_q[k].load_tick == pkt->rb_load_tick)
                {
                    s->rb_base_q[k].digest_master = pkt->rb_digest_master;
                    s->rb_base_q[k].digest_a = pkt->rb_digest_a;
                    s->rb_base_q[k].digest_b = pkt->rb_digest_b;
                    s->rb_base_q[k].digest_c = pkt->rb_digest_c;
                    dup = 1;
                    break;
                }
            }
            if (dup)
                break;
            while (s->rb_base_count >= RNET_RB_CTRL_QUEUE &&
                   rb_epoch_before(s->rb_base_q[s->rb_base_tail].epoch_id, pkt->rb_epoch_id))
            {
                s->rb_base_tail = (s->rb_base_tail + 1) % RNET_RB_CTRL_QUEUE;
                s->rb_base_count--;
            }
        }
        if (pkt->local_slot != s->wire_slot &&
            s->rb_base_count < RNET_RB_CTRL_QUEUE)
        {
            s->rb_base_q[s->rb_base_head].epoch_id = pkt->rb_epoch_id;
            s->rb_base_q[s->rb_base_head].load_tick = pkt->rb_load_tick;
            s->rb_base_q[s->rb_base_head].digest_master = pkt->rb_digest_master;
            s->rb_base_q[s->rb_base_head].digest_a = pkt->rb_digest_a;
            s->rb_base_q[s->rb_base_head].digest_b = pkt->rb_digest_b;
            s->rb_base_q[s->rb_base_head].digest_c = pkt->rb_digest_c;
            s->rb_base_q[s->rb_base_head].from = pkt->local_slot;
            s->rb_base_head = (s->rb_base_head + 1) % RNET_RB_CTRL_QUEUE;
            s->rb_base_count++;
        }
        else if (pkt->local_slot != s->wire_slot)
        {
            rb_ctrl_note_drop(s, "RB_BASELINE");
        }
        break;
    case RNET_PKT_RB_POST:
        if (s->rb_peer_slot >= 0 && (int)pkt->local_slot != s->rb_peer_slot)
        {
            break;
        }
        if (pkt->local_slot != s->wire_slot)
        {
            int i, dup = 0;
            for (i = 0; i < s->rb_post_count; ++i)
            {
                int k = RB_Q_AT(s->rb_post_tail, i);
                if (s->rb_post_q[k].from == pkt->local_slot &&
                    s->rb_post_q[k].epoch_id == pkt->rb_epoch_id &&
                    s->rb_post_q[k].target_tick == pkt->rb_target_tick)
                {
                    s->rb_post_q[k].digest_master = pkt->rb_digest_master;
                    s->rb_post_q[k].input_digest = pkt->rb_input_digest;
                    s->rb_post_q[k].match = pkt->rb_match;
                    dup = 1;
                    break;
                }
            }
            if (dup)
                break;
            while (s->rb_post_count >= RNET_RB_CTRL_QUEUE &&
                   rb_epoch_before(s->rb_post_q[s->rb_post_tail].epoch_id, pkt->rb_epoch_id))
            {
                s->rb_post_tail = (s->rb_post_tail + 1) % RNET_RB_CTRL_QUEUE;
                s->rb_post_count--;
            }
        }
        if (pkt->local_slot != s->wire_slot &&
            s->rb_post_count < RNET_RB_CTRL_QUEUE)
        {
            s->rb_post_q[s->rb_post_head].epoch_id = pkt->rb_epoch_id;
            s->rb_post_q[s->rb_post_head].target_tick = pkt->rb_target_tick;
            s->rb_post_q[s->rb_post_head].digest_master = pkt->rb_digest_master;
            s->rb_post_q[s->rb_post_head].input_digest = pkt->rb_input_digest;
            s->rb_post_q[s->rb_post_head].match = pkt->rb_match;
            s->rb_post_q[s->rb_post_head].from = pkt->local_slot;
            s->rb_post_head = (s->rb_post_head + 1) % RNET_RB_CTRL_QUEUE;
            s->rb_post_count++;
        }
        else if (pkt->local_slot != s->wire_slot)
        {
            rb_ctrl_note_drop(s, "RB_POST");
        }
        break;
    case RNET_PKT_MODSET:
        if (pkt->local_slot != s->wire_slot)
        {
            memcpy(s->modset_text, pkt->modset_text, sizeof(s->modset_text));
            s->modset_text[sizeof(s->modset_text) - 1] = '\0';
            s->modset_pending = 1u;
        }
        break;
    case RNET_PKT_MODSET_ACK:
        if (pkt->local_slot != s->wire_slot)
        {
            if (pkt->local_slot < RNET_MAX_SLOTS)
            {
                rnet_u8 from = pkt->local_slot;
                memcpy(s->modset_ack_reason[from], pkt->modset_reason,
                       sizeof(s->modset_ack_reason[from]));
                s->modset_ack_reason[from][sizeof(s->modset_ack_reason[from]) - 1] = '\0';
                s->modset_ack_status[from] = pkt->modset_status;
                s->modset_ack_pending |= 1u << from;
            }
        }
        break;
    case RNET_PKT_RB_RESOLVED:
        if (pkt->local_slot != s->wire_slot)
        {
            int i, merged = 0;
            for (i = 0; i < s->rb_resolved_count; ++i)
            {
                int k = RB_Q_AT(s->rb_resolved_tail, i);
                if (s->rb_resolved_q[k].from == pkt->local_slot)
                {
                    /* A watermark: a reordered older advert must not lower it. */
                    if (rb_epoch_before(s->rb_resolved_q[k].through, pkt->rb_resolved_through))
                        s->rb_resolved_q[k].through = pkt->rb_resolved_through;
                    merged = 1;
                    break;
                }
            }
            if (!merged && s->rb_resolved_count < RNET_RB_CTRL_QUEUE)
            {
                s->rb_resolved_q[s->rb_resolved_head].through = pkt->rb_resolved_through;
                s->rb_resolved_q[s->rb_resolved_head].from = pkt->local_slot;
                s->rb_resolved_head = (s->rb_resolved_head + 1) % RNET_RB_CTRL_QUEUE;
                s->rb_resolved_count++;
            }
            else if (!merged)
            {
                rb_ctrl_note_drop(s, "RB_RESOLVED");
            }
        }
        break;
    default:
        break;
    }
}

static void send_raw(RNetSession *s, const rnet_u8 *buf, int len)
{
    rnet_u8 tagged[RNET_MAX_PACKET];
    if (len >= 14 && rnet_proto_is_rb_control((rnet_u16)(buf[4] | (buf[5] << 8)))) {
        if ((size_t)len + 2 > sizeof(tagged)) return;
        memcpy(tagged, buf, (size_t)len);
        len = rnet_proto_tag_rb_epoch(tagged, (size_t)len, sizeof(tagged), s->input_epoch);
        if (len <= 0) return;
        buf = tagged;
    }

    if ((s == NULL) || (buf == NULL) || (len <= 0))
    {
        return;
    }
    (void)rnet_transport_send(&s->transport, buf, (size_t)len);
}

/* Sender seat of a decoded packet, or -1 when the packet carries none.
 * START has no slot field but only the sim authority (seat 0) sends it;
 * DELAY_SYNC has none and may come from any seat, so it is not attributed. */
static int packet_sender_slot(const RNetDecodedPacket *pkt)
{
    switch (pkt->type)
    {
    case RNET_PKT_START:
        return 0;
    case RNET_PKT_DELAY_SYNC:
        return -1;
    default:
        return (int)pkt->local_slot;
    }
}

static void pump_recv(RNetSession *s)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    int n;
    RNetDecodedPacket pkt;
    int guard = state_any_active(s) ? 512 : 64;

    while (guard-- > 0)
    {
        n = rnet_transport_recv(&s->transport, buf, sizeof(buf));
        if (n <= 0)
        {
            break;
        }
        if (rnet_proto_decode(buf, (size_t)n, s->cfg.protocol_magic, &pkt) == 0 &&
            pkt.session_id == s->cfg.session_id)
        {
            int from = packet_sender_slot(&pkt);
            rnet_transport_accept_pending_peer(&s->transport);
            s->last_peer_rx_ms = session_now(s);
            if (from >= 0 && from < RNET_MAX_SLOTS && from != (int)s->wire_slot)
            {
                s->peer_rx_ms[from] = s->last_peer_rx_ms ? s->last_peer_rx_ms : 1u;
            }
            s->packets_rx++;
            handle_decoded(s, &pkt);
        }
    }
}

static int state_rx_any_active(const RNetSession *s)
{
    int i;
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
    {
        if (s->rx[i].active)
        {
            return 1;
        }
    }
    return 0;
}

/* 1 while any transfer (outbound or any inbound) is open, ready or not --
 * what the single-transfer code called state_active. */
static int state_any_active(const RNetSession *s)
{
    return s != NULL && (s->tx.active || state_rx_any_active(s));
}

/* 1 while some open transfer has not completed yet. */
static int state_any_in_progress(const RNetSession *s)
{
    int i;
    if (s->tx.active && !s->tx.ready)
    {
        return 1;
    }
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
    {
        if (s->rx[i].active && !s->rx[i].ready)
        {
            return 1;
        }
    }
    return 0;
}

static int state_popcount(rnet_u32 v)
{
    int n = 0;
    while (v != 0u)
    {
        v &= v - 1u;
        n++;
    }
    return n;
}

/* Seats a transfer or probe of `op` from this session must reach.
 *   MEMCARD (guest -> host): the host alone.
 *   anything else (host -> guests): every occupied seat but our own, or
 *   only rb_peer_slot when PSX-Link group scoping is on -- STATE packets
 *   from any other seat are filtered there, so waiting on one would hang.
 * Observers are never counted: they are not seats. */
static rnet_u32 state_default_receivers(const RNetSession *s, rnet_u8 op)
{
    rnet_u32 mask = 0;
    rnet_u8 slot;
    if (op == RNET_STATE_OP_MEMCARD)
    {
        return 1u;
    }
    if (s->rb_peer_slot >= 0)
    {
        if (s->rb_peer_slot < RNET_MAX_SLOTS && s->rb_peer_slot != (int)s->cfg.local_slot &&
            rnet_config_slot_occupied(&s->cfg, (rnet_u8)s->rb_peer_slot))
        {
            return 1u << s->rb_peer_slot;
        }
        return 0;
    }
    for (slot = 0; slot < s->cfg.slot_count && slot < RNET_MAX_SLOTS; ++slot)
    {
        if (slot != s->cfg.local_slot && rnet_config_slot_occupied(&s->cfg, slot))
        {
            mask |= 1u << slot;
        }
    }
    return mask;
}

static int state_probe_all_replied(const RNetSession *s)
{
    return (s->state_probe_reply_mask & s->state_probe_expect_mask) == s->state_probe_expect_mask;
}

static void state_probe_clear(RNetSession *s)
{
    if (s == NULL)
    {
        return;
    }
    s->state_probe_active = 0;
    s->state_probe_sender = 0;
    s->state_probe_expect_mask = 0;
    s->state_probe_reply_mask = 0;
    s->state_probe_match_mask = 0;
    s->state_probe_pending = 0;
    s->state_probe_match = 0;
    s->state_probe_op = 0;
    s->state_probe_slot = 0;
    s->state_probe_size = 0;
    s->state_probe_crc = 0;
    s->state_probe_last_tx_ms = 0;
    s->state_probe_parked = 0;
    if (!state_any_active(s))
    {
        s->state_stall_sim = 0;
    }
}

/* ICE AIMD for multi‑MB MotK .pst over TURN. Cold-start used to be 8 KiB / 4
 * chunks (far too shy); AIMD still backs off when juice drops. LAN keeps a
 * wide fixed budget. Sticky cwnd warm-starts the next in-session transfer. */
#define RNET_STATE_ICE_CWND_MIN        (8u * 1024u)
#define RNET_STATE_ICE_CWND_START      (32u * 1024u)
#define RNET_STATE_ICE_CWND_MAX        (256u * 1024u)
#define RNET_STATE_ICE_CHUNKS_MIN      4u
#define RNET_STATE_ICE_CHUNKS_START    16u
#define RNET_STATE_ICE_CHUNKS_MAX      64u
#define RNET_STATE_ICE_AI_CWND         (8u * 1024u)
#define RNET_STATE_ICE_AI_CHUNKS       2u
#define RNET_STATE_ICE_ACK_TO_MIN_MS   40u
#define RNET_STATE_ICE_ACK_TO_START_MS 80u
#define RNET_STATE_ICE_ACK_TO_MAX_MS   180u
#define RNET_STATE_LAN_CWND            (256u * 1024u)
#define RNET_STATE_LAN_CHUNKS          128u
#define RNET_STATE_LAN_ACK_TO_MS       40u
/* Re-ACK pacing for a transfer this receiver already finished (the sender
 * missed the final ACK and is still retransmitting chunks). */
#define RNET_STATE_REACK_MS            20u

static int state_transport_is_ice(const RNetSession *s)
{
    return s != NULL && (s->transport.mode == RNET_TRANSPORT_ICE || s->transport.mode == RNET_TRANSPORT_ICE_HUB);
}

static void state_pacing_reset(RNetSession *s)
{
    if (s == NULL)
        return;
    if (state_transport_is_ice(s))
    {
        rnet_u32 cwnd = RNET_STATE_ICE_CWND_START;
        rnet_u32 chunks = RNET_STATE_ICE_CHUNKS_START;
        if (s->state_sticky_cwnd >= RNET_STATE_ICE_CWND_START)
        {
            cwnd = s->state_sticky_cwnd;
            if (cwnd > RNET_STATE_ICE_CWND_MAX)
                cwnd = RNET_STATE_ICE_CWND_MAX;
        }
        if (s->state_sticky_chunks >= RNET_STATE_ICE_CHUNKS_START)
        {
            chunks = s->state_sticky_chunks;
            if (chunks > RNET_STATE_ICE_CHUNKS_MAX)
                chunks = RNET_STATE_ICE_CHUNKS_MAX;
        }
        s->tx.cwnd = cwnd;
        s->tx.chunks_cap = chunks;
        s->tx.ack_timeout_ms = RNET_STATE_ICE_ACK_TO_START_MS;
    }
    else
    {
        s->tx.cwnd = RNET_STATE_LAN_CWND;
        s->tx.chunks_cap = RNET_STATE_LAN_CHUNKS;
        s->tx.ack_timeout_ms = RNET_STATE_LAN_ACK_TO_MS;
    }
}

static void state_pacing_on_ack_progress(RNetSession *s)
{
    if (s == NULL || !state_transport_is_ice(s))
        return;
    /* Additive increase: +8 KiB and +2 chunks/pump per advancing ACK. */
    if (s->tx.cwnd < RNET_STATE_ICE_CWND_MAX)
    {
        s->tx.cwnd += RNET_STATE_ICE_AI_CWND;
        if (s->tx.cwnd > RNET_STATE_ICE_CWND_MAX)
            s->tx.cwnd = RNET_STATE_ICE_CWND_MAX;
    }
    if (s->tx.chunks_cap < RNET_STATE_ICE_CHUNKS_MAX)
    {
        s->tx.chunks_cap += RNET_STATE_ICE_AI_CHUNKS;
        if (s->tx.chunks_cap > RNET_STATE_ICE_CHUNKS_MAX)
            s->tx.chunks_cap = RNET_STATE_ICE_CHUNKS_MAX;
    }
    if (s->tx.ack_timeout_ms > RNET_STATE_ICE_ACK_TO_MIN_MS)
    {
        s->tx.ack_timeout_ms -= 5u;
        if (s->tx.ack_timeout_ms < RNET_STATE_ICE_ACK_TO_MIN_MS)
            s->tx.ack_timeout_ms = RNET_STATE_ICE_ACK_TO_MIN_MS;
    }
}

static void state_pacing_on_timeout(RNetSession *s)
{
    if (s == NULL || !state_transport_is_ice(s))
        return;
    /* Multiplicative decrease — juice/TURN drop when we outrun the relay. */
    s->tx.cwnd /= 2u;
    if (s->tx.cwnd < RNET_STATE_ICE_CWND_MIN)
        s->tx.cwnd = RNET_STATE_ICE_CWND_MIN;
    s->tx.chunks_cap /= 2u;
    if (s->tx.chunks_cap < RNET_STATE_ICE_CHUNKS_MIN)
        s->tx.chunks_cap = RNET_STATE_ICE_CHUNKS_MIN;
    s->tx.ack_timeout_ms += 20u;
    if (s->tx.ack_timeout_ms > RNET_STATE_ICE_ACK_TO_MAX_MS)
        s->tx.ack_timeout_ms = RNET_STATE_ICE_ACK_TO_MAX_MS;
    /* Decay sticky so the next transfer does not restart at a failed peak. */
    if (s->state_sticky_cwnd > s->tx.cwnd)
        s->state_sticky_cwnd = s->tx.cwnd;
    if (s->state_sticky_chunks > s->tx.chunks_cap)
        s->state_sticky_chunks = s->tx.chunks_cap;
}

static void state_pacing_remember_success(RNetSession *s)
{
    if (s == NULL || !state_transport_is_ice(s))
        return;
    if (s->tx.cwnd > s->state_sticky_cwnd)
        s->state_sticky_cwnd = s->tx.cwnd;
    if (s->tx.chunks_cap > s->state_sticky_chunks)
        s->state_sticky_chunks = s->tx.chunks_cap;
}

/* After one part of the transfer state was cleared: admit stays stalled while
 * any other transfer is open, else it follows the probe (as the
 * single-transfer state_clear did). */
static void state_recompute_stall(RNetSession *s)
{
    s->state_stall_sim = state_any_active(s) ? 1 : (s->state_probe_active ? 1 : 0);
}

static void state_tx_clear(RNetSession *s)
{
    free(s->tx.buf);
    memset(&s->tx, 0, sizeof(s->tx));
    if (s->state_taken == RNET_STATE_TAKEN_TX)
    {
        s->state_taken = -1;
    }
}

static void state_rx_clear(RNetSession *s, int src)
{
    RNetStateRx *rx = &s->rx[src];
    free(rx->buf);
    rx->buf = NULL;
    rx->active = 0;
    rx->ready = 0;
    rx->op = 0;
    rx->slot = 0;
    rx->xfer_id = 0;
    rx->total = 0;
    rx->crc = 0;
    rx->contiguity = 0;
    rx->last_ack_ms = 0;
    rx->start_ms = 0;
    memset(rx->bits, 0, sizeof(rx->bits));
    if (s->state_taken == src)
    {
        s->state_taken = -1;
    }
}

/* Clear every transfer, outbound and inbound. */
static void state_clear(RNetSession *s)
{
    int i;
    if (s == NULL)
    {
        return;
    }
    state_tx_clear(s);
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
    {
        state_rx_clear(s, i);
    }
    s->state_taken = -1;
    state_recompute_stall(s);
}

static void state_rx_set_chunk(RNetStateRx *rx, rnet_u32 chunk_index)
{
    if (chunk_index >= RNET_STATE_MAX_CHUNKS)
    {
        return;
    }
    rx->bits[chunk_index >> 3] |= (rnet_u8)(1u << (chunk_index & 7u));
}

static int state_rx_has_chunk(const RNetStateRx *rx, rnet_u32 chunk_index)
{
    if (chunk_index >= RNET_STATE_MAX_CHUNKS)
    {
        return 0;
    }
    return (rx->bits[chunk_index >> 3] >> (chunk_index & 7u)) & 1;
}

static void state_rx_advance_contiguity(RNetStateRx *rx)
{
    rnet_u32 chunks = (rx->total + RNET_STATE_CHUNK_MAX - 1u) / RNET_STATE_CHUNK_MAX;
    rnet_u32 i = rx->contiguity / RNET_STATE_CHUNK_MAX;
    while (i < chunks && state_rx_has_chunk(rx, i))
    {
        rnet_u32 end = (i + 1u) * RNET_STATE_CHUNK_MAX;
        if (end > rx->total)
        {
            end = rx->total;
        }
        rx->contiguity = end;
        i++;
    }
}

static void state_send_ack_raw(RNetSession *s, rnet_u32 xfer_id, rnet_u32 ack_bytes)
{
    rnet_u8 buf[64];
    int n = rnet_proto_encode_state_ack(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id, s->wire_slot,
                                        xfer_id, ack_bytes);
    if (n > 0)
    {
        send_raw(s, buf, n);
    }
}

static void state_rx_send_ack(RNetSession *s, RNetStateRx *rx)
{
    state_send_ack_raw(s, rx->xfer_id, rx->contiguity);
    rx->last_ack_ms = session_now(s);
}

static void state_rx_mark_ready_if_complete(RNetSession *s, RNetStateRx *rx)
{
    rnet_u32 crc;
    if (!rx->active || rx->ready || rx->buf == NULL)
    {
        return;
    }
    if (rx->contiguity < rx->total)
    {
        return;
    }
    crc = rnet_proto_checksum(rx->buf, rx->total);
    if (crc != rx->crc)
    {
        /* Restart receive — ask host to resend from 0 by ACKing 0 after clear. */
        rx->contiguity = 0;
        state_rx_send_ack(s, rx);
        return;
    }
    rx->ready = 1;
    state_rx_send_ack(s, rx);
}

/* Lowest contiguous ACK over the receivers still expected (total when none
 * are left -- every one of them was dropped by the host). */
static rnet_u32 state_tx_min_ack(const RNetSession *s)
{
    rnet_u32 min = s->tx.total;
    int r;
    for (r = 0; r < RNET_MAX_SLOTS; ++r)
    {
        if ((s->tx.expect_mask & (1u << r)) && s->tx.ack[r] < min)
        {
            min = s->tx.ack[r];
        }
    }
    return min;
}

/* Receivers in expect_mask that have ACKed the whole blob. */
static rnet_u32 state_tx_done_mask(const RNetSession *s)
{
    rnet_u32 done = 0;
    int r;
    for (r = 0; r < RNET_MAX_SLOTS; ++r)
    {
        if ((s->tx.expect_mask & (1u << r)) && s->tx.ack[r] >= s->tx.total)
        {
            done |= 1u << r;
        }
    }
    return done;
}

static void state_tx_mark_ready_if_complete(RNetSession *s)
{
    if (!s->tx.active || s->tx.ready || s->tx.buf == NULL)
    {
        return;
    }
    if (state_tx_min_ack(s) >= s->tx.total)
    {
        s->tx.ready = 1;
        state_pacing_remember_success(s);
    }
}

static void state_drive_sender(RNetSession *s)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    int n;
    int r;
    rnet_u64 now;
    rnet_u32 window_end;
    rnet_u32 min_ack;
    rnet_u32 sent_this_pump = 0;
    const int is_ice = state_transport_is_ice(s);
    const rnet_u64 kBeginRetransmitMs = is_ice ? 80ULL : 40ULL;
    rnet_u32 cwnd;
    rnet_u32 chunks_cap;
    rnet_u64 ack_timeout_ms;
    int backoff = 0;
    int need_begin = 0;

    if (!s->tx.active || s->tx.ready || s->tx.buf == NULL)
    {
        return;
    }
    if (s->tx.cwnd == 0 || s->tx.chunks_cap == 0)
        state_pacing_reset(s);
    cwnd = s->tx.cwnd;
    chunks_cap = s->tx.chunks_cap;
    ack_timeout_ms = (rnet_u64)s->tx.ack_timeout_ms;
    if (ack_timeout_ms == 0)
        ack_timeout_ms = is_ice ? (rnet_u64)RNET_STATE_ICE_ACK_TO_START_MS
                                : (rnet_u64)RNET_STATE_LAN_ACK_TO_MS;

    now = session_now(s);
    /* Retransmit BEGIN until EVERY expected receiver has ACKed past 0 (saw
     * BEGIN). Stopping at the first one left a receiver that lost BEGIN with
     * nothing to open its receive, so it dropped every chunk. */
    for (r = 0; r < RNET_MAX_SLOTS; ++r)
    {
        if ((s->tx.expect_mask & (1u << r)) && s->tx.ack[r] == 0)
        {
            need_begin = 1;
            break;
        }
    }
    if (need_begin &&
        (s->tx.last_begin_ms == 0 || now - s->tx.last_begin_ms >= kBeginRetransmitMs))
    {
        n = rnet_proto_encode_state_begin(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id, s->wire_slot,
                                          s->tx.op, s->tx.slot, s->tx.xfer_id, s->tx.total, s->tx.crc);
        if (n > 0)
        {
            send_raw(s, buf, n);
        }
        s->tx.last_begin_ms = now;
    }

    min_ack = state_tx_min_ack(s);
    if (min_ack >= s->tx.total)
    {
        state_tx_mark_ready_if_complete(s);
        return;
    }

    /* Per-receiver ACK timeout: a receiver whose ACK has not advanced for
     * ack_timeout_ms rewinds the send cursor to ITS watermark. The timer runs
     * from that receiver's last progress ACK or, before any, from the
     * transfer's start -- a first burst whose chunks overtook BEGIN
     * (reordering) or lost chunk 0 otherwise left the ACK at 0 with nothing
     * ever resent (reproduced with the link simulator at 35 ms +/- 15 ms).
     * Per receiver, not one timer: with several receivers a fast one's
     * progress kept a single timer fresh, and a slow one that lost chunks was
     * never served. Back off (AIMD) when the slowest receiver times out or a
     * rewind actually happened -- a fast receiver that is merely waiting for
     * the window to reach its gap is not loss. */
    for (r = 0; r < RNET_MAX_SLOTS; ++r)
    {
        rnet_u64 base;
        if (!(s->tx.expect_mask & (1u << r)) || s->tx.ack[r] >= s->tx.total)
        {
            continue;
        }
        base = (s->tx.ack_timer_ms[r] != 0) ? s->tx.ack_timer_ms[r] : s->tx.start_ms;
        if (now - base < ack_timeout_ms)
        {
            continue;
        }
        if (s->tx.send_cursor > s->tx.ack[r] || s->tx.ack[r] == min_ack)
        {
            backoff = 1;
        }
        if (s->tx.send_cursor > s->tx.ack[r])
        {
            s->tx.send_cursor = s->tx.ack[r];
        }
        s->tx.ack_timer_ms[r] = now; /* avoid spinning every pump */
    }
    if (backoff)
    {
        state_pacing_on_timeout(s);
        cwnd = s->tx.cwnd;
        chunks_cap = s->tx.chunks_cap;
        ack_timeout_ms = (rnet_u64)s->tx.ack_timeout_ms;
    }

    if (s->tx.send_cursor < min_ack)
    {
        s->tx.send_cursor = min_ack;
    }
    window_end = min_ack + cwnd;
    if (window_end > s->tx.total)
    {
        window_end = s->tx.total;
    }

    while (s->tx.send_cursor < window_end && sent_this_pump < chunks_cap)
    {
        rnet_u32 off = s->tx.send_cursor;
        rnet_u32 left = s->tx.total - off;
        rnet_u16 chunk = (left > RNET_STATE_CHUNK_MAX) ? (rnet_u16)RNET_STATE_CHUNK_MAX : (rnet_u16)left;
        n = rnet_proto_encode_state_chunk(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id, s->wire_slot,
                                          s->tx.xfer_id, off, s->tx.buf + off, chunk);
        if (n <= 0)
        {
            break;
        }
        send_raw(s, buf, n);
        s->tx.send_cursor += chunk;
        sent_this_pump++;
    }
    s->tx.last_tx_ms = now;

    /* Progress log every ~500ms (or on first ACK) — useful on slow TURN paths.
     * "acked" is the slowest receiver's watermark. */
    if (min_ack != s->tx.last_progress_acked || s->tx.last_progress_log_ms == 0 ||
        now - s->tx.last_progress_log_ms >= 500ULL)
    {
        unsigned kib_s = 0;
        char rcv[48];
        int expect_n = state_popcount(s->tx.expect_mask);
        rcv[0] = '\0';
        if (s->tx.start_ms != 0 && now > s->tx.start_ms)
        {
            rnet_u64 elapsed = now - s->tx.start_ms;
            if (elapsed > 0)
                kib_s = (unsigned)((min_ack * 1000ULL) / elapsed / 1024ULL);
        }
        if (expect_n > 1)
        {
            snprintf(rcv, sizeof(rcv), " rcv_done=%d/%d mask=0x%02x", state_popcount(state_tx_done_mask(s)),
                     expect_n, (unsigned)s->tx.expect_mask);
        }
        fprintf(stderr,
                "rnet_state: xfer_id=%u op=%u %u/%u acked (%u KiB/s) cwnd=%u chunks=%u to=%ums%s%s\n",
                (unsigned)s->tx.xfer_id, (unsigned)s->tx.op, (unsigned)min_ack,
                (unsigned)s->tx.total, kib_s, (unsigned)s->tx.cwnd, (unsigned)s->tx.chunks_cap,
                (unsigned)s->tx.ack_timeout_ms, is_ice ? " (ice)" : "", rcv);
        s->tx.last_progress_log_ms = now;
        s->tx.last_progress_acked = min_ack;
    }

    state_tx_mark_ready_if_complete(s);
}

static void state_on_begin(RNetSession *s, const RNetDecodedPacket *pkt)
{
    rnet_u8 src = pkt->local_slot;
    RNetStateRx *rx;
    if (src == s->wire_slot)
    {
        return; /* ignore echo */
    }
    if (pkt->state_op == RNET_STATE_OP_MEMCARD)
    {
        /* Peer→host upload: only the host receives it, and only from a
         * guest. Other guests see the same broadcast and must not open a
         * receive for it, or they would sit on a transfer nobody drives for
         * them (the sender only tracks the host's ACKs). */
        if (s->cfg.local_slot != 0 || src == 0)
        {
            return;
        }
    }
    else if (s->cfg.local_slot == 0)
    {
        return; /* host never receives BEGIN */
    }
    if (pkt->state_total_size == 0 || pkt->state_total_size > RNET_STATE_MAX)
    {
        return;
    }
    if (src >= RNET_MAX_SLOTS || !pkt->state_xfer_id)
        return;
    rx = &s->rx[src];
    if (s->state_received[src].id)
    {
        /* Low 30 bits are the serial number; MEMCARD sets bit 30 as its
         * historical direction namespace. Compare serials modulo that space.
         * A MEMCARD id carries the sender's seat in bits 24..29 and its serial
         * in the low 24, so its serials compare in a 24-bit window (a 30-bit
         * compare would call every BEGIN after the serial wraps stale). */
        const int seat_ids = (pkt->state_xfer_id & 0x40000000u) && (s->state_received[src].id & 0x40000000u);
        const rnet_u32 space = seat_ids ? 0x00ffffffu : 0x3fffffffu;
        const rnet_u32 half = (space >> 1) + 1u;
        rnet_u32 distance = (pkt->state_xfer_id - s->state_received[src].id) & space;
        if (distance == 0 && pkt->state_xfer_id != s->state_received[src].id)
            return; /* same serial, different seat bits: not this sender's */
        if (distance == 0)
        {
            if (pkt->state_xfer_id != s->state_received[src].id ||
                pkt->state_total_size != s->state_received[src].total ||
                pkt->state_payload_crc != s->state_received[src].crc ||
                pkt->state_op != s->state_received[src].op ||
                pkt->state_slot != s->state_received[src].slot)
                return;
            if (s->state_received[src].finished)
                state_send_ack_raw(s, pkt->state_xfer_id, pkt->state_total_size);
            else if (rx->active && rx->xfer_id == pkt->state_xfer_id && rx->buf != NULL)
                state_rx_send_ack(s, rx);
            return;
        }
        if (distance >= half) return; /* stale BEGIN, even during a newer transfer */
    }
    /* Hash-miss path: a transfer from the prober replaces the probe it
     * answered. As a prober we drop our probe only when `src` is the one
     * seat it waits on (the two-seat rule, unchanged); a host whose barrier
     * waits on several seats keeps it while guests upload. */
    if (s->state_probe_active &&
        (!s->state_probe_sender || s->state_probe_expect_mask == (1u << src)))
    {
        state_probe_clear(s);
    }
    /* Supersede: a new transfer from the ONE receiver of our outbound
     * transfer replaces it, exactly as the single-transfer session did -- the
     * peer only starts its own after consuming ours (a guest's MEMCARD
     * receipt after the host's proposal, the host's BOOT after a guest's
     * upload), and hosts rely on the outbound transfer being gone rather
     * than completing (gbarecomp multiplayer_startup.cpp / _checkpoint.cpp).
     * An outbound transfer that still has other receivers keeps running for
     * them; `src` completes it through its ACK / finished re-ACK. */
    if (s->tx.active && s->tx.expect_mask == (1u << src))
    {
        state_tx_clear(s);
    }
    /* A new transfer from `src` replaces only src's receive: another source's
     * receive is untouched. */
    state_rx_clear(s, src);
    rx->buf = (rnet_u8 *)malloc(pkt->state_total_size);
    if (rx->buf == NULL)
    {
        state_recompute_stall(s);
        return;
    }
    memset(rx->buf, 0, pkt->state_total_size);
    rx->active = 1;
    rx->ready = 0;
    s->state_stall_sim = 1;
    rx->op = pkt->state_op;
    rx->slot = pkt->state_slot;
    rx->xfer_id = pkt->state_xfer_id;
    s->state_received[src].id = pkt->state_xfer_id;
    s->state_received[src].total = pkt->state_total_size;
    s->state_received[src].crc = pkt->state_payload_crc;
    s->state_received[src].op = pkt->state_op;
    s->state_received[src].slot = pkt->state_slot;
    s->state_received[src].finished = 0;
    s->state_received[src].last_reack_ms = 0;
    rx->total = pkt->state_total_size;
    rx->crc = pkt->state_payload_crc;
    rx->contiguity = 0;
    rx->start_ms = session_now(s);
    state_rx_send_ack(s, rx);
}

static void state_on_chunk(RNetSession *s, const RNetDecodedPacket *pkt)
{
    rnet_u32 end;
    rnet_u8 src = pkt->local_slot;
    RNetStateRx *rx;
    if (src >= RNET_MAX_SLOTS || src == s->wire_slot)
    {
        return;
    }
    rx = &s->rx[src];
    if (!rx->active || rx->buf == NULL || pkt->state_xfer_id != rx->xfer_id)
    {
        /* A chunk of a transfer we already finished: the sender missed our
         * final ACK and is retransmitting. Answer with the full watermark
         * (paced), or it would retransmit forever. */
        if (s->state_received[src].id != 0 && s->state_received[src].finished &&
            s->state_received[src].id == pkt->state_xfer_id)
        {
            rnet_u64 now = session_now(s);
            if (s->state_received[src].last_reack_ms == 0 ||
                now - s->state_received[src].last_reack_ms >= RNET_STATE_REACK_MS)
            {
                state_send_ack_raw(s, pkt->state_xfer_id, s->state_received[src].total);
                s->state_received[src].last_reack_ms = now ? now : 1u;
            }
        }
        return;
    }
    if (pkt->state_offset > rx->total || pkt->state_chunk_size == 0)
    {
        return;
    }
    end = pkt->state_offset + (rnet_u32)pkt->state_chunk_size;
    if (end > rx->total)
    {
        return;
    }
    memcpy(rx->buf + pkt->state_offset, pkt->state_chunk, pkt->state_chunk_size);
    {
        rnet_u32 chunk_index = pkt->state_offset / RNET_STATE_CHUNK_MAX;
        state_rx_set_chunk(rx, chunk_index);
        state_rx_advance_contiguity(rx);
    }
    {
        rnet_u64 now = session_now(s);
        /* ICE: ACK every chunk so the sender's cwnd can grow without waiting
         * on a 4ms coalesce that fights TURN RTT. LAN keeps light coalescing. */
        if (state_transport_is_ice(s) || now - rx->last_ack_ms >= 4ULL ||
            rx->contiguity >= rx->total)
        {
            state_rx_send_ack(s, rx);
        }
    }
    state_rx_mark_ready_if_complete(s, rx);
}

static void state_on_ack(RNetSession *s, const RNetDecodedPacket *pkt)
{
    rnet_u8 from = pkt->local_slot;
    rnet_u32 min_before;
    rnet_u32 ack;
    if (!s->tx.active)
    {
        return;
    }
    if (pkt->state_xfer_id != s->tx.xfer_id)
    {
        return;
    }
    /* Only an expected receiver's ACK counts. The single-peer sender took the
     * max over ANY ACK, so with several receivers the fastest one completed
     * the transfer for all of them (and an observer's ACK could too). */
    if (from >= RNET_MAX_SLOTS || !(s->tx.expect_mask & (1u << from)))
    {
        return;
    }
    ack = pkt->state_ack_bytes;
    if (ack > s->tx.total)
    {
        ack = s->tx.total;
    }
    if (ack > s->tx.ack[from])
    {
        rnet_u64 now = session_now(s);
        min_before = state_tx_min_ack(s);
        s->tx.ack[from] = ack;
        s->tx.ack_timer_ms[from] = now ? now : 1u;
        s->tx.last_tx_ms = 0; /* send next chunk immediately */
        /* Grow the window only when the SLOWEST receiver advances: the window
         * is anchored on it, and N receivers each ACKing must not grow cwnd N
         * times as fast. With one receiver every advance is the slowest's. */
        if (state_tx_min_ack(s) > min_before)
        {
            state_pacing_on_ack_progress(s);
        }
    }
    state_tx_mark_ready_if_complete(s);
}

/* The PROBE replied mask is one byte on the wire: one bit per seat. */
typedef char rnet_probe_replied_mask_fits_u8[(RNET_MAX_SLOTS <= 8) ? 1 : -1];

static void state_drive_probe(RNetSession *s)
{
    rnet_u8 buf[64];
    int n;
    rnet_u64 now;

    if (s->state_probe_active && !s->state_probe_sender && s->state_probe_parked)
    {
        /* A lost STATE_PROBE_DONE must not park this seat for good: repeat
         * the matching reply; the host answers it with DONE again. */
        now = session_now(s);
        if (s->state_probe_last_tx_ms == 0 || now - s->state_probe_last_tx_ms >= 50ULL)
        {
            n = rnet_proto_encode_state_probe_reply(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                                    s->wire_slot, s->state_probe_op, s->state_probe_slot, 1u,
                                                    s->state_probe_size, s->state_probe_crc);
            if (n > 0)
            {
                send_raw(s, buf, n);
                s->state_probe_last_tx_ms = now;
            }
        }
        return;
    }
    if (!s->state_probe_active || !s->state_probe_sender || state_probe_all_replied(s))
    {
        return;
    }
    now = session_now(s);
    /* Ready/hash probes: snappy retransmit — 40ms left a visible post-load hitch. */
    if (s->state_probe_last_tx_ms != 0 && now - s->state_probe_last_tx_ms < 8ULL)
    {
        return;
    }
    /* The replied mask tells seats that already answered to ignore this
     * retransmit (0 while nobody has -- the only value a two-seat host ever
     * sends, since it stops at the first reply). */
    n = rnet_proto_encode_state_probe_ex(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                         s->wire_slot, s->state_probe_op, s->state_probe_slot,
                                         s->state_probe_size, s->state_probe_crc,
                                         (rnet_u8)(s->state_probe_reply_mask & 0xffu));
    if (n > 0)
    {
        send_raw(s, buf, n);
        s->state_probe_last_tx_ms = now;
    }
}

static void state_on_probe(RNetSession *s, const RNetDecodedPacket *pkt)
{
    if (pkt->local_slot == s->wire_slot)
    {
        return;
    }
    if (s->cfg.local_slot == 0)
    {
        return; /* host never receives PROBE */
    }
    /* The prober already holds our reply to this probe: it is retransmitting
     * for a slower seat. Re-raising it would hand a LOAD/BOOT ready probe we
     * already answered (and cleared) back to the app as a fresh one. */
    if (s->wire_slot < 8u && (pkt->state_probe_replied & (1u << s->wire_slot)))
    {
        return;
    }
    if (state_any_active(s))
    {
        return; /* transfer in flight takes precedence */
    }
    /* Retransmit of a probe we already answered — resend REPLY, do not re-arm. */
    if (s->state_probe_active && !s->state_probe_sender && !s->state_probe_pending &&
        s->state_probe_op == pkt->state_op && s->state_probe_slot == pkt->state_slot &&
        s->state_probe_size == pkt->state_total_size && s->state_probe_crc == pkt->state_payload_crc)
    {
        rnet_u8 buf[64];
        int n = rnet_proto_encode_state_probe_reply(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                                    s->wire_slot, s->state_probe_op, s->state_probe_slot,
                                                    s->state_probe_match ? 1u : 0u, s->state_probe_size,
                                                    s->state_probe_crc);
        if (n > 0)
        {
            send_raw(s, buf, n);
        }
        return;
    }
    /* Fresh probe — surface to the app. size==0 (coord save) must not stall
     * admit or deferred savestate_poll never runs (deadlock). */
    s->state_probe_active = 1;
    s->state_probe_sender = 0;
    s->state_probe_pending = 1;
    s->state_probe_expect_mask = 0;
    s->state_probe_reply_mask = 0;
    s->state_probe_match_mask = 0;
    s->state_probe_match = 0;
    s->state_probe_op = pkt->state_op;
    s->state_probe_slot = pkt->state_slot;
    s->state_probe_size = pkt->state_total_size;
    s->state_probe_crc = pkt->state_payload_crc;
    s->state_stall_sim = (pkt->state_total_size != 0) ? 1 : 0;
}

static void state_on_probe_reply(RNetSession *s, const RNetDecodedPacket *pkt)
{
    rnet_u8 from = pkt->local_slot;
    if (!s->state_probe_active || !s->state_probe_sender)
    {
        if (s->state_probe_released_valid && pkt->state_probe_match && from != s->wire_slot &&
            pkt->state_op == s->state_probe_released_op && pkt->state_slot == s->state_probe_released_slot &&
            pkt->state_total_size == s->state_probe_released_size &&
            pkt->state_payload_crc == s->state_probe_released_crc)
        {
            rnet_u8 buf[64];
            int n = rnet_proto_encode_state_probe_done(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                                       s->wire_slot, s->state_probe_released_op,
                                                       s->state_probe_released_slot, s->state_probe_released_size,
                                                       s->state_probe_released_crc);
            if (n > 0)
            {
                send_raw(s, buf, n);
            }
        }
        return;
    }
    if (from == s->wire_slot)
    {
        return;
    }
    /* Bind to the active probe generation. SAVE coord (size=0,crc=target)
     * and LOAD ready (size=0,crc=ready) share op/slot with the following
     * hash probe — a late ACK must not satisfy the next probe. */
    if (pkt->state_op != s->state_probe_op || pkt->state_slot != s->state_probe_slot ||
        pkt->state_total_size != s->state_probe_size || pkt->state_payload_crc != s->state_probe_crc)
    {
        return;
    }
    /* Per seat: every expected seat must answer; one seat's reply never
     * stands in for another's. Latest answer per seat wins. */
    if (from >= RNET_MAX_SLOTS || !(s->state_probe_expect_mask & (1u << from)))
    {
        return;
    }
    s->state_probe_reply_mask |= 1u << from;
    if (pkt->state_probe_match)
    {
        s->state_probe_match_mask |= 1u << from;
    }
    else
    {
        s->state_probe_match_mask &= ~(1u << from);
    }
}

/* Guest: the host heard "match" from every expected seat for this probe
 * generation; resume. A LOAD/BOOT match never parks, so DONE only releases
 * SAVE/SRAM/RB_KF. */
static void state_on_probe_done(RNetSession *s, const RNetDecodedPacket *pkt)
{
    if (s->cfg.local_slot == 0 || pkt->local_slot == s->wire_slot || !s->state_probe_active ||
        s->state_probe_sender || !s->state_probe_parked || pkt->state_op != s->state_probe_op ||
        pkt->state_slot != s->state_probe_slot || pkt->state_total_size != s->state_probe_size ||
        pkt->state_payload_crc != s->state_probe_crc)
    {
        return;
    }
    state_probe_clear(s);
}

/* Host: release every parked guest once all expected seats matched. */
static void state_send_probe_done(RNetSession *s)
{
    rnet_u8 buf[64];
    int n;
    if (!s->state_probe_active || !s->state_probe_sender || s->state_probe_size == 0 ||
        !state_probe_all_replied(s) ||
        (s->state_probe_match_mask & s->state_probe_expect_mask) != s->state_probe_expect_mask)
    {
        return;
    }
    n = rnet_proto_encode_state_probe_done(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                           s->wire_slot, s->state_probe_op, s->state_probe_slot,
                                           s->state_probe_size, s->state_probe_crc);
    if (n > 0)
    {
        send_raw(s, buf, n);
    }
    s->state_probe_released_valid = 1;
    s->state_probe_released_op = s->state_probe_op;
    s->state_probe_released_slot = s->state_probe_slot;
    s->state_probe_released_size = s->state_probe_size;
    s->state_probe_released_crc = s->state_probe_crc;
}

static void maybe_bootstrap(RNetSession *s)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    int len;
    rnet_u64 now = session_now(s);
    rnet_u8 all_ready = 1;
    rnet_u8 slot;

    if (s->phase == RNET_PHASE_LINKING)
    {
        if (now - s->last_hello_ms >= 100ULL)
        {
            len = rnet_proto_encode_hello(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id, s->wire_slot,
                                          s->cfg.slot_count, s->delay);
            send_raw(s, buf, len);
            s->last_hello_ms = now;
        }
    }

    if (s->phase == RNET_PHASE_READY || s->phase == RNET_PHASE_LINKING)
    {
        if (!s->local_ready)
        {
            s->local_ready = 1;
        }
        if (now - s->last_ready_ms >= 100ULL)
        {
            len = rnet_proto_encode_ready(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id, s->wire_slot);
            send_raw(s, buf, len);
            s->last_ready_ms = now;
        }
        /* Mark our own seat ready. An observer has no seat to mark -- and
         * local_slot is the sentinel, which would index one past the array
         * when the room is full. It still waits for every seat below. */
        if (!s->is_observer)
        {
            s->peer_ready[s->cfg.local_slot] = 1;
        }
        for (slot = 0; slot < s->cfg.slot_count; ++slot)
        {
            if (!rnet_config_slot_occupied(&s->cfg, slot))
            {
                s->peer_ready[slot] = 1; /* empty seat — no READY expected */
                continue;
            }
            if (!s->peer_ready[slot])
            {
                all_ready = 0;
                break;
            }
        }
        if (all_ready && s->is_sim_authority && !s->start_sent)
        {
            len = rnet_proto_encode_start(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id, 0);
            send_raw(s, buf, len);
            s->start_sent = 1;
            s->sim_tick = 0;
            s->phase = RNET_PHASE_RUNNING;
            seed_delay_prefix(s);
            send_input_bundle(s);
        }
    }
}

static void send_input_bundle(RNetSession *s)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    RNetWireFrame frames[RNET_MAX_BUNDLE];
    int red = (int)s->cfg.bundle_redundancy;
    rnet_u32 tip;
    rnet_u32 lo;
    rnet_u32 t;
    rnet_u32 ack;
    rnet_u32 ack_vector[RNET_MAX_SLOTS];
    rnet_u8 slot;
    int sent_any = 0;
    rnet_u64 now = session_now(s);

    /* The one choke point for "a spectator sends no input".
     *
     * Guarded here rather than at each of the seven call sites -- several are
     * stall paths that retransmit, and a spectator's local_ring is empty
     * anyway, so those would send empty bundles forever. One guard cannot be
     * forgotten when an eighth call site is added. */
    if (s->is_observer)
    {
        return;
    }

    if (s->phase != RNET_PHASE_RUNNING)
    {
        return;
    }
    /* Stall or explicit suppress: do not emit pre-resync tips during load. */
    if (s->state_stall_sim || s->input_send_suppress)
    {
        return;
    }
    tip = rnet_wire_tick_from_sim(s->sim_tick, s->delay);
    /* A new future tip is latency-sensitive and sends immediately. Repeated
     * pumps for the same tip are reliability retransmits, not new data. */
    if (s->last_input_tip_valid && s->last_input_tip == tip &&
        now - s->last_input_ms < 8ULL)
    {
        return;
    }
    if (red < 1)
    {
        red = 1;
    }
    /* Startup must carry the complete neutral delay prefix. Otherwise delays
     * larger than one INPUT packet can never admit tick zero. Do not clamp
     * this window to RNET_MAX_BUNDLE — emit multiple packets instead. */
    if (red < (int)s->delay + 1)
    {
        red = (int)s->delay + 1;
    }
    lo = (tip + 1U > (rnet_u32)red) ? (tip + 1U - (rnet_u32)red) : 0U;
    /* Peer ACK of our tips: resend from ack+1 when they fell behind the
     * tip-redundancy window (WAN/TURN loss). Cap so one pump cannot emit
     * the whole 128-slot history. */
    if (s->cfg.slot_count > 2)
    {
        rnet_u32 ack_lo = tip + 1u;
        rnet_u32 max_back = (rnet_u32)RNET_HISTORY_LENGTH / 2u;
        if (max_back < 32u) max_back = 32u;
        if (max_back > 64u) max_back = 64u;
        for (slot = 0; slot < s->cfg.slot_count; ++slot)
        {
            rnet_u32 peer_lo;
            if (slot == s->cfg.local_slot || !rnet_config_slot_occupied(&s->cfg, slot))
                continue;
            peer_lo = (!s->peer_ack_seen[slot] || s->peer_ack_tick[slot] == 0xffffffffu)
                          ? 0u : s->peer_ack_tick[slot] + 1u;
            if (peer_lo < ack_lo) ack_lo = peer_lo;
        }
        if (tip + 1u > max_back && ack_lo + max_back <= tip)
            ack_lo = tip + 1u - max_back;
        if (ack_lo < lo) lo = ack_lo;
    }
    else if (s->highest_remote_ack < tip)
    {
        rnet_u32 ack_lo = s->highest_remote_ack + 1U;
        rnet_u32 max_back = (rnet_u32)RNET_HISTORY_LENGTH / 2U;
        if (max_back < 32U)
            max_back = 32U;
        if (max_back > 64U)
            max_back = 64U;
        if (tip + 1U > max_back && ack_lo + max_back <= tip)
            ack_lo = tip + 1U - max_back;
        if (ack_lo < lo)
            lo = ack_lo;
    }
    /* Which peer's progress to acknowledge. Seated peers point at the next
     * seat round; an observer has no "next" seat from a seat of its own, and
     * the sentinel would pick a ring by accident of arithmetic. Seat 0 is the
     * sim authority and the one peer guaranteed present, so an observer acks
     * against that. */
    ack = rnet_ring_highest_valid(
        &s->remote_rings[s->is_observer
                             ? 0
                             : ((s->cfg.local_slot + 1) % s->cfg.slot_count)]);
    for (slot = 0; slot < s->cfg.slot_count; ++slot)
        ack_vector[slot] = s->remote_contiguous_ack[slot];
    t = lo;
    while (t <= tip)
    {
        int count = 0;
        int len;
        while (t <= tip && count < RNET_MAX_BUNDLE)
        {
            RNetInputSample sample;
            if (rnet_ring_get(&s->local_ring, t, &sample))
            {
                frames[count].tick = sample.tick;
                frames[count].size = sample.size;
                memcpy(frames[count].bytes, sample.bytes, sample.size);
                count++;
            }
            if (t == 0xffffffffu)
            {
                break;
            }
            t++;
        }
        if (count == 0)
        {
            continue;
        }
        if (s->cfg.slot_count > 2)
            len = rnet_proto_encode_input_acks(buf, sizeof(buf), s->cfg.protocol_magic,
                                               s->cfg.session_id, s->wire_slot, s->input_epoch,
                                               ack, frames, count, ack_vector, s->cfg.slot_count);
        else
            len = rnet_proto_encode_input(buf, sizeof(buf), s->cfg.protocol_magic,
                                          s->cfg.session_id, s->wire_slot, s->input_epoch, ack,
                                          frames, count);
        if (len < 0)
        {
            break;
        }
        send_raw(s, buf, len);
        s->input_bundle_sends++;
        sent_any = 1;
    }
    if (!sent_any)
    {
        return;
    }
    s->last_input_ms = now;
    s->last_input_tip = tip;
    s->last_input_tip_valid = 1;
}

static int remotes_ready_for_play_wire(RNetSession *s, rnet_u32 play_wire)
{
    rnet_u8 slot;
    RNetInputSample tmp;

    for (slot = 0; slot < s->cfg.slot_count; ++slot)
    {
        if (slot == s->cfg.local_slot)
        {
            continue;
        }
        if (!rnet_config_slot_occupied(&s->cfg, slot))
        {
            continue; /* empty lobby seat — local neutral, not on the wire */
        }
        if (!rnet_ring_get(&s->remote_rings[slot], play_wire, &tmp))
        {
            return 0;
        }
    }
    return 1;
}

/* Gameplay ticks 0..D-1 have no prior human sample. Seed them with a
 * deterministic neutral row; fresh input sampled at sim T is stored at T+D. */
static void seed_delay_prefix(RNetSession *s)
{
    rnet_u32 t;
    if (s == NULL) return;
    for (t = 0; t < (rnet_u32)s->delay; ++t)
    {
        RNetInputSample sample;
        if (rnet_ring_get(&s->local_ring, t, &sample)) continue;
        memset(&sample, 0, sizeof(sample));
        sample.tick = t;
        sample.valid = 1;
        rnet_ring_store(&s->local_ring, &sample);
    }
}

static int collect_wire_inputs(RNetSession *s, rnet_u32 wire,
                               RNetInputSample *resolved)
{
    rnet_u8 slot;
    if (s == NULL || resolved == NULL) return 0;
    memset(resolved, 0, sizeof(RNetInputSample) * RNET_MAX_SLOTS);
    for (slot = 0; slot < s->cfg.slot_count; ++slot)
    {
        RNetInputSample sample;
        int found;
        if (!rnet_config_slot_occupied(&s->cfg, slot))
        {
            memset(&sample, 0, sizeof(sample));
            sample.tick = wire;
            sample.valid = 1;
            resolved[slot] = sample;
            continue;
        }
        found = slot == s->cfg.local_slot
            ? rnet_ring_get(&s->local_ring, wire, &sample)
            : rnet_ring_get(&s->remote_rings[slot], wire, &sample);
        if (!found) return 0;
        resolved[slot] = sample;
        resolved[slot].tick = wire;
    }
    return 1;
}

/* Resolve and advertise a wire row as soon as both peers' inputs exist. The
 * row may be D frames in the future; that lead time absorbs confirmation RTT. */
static int prepare_wire_confirm(RNetSession *s, rnet_u32 wire, int force_send)
{
    RNetInputSample resolved[RNET_MAX_SLOTS];
    rnet_u32 index = wire % RNET_HISTORY_LENGTH;
    rnet_u32 hash;
    rnet_u64 now;
    rnet_u8 slot;

    if (!collect_wire_inputs(s, wire, resolved)) return 0;
    hash = hash_resolved_inputs(wire, resolved, (int)s->cfg.slot_count);
    s->published_tick[index] = wire;
    s->published_hash[index] = hash;
    s->published_valid[index] = 1;

    for (slot = 0; slot < s->cfg.slot_count; ++slot)
    {
        if (slot == s->cfg.local_slot) continue;
        if (!rnet_config_slot_occupied(&s->cfg, slot)) continue;
        if (s->peer_history_valid[index][slot] &&
            s->peer_history_tick[index][slot] == wire &&
            s->peer_history_hash[index][slot] != hash)
        {
            s->input_desync = 1;
            s->desync_tick = wire;
            s->desync_local_hash = hash;
            s->desync_remote_hash = s->peer_history_hash[index][slot];
            return 0;
        }
    }

    now = session_now(s);
    if (force_send || s->confirm_last_sent_ms[index] == 0 ||
        now - s->confirm_last_sent_ms[index] >= 4ULL)
        send_input_confirm_tick(s, wire, hash);
    return 1;
}

static int wire_confirmations_agree(RNetSession *s, rnet_u32 wire)
{
    rnet_u32 index = wire % RNET_HISTORY_LENGTH;
    rnet_u8 slot;
    if (!s->published_valid[index] || s->published_tick[index] != wire)
        return 0;
    for (slot = 0; slot < s->cfg.slot_count; ++slot)
    {
        if (slot == s->cfg.local_slot) continue;
        if (!rnet_config_slot_occupied(&s->cfg, slot)) continue;
        if (!s->peer_history_valid[index][slot] ||
            s->peer_history_tick[index][slot] != wire)
            return 0;
        if (s->peer_history_hash[index][slot] != s->published_hash[index])
            return 0;
    }
    return 1;
}

/* Retransmit confirms for [play - trail, tip], not only [play, tip].
 * Otherwise a lost INPUT_CONFIRM for an already-admitted tick leaves the
 * slower peer in wait_confirm forever while the faster peer runs ahead
 * (spam rematch over ICE: guest@19 wait_confirm, host@26 wait_remote).
 * Prefer the cached published hash so trailing ticks still retransmit after
 * rings have advanced past the original inputs. */
static void refresh_confirm_window(RNetSession *s, rnet_u32 play_wire, rnet_u32 sample_wire)
{
    rnet_u32 trail;
    rnet_u32 lo;
    rnet_u32 w;
    rnet_u64 now;

    if (s == NULL)
        return;
    trail = (rnet_u32)s->delay + (rnet_u32)s->cfg.bundle_redundancy + 8u;
    if (trail < 16u)
        trail = 16u;
    if (trail > (rnet_u32)RNET_HISTORY_LENGTH / 2u)
        trail = (rnet_u32)RNET_HISTORY_LENGTH / 2u;
    lo = (play_wire > trail) ? (play_wire - trail) : 0u;
    if (sample_wire < lo)
        sample_wire = lo;
    now = session_now(s);
    for (w = lo; w <= sample_wire; ++w)
    {
        rnet_u32 index = w % RNET_HISTORY_LENGTH;
        if (s->published_valid[index] && s->published_tick[index] == w)
        {
            if (s->confirm_last_sent_ms[index] == 0 ||
                now - s->confirm_last_sent_ms[index] >= 4ULL)
                send_input_confirm_tick(s, w, s->published_hash[index]);
        }
        else
        {
            (void)prepare_wire_confirm(s, w, 0);
        }
        if (w == 0xffffffffu)
            break;
    }
}

RNetSession *rnet_session_create(const RNetConfig *cfg, const RNetHostVTable *host)
{
    RNetSession *s;
    rnet_u8 i;
    if ((cfg == NULL) || (host == NULL) || (host->sample_local == NULL) || (host->publish == NULL))
    {
        return NULL;
    }
    if (cfg->slot_count < 2 || cfg->slot_count > RNET_MAX_SLOTS)
    {
        return NULL;
    }
    /* `slot_count` exactly is the observer sentinel; above it is a seat that
     * does not exist and stays an error, so a typo cannot quietly become a
     * session that waits on nobody. */
    if (cfg->local_slot > cfg->slot_count)
    {
        return NULL;
    }
    /* An observer must carry a wire id outside the seat range. Sending as a
     * player slot would have the relay forward its packets and the peers
     * store them as that seat's input -- the exact thing a spectator must not
     * be able to do. Refuse rather than silently send as a player. */
    if (cfg->local_slot == cfg->slot_count &&
        rnet_config_wire_slot(cfg) < cfg->slot_count)
    {
        return NULL;
    }
    s = (RNetSession *)calloc(1, sizeof(*s));
    if (s == NULL)
    {
        return NULL;
    }
    s->cfg = *cfg;
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
        s->remote_contiguous_ack[i] = 0xffffffffu;
    s->host = *host;
    s->delay = cfg->input_delay;
    s->phase = RNET_PHASE_IDLE;
    s->is_observer = rnet_config_is_observer(cfg) ? 1 : 0;
    s->wire_slot = rnet_config_wire_slot(cfg);
    /* An observer is never authority: its local_slot is the sentinel, never
     * 0, so this already answers no -- stated rather than relied upon. */
    s->is_sim_authority = (!s->is_observer && cfg->local_slot == 0) ? 1 : 0;
    s->rb_peer_slot = -1;
    s->rb_last_from = -1;
    s->state_taken = -1;
    s->session_start_ms = rnet_os_monotonic_ms();
    s->last_peer_rx_ms = 0;
    s->peer_gone = 0;
    rnet_transport_init(&s->transport);
    rnet_ring_clear(&s->local_ring);
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
    {
        rnet_ring_clear(&s->remote_rings[i]);
    }
    return s;
}

void rnet_session_destroy(RNetSession *s)
{
    if (s == NULL)
    {
        return;
    }
    state_clear(s);
    rnet_transport_shutdown(&s->transport);
    rnet_ice_agent_destroy(s->ice);
    s->ice = NULL;
    {
        int i;
        for (i = 0; i < RNET_MAX_SLOTS; ++i)
        {
            rnet_ice_agent_destroy(s->ice_hub[i]);
            s->ice_hub[i] = NULL;
        }
    }
    free(s);
}

int rnet_session_start_lan(RNetSession *s, const char *bind_hostport, const char *peer_hostport)
{
    if (s == NULL)
    {
        return -1;
    }
    if (rnet_transport_start_lan(&s->transport, bind_hostport, peer_hostport) != 0)
    {
        return -1;
    }
    s->phase = RNET_PHASE_LINKING;
    s->last_hello_ms = 0;
    return 0;
}

int rnet_session_start_lan_hub(RNetSession *s, const char *bind_hostport)
{
    if (s == NULL)
    {
        return -1;
    }
    /* Transport fan-out hub (lobby owner). Sim local_slot is independent —
     * seats may be reordered; guests still dial this peer's endpoint. */
    if (rnet_transport_start_lan_hub(&s->transport, bind_hostport) != 0)
    {
        return -1;
    }
    s->phase = RNET_PHASE_LINKING;
    s->last_hello_ms = 0;
    return 0;
}

int rnet_session_start_ice(RNetSession *s, const RNetIceConfig *ice)
{
#if !defined(RNET_ENABLE_ICE)
    (void)s;
    (void)ice;
    return -1;
#else
    RNetIceConfig local;
    if ((s == NULL) || (ice == NULL))
    {
        return -1;
    }
    local = *ice;
    if (s->ice != NULL)
    {
        rnet_ice_agent_destroy(s->ice);
        s->ice = NULL;
    }
    s->ice = rnet_ice_agent_create(&local, ice_emit_bridge, s);
    if (s->ice == NULL)
    {
        return -1;
    }
    rnet_transport_shutdown(&s->transport);
    rnet_transport_init(&s->transport);
    s->transport.mode = RNET_TRANSPORT_ICE;
    s->transport.ice_send = ice_send_bridge;
    s->transport.ice_recv = ice_recv_bridge;
    s->transport.ice_ctx = s->ice;
    s->ice_attempt_ms = session_now(s);
    s->ice_completed_ms = 0;
    if (rnet_ice_agent_start_gathering(s->ice) != 0)
    {
        return -1;
    }
    /* Stay IDLE until ICE completes; pump will promote to LINKING. */
    s->phase = RNET_PHASE_IDLE;
    return 0;
#endif
}

int rnet_session_start_ice_hub(RNetSession *s, const RNetIceConfig *ice, rnet_u32 guest_slot_mask)
{
#if !defined(RNET_ENABLE_ICE)
    (void)s;
    (void)ice;
    (void)guest_slot_mask;
    return -1;
#else
    RNetIceConfig local;
    void *ctx[RNET_MAX_SLOTS];
    int i;
    int any = 0;

    if ((s == NULL) || (ice == NULL))
    {
        return -1;
    }
    /* Guest seats are 1..RNET_MAX_SLOTS-1; bit 0 and out-of-range bits invalid. */
    if ((guest_slot_mask & 1u) != 0u || (guest_slot_mask >> RNET_MAX_SLOTS) != 0u ||
        guest_slot_mask == 0u)
    {
        return -1;
    }
    local = *ice;
    local.bind_port = 0;   /* never a fixed port: N agents on one port collide */
    local.controlling = 0; /* host answers; guests offer */

    if (s->ice != NULL)
    {
        rnet_ice_agent_destroy(s->ice);
        s->ice = NULL;
    }
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
    {
        rnet_ice_agent_destroy(s->ice_hub[i]);
        s->ice_hub[i] = NULL;
        ctx[i] = NULL;
    }
    s->ice_hub_mask = 0;
    rnet_transport_shutdown(&s->transport);
    rnet_transport_init(&s->transport);

    for (i = 1; i < RNET_MAX_SLOTS; ++i)
    {
        if ((guest_slot_mask & (1u << i)) == 0u)
        {
            continue;
        }
        s->ice_hub_bridge[i].s = s;
        s->ice_hub_bridge[i].slot = i;
        s->ice_hub[i] = rnet_ice_agent_create(&local, ice_hub_emit_bridge, &s->ice_hub_bridge[i]);
        if (s->ice_hub[i] == NULL)
        {
            goto fail;
        }
        ctx[i] = s->ice_hub[i];
        any = 1;
    }
    if (!any || rnet_transport_start_ice_hub(&s->transport, ice_send_bridge, ice_recv_bridge, ctx) != 0)
    {
        goto fail;
    }
    s->ice_hub_mask = guest_slot_mask;
    s->ice_attempt_ms = session_now(s);
    s->ice_completed_ms = 0;
    for (i = 1; i < RNET_MAX_SLOTS; ++i)
    {
        /* Answerer: records gather_pending; gathers on the guest's offer. */
        if (s->ice_hub[i] != NULL && rnet_ice_agent_start_gathering(s->ice_hub[i]) != 0)
        {
            goto fail;
        }
    }
    s->phase = RNET_PHASE_IDLE;
    return 0;
fail:
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
    {
        rnet_ice_agent_destroy(s->ice_hub[i]);
        s->ice_hub[i] = NULL;
    }
    s->ice_hub_mask = 0;
    rnet_transport_shutdown(&s->transport);
    rnet_transport_init(&s->transport);
    return -1;
#endif
}

/* Tear down whatever transport the session had, so an adopted agent starts
 * from a clean slate. */
static void session_ice_reset_transport(RNetSession *s)
{
    int i;
    if (s->ice != NULL)
    {
        rnet_ice_agent_destroy(s->ice);
        s->ice = NULL;
    }
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
    {
        rnet_ice_agent_destroy(s->ice_hub[i]);
        s->ice_hub[i] = NULL;
    }
    s->ice_hub_mask = 0;
    rnet_transport_shutdown(&s->transport);
    rnet_transport_init(&s->transport);
}

int rnet_session_start_ice_hub_adopt(RNetSession *s, const RNetIceAdoptSeat *seats, int n)
{
#if !defined(RNET_ENABLE_ICE)
    (void)s;
    (void)seats;
    (void)n;
    return -1;
#else
    void *ctx[RNET_MAX_SLOTS];
    rnet_u32 mask = 0;
    int i;

    if ((s == NULL) || (seats == NULL) || n < 1 || n >= RNET_MAX_SLOTS)
    {
        return -1;
    }
    /* Validate everything before taking anything: on -1 the caller still owns
     * every agent. The mask is built only from agents that are COMPLETED -- a
     * seat that is not connected fails the start instead of shrinking the room. */
    for (i = 0; i < n; ++i)
    {
        const int slot = seats[i].slot;
        if (seats[i].agent == NULL || slot < 1 || slot >= RNET_MAX_SLOTS ||
            (mask & (1u << slot)) != 0u ||
            rnet_ice_agent_state(seats[i].agent) != RNET_ICE_STATE_COMPLETED)
        {
            return -1;
        }
        mask |= (1u << slot);
    }
    memset(ctx, 0, sizeof(ctx));
    session_ice_reset_transport(s);
    for (i = 0; i < n; ++i)
    {
        const int slot = seats[i].slot;
        s->ice_hub[slot] = seats[i].agent;
        s->ice_hub_bridge[slot].s = s;
        s->ice_hub_bridge[slot].slot = slot;
        rnet_ice_agent_set_emit(seats[i].agent, ice_hub_emit_bridge, &s->ice_hub_bridge[slot]);
        rnet_ice_agent_freeze(seats[i].agent);
        ctx[slot] = seats[i].agent;
    }
    if (rnet_transport_start_ice_hub(&s->transport, ice_send_bridge, ice_recv_bridge, ctx) != 0)
    {
        /* Not reachable for a non-empty ctx; still, hand ownership back. */
        for (i = 0; i < n; ++i)
            s->ice_hub[seats[i].slot] = NULL;
        return -1;
    }
    s->ice_hub_mask = mask;
    /* Dead-path timers measure from adoption, not from the waiting room. */
    s->ice_attempt_ms = session_now(s);
    s->ice_completed_ms = 0;
    s->last_peer_rx_ms = 0ULL;
    s->phase = RNET_PHASE_IDLE;
    return 0;
#endif
}

int rnet_session_adopt_ice_agent(RNetSession *s, RNetIceAgent *agent)
{
#if !defined(RNET_ENABLE_ICE)
    (void)s;
    (void)agent;
    return -1;
#else
    if ((s == NULL) || (agent == NULL) ||
        rnet_ice_agent_state(agent) != RNET_ICE_STATE_COMPLETED)
    {
        return -1;
    }
    session_ice_reset_transport(s);
    s->ice = agent;
    rnet_ice_agent_set_emit(agent, ice_emit_bridge, s);
    rnet_ice_agent_freeze(agent);
    s->transport.mode = RNET_TRANSPORT_ICE;
    s->transport.ice_send = ice_send_bridge;
    s->transport.ice_recv = ice_recv_bridge;
    s->transport.ice_ctx = agent;
    s->ice_attempt_ms = session_now(s);
    s->ice_completed_ms = 0;
    s->last_peer_rx_ms = 0ULL;
    s->phase = RNET_PHASE_IDLE;
    return 0;
#endif
}

#if defined(RNET_ENABLE_ICE)
/* After STUN/host ICE fails or stalls, one automatic gather with force_relay
 * when TURN credentials are present. Opt out: RNET_ICE_NO_RELAY_FALLBACK=1.
 *
 * Timers (env overrides):
 *   RNET_ICE_RELAY_FALLBACK_MS — general stall (default 5000, was 12000)
 *   RNET_ICE_RELAY_PRIVATE_MS  — early fallback when every remote so far is
 *                                RFC1918 (default 2500); host/STUN cannot
 *                                reach CGNAT LAN candidates
 *   RNET_ICE_RELAY_DEAD_MS     — completed non-relay but no session RX
 */
static void session_maybe_ice_relay_fallback(RNetSession *s)
{
    RNetIceState st;
    rnet_u64 now;
    rnet_u64 elapsed;
    rnet_u64 stuck_ms = 5000ULL;
    rnet_u64 private_ms = 2500ULL;
    rnet_u64 dead_ms = 6000ULL;
    const char *env;
    char path[32];
    int only_private;

    if (s == NULL || s->ice == NULL || s->phase == RNET_PHASE_RUNNING)
        return;
    if (rnet_ice_agent_relay_fallback_done(s->ice) || rnet_ice_agent_is_force_relay(s->ice))
        return;
    if (!rnet_ice_agent_has_turn(s->ice))
        return;
    env = getenv("RNET_ICE_NO_RELAY_FALLBACK");
    if (env != NULL && env[0] != '\0' && env[0] != '0')
        return;
    env = getenv("RNET_ICE_RELAY_FALLBACK_MS");
    if (env != NULL && env[0] != '\0')
    {
        long v = strtol(env, NULL, 10);
        if (v >= 2000L && v <= 60000L)
            stuck_ms = (rnet_u64)v;
    }
    env = getenv("RNET_ICE_RELAY_PRIVATE_MS");
    if (env != NULL && env[0] != '\0')
    {
        long v = strtol(env, NULL, 10);
        if (v >= 1000L && v <= 30000L)
            private_ms = (rnet_u64)v;
    }
    env = getenv("RNET_ICE_RELAY_DEAD_MS");
    if (env != NULL && env[0] != '\0')
    {
        long v = strtol(env, NULL, 10);
        if (v >= 2000L && v <= 30000L)
            dead_ms = (rnet_u64)v;
    }
    if (private_ms > stuck_ms)
        private_ms = stuck_ms;

    now = session_now(s);
    elapsed = (s->ice_attempt_ms != 0ULL && now >= s->ice_attempt_ms)
                  ? (now - s->ice_attempt_ms)
                  : 0ULL;
    st = rnet_ice_agent_state(s->ice);
    only_private = rnet_ice_agent_remote_only_private(s->ice);

    if (st == RNET_ICE_STATE_FAILED)
        goto do_fallback;
    if (st != RNET_ICE_STATE_CONNECTED && st != RNET_ICE_STATE_COMPLETED)
    {
        /* CGNAT / internet: only private host remotes will never connect via
         * host/STUN — cut over to force_relay without waiting the full stall. */
        if (only_private && elapsed >= private_ms)
            goto do_fallback;
        if (elapsed >= stuck_ms)
            goto do_fallback;
        return;
    }
    /* Completed on a non-relay path but no session packets yet — flaky CGNAT. */
    if (s->last_peer_rx_ms == 0ULL && s->ice_completed_ms != 0ULL &&
        now - s->ice_completed_ms >= dead_ms)
    {
        path[0] = '\0';
        rnet_ice_agent_selected_info(s->ice, path, sizeof(path), NULL, 0, NULL, 0);
        if (path[0] != '\0' && strcmp(path, "relay") != 0)
            goto do_fallback;
    }
    return;

do_fallback:
    if (only_private)
    {
        fprintf(stderr,
                "rnet_ice: TURN fallback with RFC1918-only remotes so far — "
                "restarting force_relay so both sides can gather typ relay "
                "(early=%llums stall=%llums)\n",
                (unsigned long long)private_ms, (unsigned long long)stuck_ms);
    }
    else
    {
        fprintf(stderr,
                "rnet_ice: TURN fallback after host/STUN stall (%llums) — "
                "restarting force_relay\n",
                (unsigned long long)stuck_ms);
    }
    if (rnet_ice_agent_restart_force_relay(s->ice) != 0)
        return;
    s->transport.ice_ctx = s->ice;
    s->ice_attempt_ms = now;
    s->ice_completed_ms = 0;
    s->phase = RNET_PHASE_IDLE;
}
#endif

void rnet_session_pump(RNetSession *s)
{
    if (s == NULL)
    {
        return;
    }
    if (s->ice_hub_mask != 0u)
    {
        int i;
        int all_completed = 1;
        for (i = 1; i < RNET_MAX_SLOTS; ++i)
        {
            if (s->ice_hub[i] == NULL)
            {
                continue;
            }
            rnet_ice_agent_poll(s->ice_hub[i]);
            if (rnet_ice_agent_state(s->ice_hub[i]) == RNET_ICE_STATE_COMPLETED)
            {
                /* Negotiation is over: no later signal may rebuild this seat. */
                rnet_ice_agent_freeze(s->ice_hub[i]);
            }
            else
            {
                all_completed = 0;
            }
        }
        if (s->phase == RNET_PHASE_IDLE && all_completed)
        {
            s->phase = RNET_PHASE_LINKING;
            if (s->ice_completed_ms == 0ULL)
                s->ice_completed_ms = session_now(s);
        }
    }
    if (s->ice != NULL)
    {
#if defined(RNET_ENABLE_ICE)
        session_maybe_ice_relay_fallback(s);
#endif
        rnet_ice_agent_poll(s->ice);
        if (rnet_ice_agent_state(s->ice) == RNET_ICE_STATE_COMPLETED &&
            (!rnet_ice_agent_has_turn(s->ice) || s->last_peer_rx_ms != 0ULL))
        {
            /* Negotiation is over: a repeated REMOTE_SDP/CANDIDATE must not
             * rebuild the live agent. With TURN configured and no session
             * traffic yet, the dead-path fallback may still restart ICE (and
             * the peer's restart offer must be honoured), so freeze later. */
            rnet_ice_agent_freeze(s->ice);
        }
        if (s->phase == RNET_PHASE_IDLE && rnet_ice_agent_state(s->ice) == RNET_ICE_STATE_COMPLETED)
        {
            s->phase = RNET_PHASE_LINKING;
            if (s->ice_completed_ms == 0ULL)
                s->ice_completed_ms = session_now(s);
        }
    }
    pump_recv(s);
    maybe_bootstrap(s);
    profiles_drive(s);
    if (s->state_probe_active)
    {
        state_drive_probe(s);
    }
    if (state_any_active(s))
    {
        /* Burst recv↔send so ACK progress can refill the window inside one
         * host pump (otherwise TURN transfers are gated to one cwnd/frame). */
        int burst;
        for (burst = 0; burst < 4; burst++)
        {
            state_drive_sender(s);
            if (!state_any_in_progress(s))
                break;
            pump_recv(s);
        }
    }
    /* LOAD apply/ready suppresses INPUT; emit HELLO so peers keep stamping
     * last_peer_rx_ms (hash-match apply of a multi‑MB .pst is otherwise silent). */
    if (s->phase == RNET_PHASE_RUNNING && (s->input_send_suppress || s->state_stall_sim))
    {
        rnet_u64 now = session_now(s);
        if (now - s->last_hello_ms >= 250ULL)
        {
            rnet_u8 buf[RNET_MAX_PACKET];
            int len = rnet_proto_encode_hello(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                              s->wire_slot, s->cfg.slot_count, s->delay);
            send_raw(s, buf, len);
            s->last_hello_ms = now;
        }
    }
    send_input_bundle(s);
    /* Retransmit pending DELAY_SYNC until both peers apply (UDP). */
    if (s->delay_pending && s->phase == RNET_PHASE_RUNNING)
    {
        rnet_u64 now = session_now(s);
        if (s->delay_pending_last_tx_ms == 0ULL ||
            now - s->delay_pending_last_tx_ms >= 50ULL)
        {
            emit_delay_sync(s, s->delay_pending_value, s->delay_pending_effective);
        }
    }
}

int rnet_session_wait_recv(RNetSession *s, int timeout_ms)
{
    if (s == NULL)
    {
        return 0;
    }
    if (timeout_ms < 0)
    {
        timeout_ms = 0;
    }
    /* LAN UDP: block in poll so the peer can run without us busy-spinning. */
    if (s->transport.mode == RNET_TRANSPORT_LAN_UDP && rnet_os_socket_valid(s->transport.sock))
    {
        int r = rnet_os_poll_recv(s->transport.sock, timeout_ms);
        return (r > 0) ? 1 : 0;
    }
    /* ICE / no sock: coarse sleep only — still better than a busy spin. */
    if (timeout_ms > 0)
    {
        rnet_os_sleep_micros((unsigned)timeout_ms * 1000U);
    }
    return 0;
}

int rnet_session_try_admit(RNetSession *s, rnet_u32 sim_tick)
{
    RNetInputSample resolved[RNET_MAX_SLOTS];
    RNetInputSample local_play;
    RNetInputSample local_future;
    rnet_u32 play_wire;
    rnet_u32 sample_wire;
    rnet_u32 hash;
    rnet_u8 slot;

    if ((s == NULL) || (s->phase != RNET_PHASE_RUNNING))
    {
        if (s)
            note_admit_stall(s, RNET_ADMIT_NOT_RUNNING);
        return 0;
    }
    if (s->profiles.active && rnet_session_profiles_status(s, NULL, NULL) != 1)
    {
        note_admit_stall(s, RNET_ADMIT_NOT_RUNNING);
        return 0;
    }
    if (s->state_stall_sim && (state_any_active(s) || s->state_probe_active))
    {
        /* Stall while probe or chunked transfer is in flight. */
        note_admit_stall(s, RNET_ADMIT_STATE_XFER);
        return 0;
    }
    if (sim_tick != s->sim_tick)
    {
        /* Host must advance in lockstep with session clock. */
        note_admit_stall(s, RNET_ADMIT_SIM_MISMATCH);
        return 0;
    }
    if (s->input_desync)
    {
        note_admit_stall(s, RNET_ADMIT_DESYNC);
        return 0;
    }

    /* Simulate wire T while sampling local input for wire T+D. Once the
     * neutral prefix is consumed, peer input normally arrived D frames ago. */
    play_wire = sim_tick;
    sample_wire = rnet_wire_tick_from_sim(sim_tick, s->delay);
    memset(&local_future, 0, sizeof(local_future));
    memset(&local_play, 0, sizeof(local_play));
    if (!s->is_observer)
    {
        if (!rnet_ring_get(&s->local_ring, sample_wire, &local_future))
        {
            memset(&local_future, 0, sizeof(local_future));
            s->host.sample_local(sim_tick, &local_future, s->host.ctx);
            local_future.tick = sample_wire;
            local_future.valid = 1;
            if (local_future.size > RNET_INPUT_MAX)
            {
                local_future.size = RNET_INPUT_MAX;
            }
            rnet_ring_store(&s->local_ring, &local_future);
        }

        send_input_bundle(s);
    }
    /* An observer samples nothing and sends nothing.
     *
     * Not sampling is the point: its controllers must not reach the match,
     * and the surest way is for the pad never to enter the pipeline at all.
     * Not sending follows -- it has no row anyone needs, the relay would drop
     * it anyway, and on a direct path a peer would reject it as an
     * out-of-range seat. Three layers agreeing, none of them relied upon
     * alone. It still sends HELLO / READY / confirms, which is what binds it
     * at the relay so the match reaches it. */
    refresh_confirm_window(s, play_wire, sample_wire);

    if (!s->is_observer && !rnet_ring_get(&s->local_ring, play_wire, &local_play))
    {
        if (play_wire == sample_wire)
            local_play = local_future;
        else
        {
            send_input_bundle(s);
            note_admit_stall(s, RNET_ADMIT_WAIT_LOCAL_INPUT);
            return 0;
        }
    }

    if (!remotes_ready_for_play_wire(s, play_wire))
    {
        send_input_bundle(s);
        note_admit_stall(s, RNET_ADMIT_WAIT_REMOTE_INPUT);
        return 0;
    }

    memset(resolved, 0, sizeof(resolved));
    for (slot = 0; slot < s->cfg.slot_count; ++slot)
    {
        if (slot == s->cfg.local_slot)
        {
            resolved[slot] = local_play;
            resolved[slot].tick = sim_tick;
        }
        else if (!rnet_config_slot_occupied(&s->cfg, slot))
        {
            /* Sparse lobby seat: deterministic neutral (matches delay-prefix
             * seed zeros). All peers synthesize the same bytes locally. */
            memset(&resolved[slot], 0, sizeof(resolved[slot]));
            resolved[slot].tick = sim_tick;
            resolved[slot].valid = 1;
        }
        else
        {
            RNetInputSample remote;
            if (!rnet_ring_get(&s->remote_rings[slot], play_wire, &remote))
            {
                note_admit_stall(s, RNET_ADMIT_WAIT_REMOTE_INPUT);
                return 0;
            }
            resolved[slot] = remote;
            resolved[slot].tick = sim_tick;
        }
    }

    hash = hash_resolved_inputs(sim_tick, resolved, (int)s->cfg.slot_count);

    if (!prepare_wire_confirm(s, play_wire, 0) || s->input_desync) {
        note_admit_stall(s, s->input_desync ? RNET_ADMIT_DESYNC
                                            : RNET_ADMIT_WAIT_CONFIRM);
        return 0;
    }
    if (s->published_hash[play_wire % RNET_HISTORY_LENGTH] != hash)
    {
        s->input_desync = 1;
        s->desync_tick = play_wire;
        s->desync_local_hash = hash;
        s->desync_remote_hash =
            s->published_hash[play_wire % RNET_HISTORY_LENGTH];
        note_admit_stall(s, RNET_ADMIT_DESYNC);
        return 0;
    }
    if (!wire_confirmations_agree(s, play_wire))
    {
        (void)prepare_wire_confirm(s, play_wire, 0);
        send_input_bundle(s);
        note_admit_stall(s, RNET_ADMIT_WAIT_CONFIRM);
        return 0;
    }
    s->host.publish(sim_tick, resolved, (int)s->cfg.slot_count, s->host.ctx);
    note_admit_ok(s);
    return 1;

#if 0 /* Legacy strict confirmation barrier; superseded by delay pipeline. */

    if (!s->confirm_active || s->confirm_sim_tick != sim_tick)
    {
        /* Peer INPUT_CONFIRM may arrive before we activate. Preserve those
         * same-tick sightings — wiping them races the slower peer into a
         * permanent stall once the faster peer admits and stops retransmit. */
        rnet_u8 saved_seen[RNET_MAX_SLOTS];
        rnet_u32 saved_hash[RNET_MAX_SLOTS];
        memcpy(saved_seen, s->confirm_seen, sizeof(saved_seen));
        memcpy(saved_hash, s->peer_confirm_hash, sizeof(saved_hash));

        s->confirm_active = 1;
        s->confirm_sim_tick = sim_tick;
        s->confirm_hash = hash;
        memcpy(s->confirm_resolved, resolved, sizeof(resolved));
        memset(s->confirm_seen, 0, sizeof(s->confirm_seen));
        memset(s->peer_confirm_hash, 0, sizeof(s->peer_confirm_hash));
        for (slot = 0; slot < s->cfg.slot_count; ++slot)
        {
            if (slot == s->cfg.local_slot)
            {
                continue;
            }
            if (saved_seen[slot])
            {
                s->confirm_seen[slot] = 1;
                s->peer_confirm_hash[slot] = saved_hash[slot];
                if (saved_hash[slot] != hash)
                {
                    s->input_desync = 1;
                    s->desync_tick = sim_tick;
                    s->desync_local_hash = hash;
                    s->desync_remote_hash = saved_hash[slot];
                    return 0;
                }
            }
        }
        s->confirm_seen[s->cfg.local_slot] = 1;
        s->peer_confirm_hash[s->cfg.local_slot] = hash;
        send_input_confirm(s);
        send_input_bundle(s);
        /* Peer CONFIRM may already be in saved_seen (arrived before we
         * activated). Admit immediately when everyone already agrees. */
        if (confirms_agree(s))
        {
            s->host.publish(sim_tick, s->confirm_resolved, (int)s->cfg.slot_count, s->host.ctx);
            s->confirm_active = 0;
            return 1;
        }
        return 0;
    }

    if (hash != s->confirm_hash)
    {
        s->input_desync = 1;
        s->desync_tick = sim_tick;
        s->desync_local_hash = hash;
        s->desync_remote_hash = s->confirm_hash;
        return 0;
    }

    now = session_now(s);
    if (now - s->last_confirm_ms >= 4ULL)
    {
        send_input_confirm(s);
    }

    if (!confirms_agree(s))
    {
        send_input_bundle(s);
        return 0;
    }

    s->host.publish(sim_tick, s->confirm_resolved, (int)s->cfg.slot_count, s->host.ctx);
    s->confirm_active = 0;
    return 1;
#endif
}

static void apply_pending_delay(RNetSession *s)
{
    if (s == NULL || !s->delay_pending)
    {
        return;
    }
    if (s->sim_tick < s->delay_pending_effective)
    {
        return;
    }
    s->delay = s->delay_pending_value;
    s->cfg.input_delay = s->delay_pending_value;
    s->delay_pending = 0;
}

static void emit_delay_sync(RNetSession *s, rnet_u8 new_delay, rnet_u32 effective_tick)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    int len;

    if (s == NULL)
    {
        return;
    }
    len = rnet_proto_encode_delay_sync(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                       new_delay, effective_tick);
    if (len > 0)
    {
        send_raw(s, buf, len);
        s->delay_pending_last_tx_ms = session_now(s);
    }
}

void rnet_session_advance(RNetSession *s)
{
    if (s == NULL)
    {
        return;
    }
    s->sim_tick++;
    apply_pending_delay(s);
}

int rnet_session_input_desync(const RNetSession *s, rnet_u32 *tick, rnet_u32 *local_hash, rnet_u32 *remote_hash)
{
    if ((s == NULL) || !s->input_desync)
    {
        return 0;
    }
    if (tick != NULL)
    {
        *tick = s->desync_tick;
    }
    if (local_hash != NULL)
    {
        *local_hash = s->desync_local_hash;
    }
    if (remote_hash != NULL)
    {
        *remote_hash = s->desync_remote_hash;
    }
    return 1;
}

int rnet_session_send_bye(RNetSession *s)
{
    rnet_u8 buf[64];
    int n;

    if ((s == NULL) || (s->transport.mode == RNET_TRANSPORT_NONE))
    {
        return -1;
    }
    n = rnet_proto_encode_bye(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id, s->wire_slot);
    if (n <= 0)
    {
        return -1;
    }
    /* Best-effort: send a few times so a single loss doesn't leave peer hanging. */
    (void)rnet_transport_send(&s->transport, buf, (size_t)n);
    (void)rnet_transport_send(&s->transport, buf, (size_t)n);
    (void)rnet_transport_send(&s->transport, buf, (size_t)n);
    return 0;
}

int rnet_session_peer_disconnected(const RNetSession *s, rnet_u64 timeout_ms)
{
    rnet_u64 now;
    rnet_u64 last;

    if (s == NULL)
    {
        return 0;
    }
    if (s->peer_gone)
    {
        return 1;
    }
    if (timeout_ms == 0)
    {
        return 0;
    }
    now = rnet_os_monotonic_ms();
    if (s->last_peer_rx_ms == 0)
    {
        /* No peer traffic yet — only after we expected packets (linking/running).
         * Rematch session_reboot is often >15s on the slower peer; the old
         * timeout_ms*10 (~15s at 1500) false-disconnected the fast peer and
         * BYE'd both back to lobby. Keep a long link budget; callers that
         * pass timeout_ms==0 skip this path entirely (BYE-only). */
        rnet_u64 link_budget_ms;
        if (s->phase != RNET_PHASE_RUNNING && s->phase != RNET_PHASE_LINKING)
        {
            return 0;
        }
        link_budget_ms = timeout_ms * 60u;
        if (link_budget_ms < 90000u)
            link_budget_ms = 90000u;
        if (s->session_start_ms != 0 && (now - s->session_start_ms) > link_budget_ms)
        {
            return 1;
        }
        return 0;
    }
    last = s->last_peer_rx_ms;
    return (now > last && (now - last) >= timeout_ms) ? 1 : 0;
}

void rnet_session_touch_peer_liveness(RNetSession *s)
{
    rnet_u64 now;
    rnet_u8 slot;
    if (s == NULL)
    {
        return;
    }
    now = session_now(s);
    /* Per seat: every occupied remote seat that has not said BYE. Done even
     * when the aggregate is already gone -- one seat leaving a room of four
     * must not freeze the others' silence budgets. */
    for (slot = 0; slot < s->cfg.slot_count && slot < RNET_MAX_SLOTS; ++slot)
    {
        if (slot == s->cfg.local_slot || !rnet_config_slot_occupied(&s->cfg, slot) ||
            (s->peer_gone_mask & (1u << slot)))
        {
            continue;
        }
        s->peer_rx_ms[slot] = now ? now : 1u;
    }
    if (s->peer_gone)
    {
        return;
    }
    s->last_peer_rx_ms = now;
}

/* Remote seat index valid for the per-seat liveness accessors. */
static int liveness_seat_ok(const RNetSession *s, int slot)
{
    return s != NULL && slot >= 0 && slot < (int)s->cfg.slot_count && slot < RNET_MAX_SLOTS &&
           slot != (int)s->cfg.local_slot;
}

int rnet_session_peer_gone(const RNetSession *s, int slot)
{
    if (!liveness_seat_ok(s, slot))
    {
        return 0;
    }
    return (s->peer_gone_mask & (1u << slot)) ? 1 : 0;
}

rnet_u32 rnet_session_peer_gone_mask(const RNetSession *s)
{
    return (s != NULL) ? s->peer_gone_mask : 0u;
}

rnet_u64 rnet_session_peer_rx_age_ms(const RNetSession *s, int slot)
{
    rnet_u64 now;
    if (!liveness_seat_ok(s, slot) || s->peer_rx_ms[slot] == 0)
    {
        return RNET_PEER_RX_NEVER;
    }
    now = session_now((RNetSession *)s);
    return (now > s->peer_rx_ms[slot]) ? (now - s->peer_rx_ms[slot]) : 0u;
}

int rnet_session_peer_slot_disconnected(const RNetSession *s, int slot, rnet_u64 timeout_ms)
{
    rnet_u64 age;
    if (!liveness_seat_ok(s, slot) || !rnet_config_slot_occupied(&s->cfg, (rnet_u8)slot))
    {
        return 0;
    }
    if (s->peer_gone_mask & (1u << slot))
    {
        return 1;
    }
    if (timeout_ms == 0)
    {
        return 0;
    }
    if (s->peer_rx_ms[slot] == 0)
    {
        /* Never heard from this seat: the same long link budget as the
         * aggregate (rematch boots can exceed 15 s on one peer). */
        rnet_u64 link_budget_ms;
        rnet_u64 os_now;
        if (s->phase != RNET_PHASE_RUNNING && s->phase != RNET_PHASE_LINKING)
        {
            return 0;
        }
        link_budget_ms = timeout_ms * 60u;
        if (link_budget_ms < 90000u)
            link_budget_ms = 90000u;
        os_now = rnet_os_monotonic_ms();
        return (s->session_start_ms != 0 && os_now - s->session_start_ms > link_budget_ms) ? 1 : 0;
    }
    age = rnet_session_peer_rx_age_ms(s, slot);
    return (age >= timeout_ms) ? 1 : 0;
}

rnet_u32 rnet_session_disconnected_peers(const RNetSession *s, rnet_u64 timeout_ms)
{
    rnet_u32 mask = 0;
    int slot;
    if (s == NULL)
    {
        return 0;
    }
    for (slot = 0; slot < (int)s->cfg.slot_count && slot < RNET_MAX_SLOTS; ++slot)
    {
        if (rnet_session_peer_slot_disconnected(s, slot, timeout_ms))
        {
            mask |= 1u << slot;
        }
    }
    return mask;
}

void rnet_session_push_signal(RNetSession *s, const RNetSignal *msg)
{
    if ((s == NULL) || (msg == NULL) || (s->ice == NULL))
    {
        return;
    }
    rnet_ice_agent_push_signal(s->ice, msg);
}

int rnet_session_push_signal_from(RNetSession *s, int slot, const RNetSignal *sig)
{
    if ((s == NULL) || (sig == NULL) || slot < 1 || slot >= RNET_MAX_SLOTS ||
        s->ice_hub[slot] == NULL)
    {
        return -1;
    }
    rnet_ice_agent_push_signal(s->ice_hub[slot], sig);
    return 0;
}

rnet_u8 rnet_session_committed_delay(const RNetSession *s)
{
    return (s != NULL) ? s->delay : 0;
}

int rnet_session_request_delay_change(RNetSession *s, rnet_u8 new_delay)
{
    rnet_u32 effective;
    rnet_u8 clamped;

    if (s == NULL || s->phase != RNET_PHASE_RUNNING)
    {
        return 0;
    }
    clamped = new_delay;
    if (clamped < 2u)
    {
        clamped = 2u;
    }
    if (clamped > 20u)
    {
        clamped = 20u;
    }
    if (clamped == s->delay && !s->delay_pending)
    {
        return 0;
    }
    /* Margin: 2*D + 8 ticks so the packet can land before both peers admit. */
    effective = s->sim_tick + ((rnet_u32)s->delay * 2u) + 8u;
    if (s->delay_pending && s->delay_pending_effective > s->sim_tick &&
        s->delay_pending_value == clamped)
    {
        /* Already scheduled — refresh the wire copy. */
        emit_delay_sync(s, clamped, s->delay_pending_effective);
        return 1;
    }
    s->delay_pending = 1;
    s->delay_pending_value = clamped;
    s->delay_pending_effective = effective;
    emit_delay_sync(s, clamped, effective);
    return 1;
}

int rnet_session_local_slot(const RNetSession *s)
{
    return (s != NULL) ? (int)s->cfg.local_slot : -1;
}

int rnet_session_is_observer(const RNetSession *s)
{
    return (s != NULL) ? s->is_observer : 0;
}

rnet_u32 rnet_session_sim_tick(const RNetSession *s)
{
    return (s != NULL) ? s->sim_tick : 0;
}

int rnet_session_is_running(const RNetSession *s)
{
    return (s != NULL && s->phase == RNET_PHASE_RUNNING) ? 1 : 0;
}

RNetIceState rnet_session_ice_state(const RNetSession *s)
{
    if (s == NULL)
    {
        return RNET_ICE_STATE_IDLE;
    }
    if (s->ice_hub_mask != 0u)
    {
        /* Minimum over seats (enum order IDLE < ... < COMPLETED); FAILED is
         * the highest value but dominates, so report it if any seat failed. */
        int i;
        RNetIceState min = RNET_ICE_STATE_COMPLETED;
        for (i = 1; i < RNET_MAX_SLOTS; ++i)
        {
            RNetIceState st;
            if (s->ice_hub[i] == NULL)
                continue;
            st = rnet_ice_agent_state(s->ice_hub[i]);
            if (st == RNET_ICE_STATE_FAILED)
                return RNET_ICE_STATE_FAILED;
            if (st < min)
                min = st;
        }
        return min;
    }
    if (s->ice == NULL)
    {
        return RNET_ICE_STATE_IDLE;
    }
    return rnet_ice_agent_state(s->ice);
}

void rnet_session_get_stats(const RNetSession *s, RNetSessionStats *out)
{
    rnet_u8 slot;
    rnet_u32 highest_remote = 0;
    rnet_u64 now;
    int have_remote = 0;

    if (out == NULL)
        return;
    memset(out, 0, sizeof(*out));
    if (s == NULL)
        return;

    out->sim_tick = s->sim_tick;
    out->delay = s->delay;
    out->local_slot = s->cfg.local_slot;
    out->slot_count = s->cfg.slot_count;
    out->is_running = (s->phase == RNET_PHASE_RUNNING) ? 1 : 0;
    out->peer_gone = s->peer_gone;
    out->input_desync = s->input_desync;
    out->desync_tick = s->desync_tick;
    out->desync_local_hash = s->desync_local_hash;
    out->desync_remote_hash = s->desync_remote_hash;
    out->ice_state = rnet_session_ice_state(s);
    out->last_stall = s->last_stall;
    out->consecutive_stalls = s->consecutive_stalls;
    out->admit_ok_count = s->admit_ok_count;
    out->stall_streaks = s->stall_streaks;
    out->last_admit_wait_ms = s->last_admit_wait_ms;
    out->max_admit_wait_ms = s->max_admit_wait_ms;
    out->state_busy = (state_any_active(s) || s->state_probe_active) ? 1 : 0;
    /* The single-transfer fields describe the outbound transfer when there is
     * one, else the lowest-seat inbound one (the only one with two seats). */
    if (s->tx.active)
    {
        out->state_op = s->tx.op;
        out->state_sender = 1;
        out->state_bytes_total = s->tx.total;
        out->state_bytes_acked = state_tx_min_ack(s);
    }
    else
    {
        int i;
        const RNetStateRx *rx = NULL;
        for (i = 0; i < RNET_MAX_SLOTS && rx == NULL; ++i)
        {
            if (s->rx[i].active)
                rx = &s->rx[i];
        }
        out->state_op = (rx != NULL) ? rx->op : (s->state_probe_active ? s->state_probe_op : 0);
        out->state_sender = 0;
        out->state_bytes_total = (rx != NULL) ? rx->total : 0;
        out->state_bytes_acked = (rx != NULL) ? rx->contiguity : 0;
    }
    out->state_expect_mask = s->tx.active ? s->tx.expect_mask : 0u;
    out->state_done_mask = s->tx.active ? state_tx_done_mask(s) : 0u;
    {
        int i;
        for (i = 0; i < RNET_MAX_SLOTS; ++i)
        {
            if (s->rx[i].active)
                out->state_rx_mask |= 1u << i;
        }
    }
    out->state_probe_expect_mask = (s->state_probe_active && s->state_probe_sender) ? s->state_probe_expect_mask : 0u;
    out->state_probe_reply_mask = (s->state_probe_active && s->state_probe_sender) ? s->state_probe_reply_mask : 0u;
    out->peer_gone_mask = s->peer_gone_mask;
    {
        int i;
        for (i = 0; i < RNET_MAX_SLOTS; ++i)
            out->peer_rx_age_ms[i] = rnet_session_peer_rx_age_ms(s, i);
    }
    out->packets_rx = s->packets_rx;
    out->input_bundle_sends = s->input_bundle_sends;

    now = session_now((RNetSession *)s);
    if (s->last_peer_rx_ms != 0)
        out->last_peer_rx_age_ms = now - s->last_peer_rx_ms;
    /* Refresh live stall wait while still blocked. */
    if (s->stall_started_ms != 0 && s->last_stall != RNET_ADMIT_OK) {
        out->last_admit_wait_ms = (rnet_u32)(now - s->stall_started_ms);
        if (out->last_admit_wait_ms > out->max_admit_wait_ms)
            out->max_admit_wait_ms = out->last_admit_wait_ms;
    }

    for (slot = 0; slot < s->cfg.slot_count; ++slot) {
        rnet_u32 tip;
        if (slot == s->cfg.local_slot)
            continue;
        if (!rnet_config_slot_occupied(&s->cfg, slot))
            continue;
        tip = rnet_ring_highest_valid(&s->remote_rings[slot]);
        if (!have_remote || tip > highest_remote)
            highest_remote = tip;
        have_remote = 1;
    }
    out->highest_remote_wire = highest_remote;
    out->remote_lead = have_remote ? (int)highest_remote - (int)s->sim_tick : 0;

#if defined(RNET_ENABLE_ICE)
    if (s->ice_hub_mask != 0u) {
        int i;
        rnet_u32 drops = 0;
        for (i = 1; i < RNET_MAX_SLOTS; ++i)
            drops += rnet_ice_agent_recv_drops(s->ice_hub[i]);
        out->ice_recv_drops = drops;
        if (out->ice_state == RNET_ICE_STATE_FAILED)
            snprintf(out->ice_path, sizeof(out->ice_path), "failed");
        else if (out->ice_state == RNET_ICE_STATE_COMPLETED)
            snprintf(out->ice_path, sizeof(out->ice_path), "hub");
        else
            snprintf(out->ice_path, sizeof(out->ice_path), "pending");
    } else if (s->ice != NULL) {
        out->ice_recv_drops = rnet_ice_agent_recv_drops(s->ice);
        rnet_ice_agent_selected_info(s->ice, out->ice_path, sizeof(out->ice_path),
                                     out->ice_local, sizeof(out->ice_local),
                                     out->ice_remote, sizeof(out->ice_remote));
        if (out->ice_state == RNET_ICE_STATE_FAILED)
            snprintf(out->ice_path, sizeof(out->ice_path), "failed");
        else if (out->ice_state != RNET_ICE_STATE_CONNECTED &&
                 out->ice_state != RNET_ICE_STATE_COMPLETED &&
                 out->ice_path[0] == '\0')
            snprintf(out->ice_path, sizeof(out->ice_path), "pending");
    } else
#endif
    {
        snprintf(out->ice_path, sizeof(out->ice_path), "lan");
    }
}

int rnet_session_state_probe(RNetSession *s, rnet_u8 op, rnet_u8 slot, rnet_u32 total_size, rnet_u32 payload_crc)
{
    rnet_u32 expect;
    if ((s == NULL) || s->cfg.local_slot != 0 || s->phase != RNET_PHASE_RUNNING)
    {
        return -1;
    }
    if (state_any_active(s) || (s->state_probe_active && s->state_probe_sender && !state_probe_all_replied(s)))
    {
        return -1;
    }
    if (op != RNET_STATE_OP_SAVE && op != RNET_STATE_OP_LOAD && op != RNET_STATE_OP_SRAM &&
        op != RNET_STATE_OP_RB_KF && op != RNET_STATE_OP_BOOT && op != RNET_STATE_OP_MENU)
    {
        return -1;
    }
    if (total_size > RNET_STATE_MAX)
    {
        return -1;
    }
    expect = state_default_receivers(s, op);
    if (expect == 0u)
    {
        return -1; /* nobody to answer */
    }

    state_probe_clear(s);
    s->state_probe_released_valid = 0;
    s->state_probe_active = 1;
    s->state_probe_sender = 1;
    s->state_probe_expect_mask = expect;
    s->state_probe_reply_mask = 0;
    s->state_probe_match_mask = 0;
    s->state_probe_pending = 0;
    s->state_probe_match = 0;
    s->state_probe_op = op;
    s->state_probe_slot = slot;
    s->state_probe_size = total_size;
    s->state_probe_crc = payload_crc;
    s->state_probe_last_tx_ms = 0;
    /* size==0 probes must not stall INPUT: SAVE coord and LOAD ready both need
     * the slower peer to keep admitting for savestate_poll. App-layer LOAD_READY
     * freezes sim until mutual ready + hard_resync; stalling send_input_bundle
     * here starves the late applier's tip runway (spam-load hang).
     * Hash probe (size!=0): stall until agree or transfer. */
    s->state_stall_sim = (total_size != 0) ? 1 : 0;
    state_drive_probe(s);
    return 0;
}

int rnet_session_state_probe_take_reply(RNetSession *s, int *match_out)
{
    if ((s == NULL) || !s->state_probe_active || !s->state_probe_sender || !state_probe_all_replied(s))
    {
        return 0;
    }
    if (match_out)
    {
        *match_out = ((s->state_probe_match_mask & s->state_probe_expect_mask) == s->state_probe_expect_mask) ? 1 : 0;
    }
    return 1;
}

int rnet_session_state_probe_take_reply_from(RNetSession *s, int slot, int *match_out)
{
    if ((s == NULL) || !s->state_probe_active || !s->state_probe_sender || slot < 0 || slot >= RNET_MAX_SLOTS ||
        !(s->state_probe_expect_mask & (1u << slot)) || !(s->state_probe_reply_mask & (1u << slot)))
    {
        return 0;
    }
    if (match_out)
    {
        *match_out = (s->state_probe_match_mask & (1u << slot)) ? 1 : 0;
    }
    return 1;
}

int rnet_session_state_probe_replies(const RNetSession *s, rnet_u32 *expect_mask, rnet_u32 *replied_mask,
                                     rnet_u32 *match_mask)
{
    if ((s == NULL) || !s->state_probe_active || !s->state_probe_sender)
    {
        return 0;
    }
    if (expect_mask)
    {
        *expect_mask = s->state_probe_expect_mask;
    }
    if (replied_mask)
    {
        *replied_mask = s->state_probe_reply_mask & s->state_probe_expect_mask;
    }
    if (match_mask)
    {
        *match_mask = s->state_probe_match_mask & s->state_probe_reply_mask & s->state_probe_expect_mask;
    }
    return 1;
}

int rnet_session_state_probe_pending(const RNetSession *s, rnet_u8 *op_out, rnet_u8 *slot_out, rnet_u32 *size_out,
                                     rnet_u32 *crc_out)
{
    if ((s == NULL) || !s->state_probe_active || !s->state_probe_pending)
    {
        return 0;
    }
    if (op_out)
    {
        *op_out = s->state_probe_op;
    }
    if (slot_out)
    {
        *slot_out = s->state_probe_slot;
    }
    if (size_out)
    {
        *size_out = s->state_probe_size;
    }
    if (crc_out)
    {
        *crc_out = s->state_probe_crc;
    }
    return 1;
}

int rnet_session_state_probe_reply(RNetSession *s, int match)
{
    rnet_u8 buf[64];
    int n;

    if ((s == NULL) || !s->state_probe_active || s->state_probe_sender || !s->state_probe_pending)
    {
        return -1;
    }
    n = rnet_proto_encode_state_probe_reply(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                            s->wire_slot, s->state_probe_op, s->state_probe_slot,
                                            match ? 1u : 0u, s->state_probe_size, s->state_probe_crc);
    if (n <= 0)
    {
        return -1;
    }
    send_raw(s, buf, n);
    s->state_probe_pending = 0;
    s->state_probe_match = match ? 1 : 0;
    if (s->state_probe_size == 0)
    {
        s->state_stall_sim = 0;
        if (s->state_probe_op == RNET_STATE_OP_LOAD || s->state_probe_op == RNET_STATE_OP_BOOT)
        {
            /* Post-load / post-boot ready ACK — done; do not keep probe for retransmit. */
            state_probe_clear(s);
            return 0;
        }
        /* SAVE coord ACK: leave probe active for retransmit replies. */
        return 0;
    }
    if (match)
    {
        if (s->state_probe_op == RNET_STATE_OP_LOAD || s->state_probe_op == RNET_STATE_OP_BOOT)
        {
            /* LOAD/BOOT: the app keeps pumping until its staged restore is
             * applied (its own ready probe follows), so release now. */
            state_probe_clear(s);
        }
        else
        {
            /* SAVE/SRAM/RB_KF: another seat may still miss, and its transfer
             * must start from the same tick. Hold input until the host has
             * heard every seat and sends DONE (or BEGIN on a miss). */
            s->state_probe_parked = 1;
            s->state_probe_last_tx_ms = session_now(s);
        }
    }
    /* Hash miss: keep stall until STATE_BEGIN (or a new probe). */
    return 0;
}

void rnet_session_state_probe_finish(RNetSession *s)
{
    if (s != NULL)
    {
        state_send_probe_done(s);
    }
    state_probe_clear(s);
}

int rnet_session_state_begin(RNetSession *s, rnet_u8 op, rnet_u8 slot, const void *data, size_t size)
{
    rnet_u8 buf[64];
    int n;
    rnet_u32 expect;

    if ((s == NULL) || (data == NULL) || (size == 0) || (size > RNET_STATE_MAX))
    {
        return -1;
    }
    if (s->phase != RNET_PHASE_RUNNING || state_any_active(s) || s->is_observer)
    {
        return -1;
    }
    if (op == RNET_STATE_OP_MEMCARD)
    {
        /* Guest-only upload: the host is the receiver of this op, never its
         * sender (it would be broadcasting to peers that drop it). */
        if (s->cfg.local_slot == 0)
        {
            return -1;
        }
    }
    else if (s->cfg.local_slot != 0)
    {
        return -1;
    }
    else if (op != RNET_STATE_OP_SAVE && op != RNET_STATE_OP_LOAD && op != RNET_STATE_OP_SRAM &&
             op != RNET_STATE_OP_RB_KF && op != RNET_STATE_OP_BOOT && op != RNET_STATE_OP_MENU)
    {
        return -1;
    }
    expect = state_default_receivers(s, op);
    if (expect == 0u)
    {
        return -1; /* nobody to send to */
    }

    /* Drop any open probe — transfer is the authority path after a hash miss. */
    state_probe_clear(s);

    state_tx_clear(s);
    s->tx.buf = (rnet_u8 *)malloc(size);
    if (s->tx.buf == NULL)
    {
        state_recompute_stall(s);
        return -1;
    }
    memcpy(s->tx.buf, data, size);
    s->tx.active = 1;
    s->tx.ready = 0;
    s->state_stall_sim = 1;
    s->tx.op = op;
    s->tx.slot = slot;
    s->tx.expect_mask = expect;
    s->state_next_xfer_id++;
    if (s->state_next_xfer_id == 0)
    {
        s->state_next_xfer_id = 1;
    }
    s->tx.xfer_id = s->state_next_xfer_id;
    if (op == RNET_STATE_OP_MEMCARD)
    {
        /* The receiver compares against the ids of transfers IT finished — its
         * own, small counter. Keep guest uploads out of that space so a fresh
         * upload is never mistaken for a completed host transfer and re-ACKed
         * instead of received.
         *
         * Bits 24..29 carry (seat - 1): the host's STATE_ACK names only the
         * transfer id, and it is broadcast to every guest, so two guests
         * uploading at once with the same serial would each take the host's
         * ACK of the OTHER's upload as its own. Seat 1 gets 0 there, so a
         * two-seat guest's id is exactly what it always was. The serial keeps
         * the low 24 bits; the receiver's stale-BEGIN window compares ids from
         * one sender, whose seat bits never change. */
        s->tx.xfer_id = 0x40000000u | ((((rnet_u32)s->cfg.local_slot - 1u) & 0x3fu) << 24) |
                        (s->state_next_xfer_id & 0x00ffffffu);
    }
    s->tx.total = (rnet_u32)size;
    s->tx.crc = rnet_proto_checksum(s->tx.buf, s->tx.total);
    s->tx.send_cursor = 0;
    s->tx.start_ms = session_now(s);
    state_pacing_reset(s);

    n = rnet_proto_encode_state_begin(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id, s->wire_slot,
                                      s->tx.op, s->tx.slot, s->tx.xfer_id, s->tx.total, s->tx.crc);
    if (n > 0)
    {
        send_raw(s, buf, n);
        s->tx.last_begin_ms = session_now(s);
    }
    state_drive_sender(s);
    return 0;
}

int rnet_session_state_busy(const RNetSession *s)
{
    if (s == NULL)
    {
        return 0;
    }
    if (s->state_probe_active && s->state_probe_sender && !state_probe_all_replied(s))
    {
        return 1;
    }
    if (s->state_probe_active && (s->state_probe_pending || s->state_probe_parked))
    {
        return 1;
    }
    return state_any_in_progress(s);
}

/* The transfer take_ready reports: the outbound one when it is complete,
 * else the ready inbound one from the lowest source seat. -1 = none ready. */
static int state_pick_ready(const RNetSession *s)
{
    int i;
    if (s->tx.active && s->tx.ready && s->tx.buf != NULL)
    {
        return RNET_STATE_TAKEN_TX;
    }
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
    {
        if (s->rx[i].active && s->rx[i].ready && s->rx[i].buf != NULL)
        {
            return i;
        }
    }
    return -1;
}

int rnet_session_state_take_ready_from(RNetSession *s, int *from_out, rnet_u8 *op_out, rnet_u8 *slot_out,
                                       const void **data_out, size_t *size_out)
{
    int pick;
    rnet_u8 op, slot;
    const rnet_u8 *data;
    rnet_u32 total;
    int from;
    if (s == NULL)
    {
        return 0;
    }
    pick = state_pick_ready(s);
    if (pick < 0)
    {
        return 0;
    }
    if (pick == RNET_STATE_TAKEN_TX)
    {
        op = s->tx.op;
        slot = s->tx.slot;
        data = s->tx.buf;
        total = s->tx.total;
        from = (int)s->cfg.local_slot;
    }
    else
    {
        op = s->rx[pick].op;
        slot = s->rx[pick].slot;
        data = s->rx[pick].buf;
        total = s->rx[pick].total;
        from = pick;
    }
    s->state_taken = pick;
    if (from_out)
    {
        *from_out = from;
    }
    if (op_out)
    {
        *op_out = op;
    }
    if (slot_out)
    {
        *slot_out = slot;
    }
    if (data_out)
    {
        *data_out = data;
    }
    if (size_out)
    {
        *size_out = total;
    }
    return 1;
}

int rnet_session_state_take_ready(RNetSession *s, rnet_u8 *op_out, rnet_u8 *slot_out, const void **data_out,
                                  size_t *size_out)
{
    return rnet_session_state_take_ready_from(s, NULL, op_out, slot_out, data_out, size_out);
}

int rnet_session_state_drop_peer(RNetSession *s, int slot)
{
    int changed = 0;
    if (s == NULL || slot < 0 || slot >= RNET_MAX_SLOTS)
    {
        return 0;
    }
    if (s->tx.active && (s->tx.expect_mask & (1u << slot)))
    {
        s->tx.expect_mask &= ~(1u << slot);
        changed = 1;
        state_tx_mark_ready_if_complete(s);
    }
    if (s->state_probe_active && s->state_probe_sender && (s->state_probe_expect_mask & (1u << slot)))
    {
        s->state_probe_expect_mask &= ~(1u << slot);
        changed = 1;
    }
    return changed;
}

int rnet_session_state_progress(const RNetSession *s, int slot, rnet_u32 *acked_out, rnet_u32 *total_out)
{
    if (s == NULL || !s->tx.active || slot < 0 || slot >= RNET_MAX_SLOTS || !(s->tx.expect_mask & (1u << slot)))
    {
        return 0;
    }
    if (acked_out)
    {
        *acked_out = s->tx.ack[slot];
    }
    if (total_out)
    {
        *total_out = s->tx.total;
    }
    return 1;
}

rnet_u32 rnet_session_state_pending_receivers(const RNetSession *s)
{
    if (s == NULL || !s->tx.active)
    {
        return 0u;
    }
    return s->tx.expect_mask & ~state_tx_done_mask(s);
}

rnet_u32 rnet_session_state_inbound_mask(const RNetSession *s)
{
    rnet_u32 mask = 0;
    int i;
    if (s == NULL)
    {
        return 0u;
    }
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
    {
        if (s->rx[i].active)
        {
            mask |= 1u << i;
        }
    }
    return mask;
}

void rnet_session_hard_resync(RNetSession *s)
{
    rnet_u8 i;
    if (s == NULL)
    {
        return;
    }
    rnet_ring_clear(&s->local_ring);
    /* Clear remotes too: leftover tip rows from a prior post-load epoch are
     * first-wins and can let one peer admit on stale wire=D inputs. Both peers
     * re-prime after mutual ready and wait for a fresh tip exchange. */
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
    {
        rnet_ring_clear(&s->remote_rings[i]);
    }
    memset(s->published_valid, 0, sizeof(s->published_valid));
    memset(s->peer_history_valid, 0, sizeof(s->peer_history_valid));
    memset(s->confirm_last_sent_ms, 0, sizeof(s->confirm_last_sent_ms));
    s->input_desync = 0;
    s->desync_tick = 0;
    s->desync_local_hash = 0;
    s->desync_remote_hash = 0;
    s->highest_remote_ack = 0;
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
        s->remote_contiguous_ack[i] = 0xffffffffu;
    memset(s->peer_ack_seen, 0, sizeof(s->peer_ack_seen));
    s->last_input_tip_valid = 0;
    /* Peers may have applied a load on different sim ticks; restart together. */
    s->sim_tick = 0;
    /* Invalidate in-flight INPUT/CONFIRM from the previous era (same low ticks
     * would otherwise first-wins into this window — spam rematch + stick mash). */
    s->input_epoch = (rnet_u16)(s->input_epoch + 1u);
    s->rb_fc_q_head = s->rb_fc_q_tail = s->rb_fc_q_count = 0;
    s->rb_sync_head = s->rb_sync_tail = s->rb_sync_count = 0;
    s->rb_seal_head = s->rb_seal_tail = s->rb_seal_count = 0;
    s->rb_base_head = s->rb_base_tail = s->rb_base_count = 0;
    s->rb_post_head = s->rb_post_tail = s->rb_post_count = 0;
    s->rb_resolved_head = s->rb_resolved_tail = s->rb_resolved_count = 0;
    s->rb_last_from = -1;

    /* Keep suppress until prime_delay_inputs — avoids emitting an empty tip. */
    s->input_send_suppress = 1;
    s->delay_pending = 0;
}

void rnet_session_set_input_send_suppress(RNetSession *s, int suppress)
{
    if (s == NULL)
    {
        return;
    }
    s->input_send_suppress = suppress ? 1 : 0;
}

void rnet_session_prime_delay_inputs(RNetSession *s, const rnet_u8 *bytes, rnet_u16 size)
{
    rnet_u32 tip;
    rnet_u32 t;
    if (s == NULL || bytes == NULL || size == 0 || size > RNET_INPUT_MAX)
    {
        return;
    }
    if (s->phase != RNET_PHASE_RUNNING)
    {
        return;
    }
    tip = rnet_wire_tick_from_sim(s->sim_tick, s->delay);
    for (t = s->sim_tick; t < tip; ++t)
    {
        RNetInputSample sample;
        memset(&sample, 0, sizeof(sample));
        sample.tick = t;
        sample.size = size;
        memcpy(sample.bytes, bytes, size);
        sample.valid = 1;
        rnet_ring_store(&s->local_ring, &sample);
    }
    s->last_input_ms = 0;
    s->last_input_tip_valid = 0;
    s->input_send_suppress = 0;
    /* Prime must emit even if a LOAD ready probe still has state_stall_sim
     * (host commits sync before probe_finish). */
    {
        int saved_stall = s->state_stall_sim;
        s->state_stall_sim = 0;
        send_input_bundle(s);
        s->state_stall_sim = saved_stall;
    }
}

/* Finish (ready) or abort (not ready) one inbound transfer. */
static void state_finish_rx(RNetSession *s, int src)
{
    if (s->rx[src].active && s->rx[src].ready)
    {
        s->state_received[src].finished = 1;
    }
    state_rx_clear(s, src);
}

void rnet_session_state_finish_from(RNetSession *s, int from_slot, int hard_resync)
{
    int i;
    if (s == NULL)
    {
        return;
    }
    if (from_slot == RNET_STATE_FROM_ALL)
    {
        for (i = 0; i < RNET_MAX_SLOTS; ++i)
        {
            state_finish_rx(s, i);
        }
        state_tx_clear(s);
    }
    else if (!s->is_observer && from_slot == (int)s->cfg.local_slot)
    {
        state_tx_clear(s);
    }
    else if (from_slot >= 0 && from_slot < RNET_MAX_SLOTS)
    {
        state_finish_rx(s, from_slot);
    }
    if (hard_resync)
    {
        rnet_session_hard_resync(s);
    }
    state_recompute_stall(s);
}

void rnet_session_state_finish(RNetSession *s, int hard_resync)
{
    int target;
    if (s == NULL)
    {
        return;
    }
    /* The transfer the last take_ready reported, if it is still open; else
     * whichever take_ready would report now; else (nothing ready) abort
     * everything -- with one transfer open that is exactly the old call. */
    target = s->state_taken;
    if (target == RNET_STATE_TAKEN_TX ? !s->tx.active : (target < 0 || !s->rx[target].active))
    {
        target = state_pick_ready(s);
    }
    if (target < 0)
    {
        rnet_session_state_finish_from(s, RNET_STATE_FROM_ALL, hard_resync);
        return;
    }
    if (target == RNET_STATE_TAKEN_TX)
    {
        state_tx_clear(s);
    }
    else
    {
        state_finish_rx(s, target);
    }
    s->state_taken = -1;
    if (hard_resync)
    {
        rnet_session_hard_resync(s);
    }
    state_recompute_stall(s);
}

int rnet_session_send_rb_frame_commit(RNetSession *s, rnet_u32 through_tick,
                                      rnet_u32 state_hash)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    int n;
    if (s == NULL || s->phase != RNET_PHASE_RUNNING)
    {
        return -1;
    }
    n = rnet_proto_encode_rb_frame_commit(buf, sizeof(buf), s->cfg.protocol_magic,
                                          s->cfg.session_id, s->wire_slot,
                                          through_tick, state_hash);
    if (n <= 0)
    {
        return -1;
    }
    send_raw(s, buf, n);
    return 0;
}

int rnet_session_remote_tip(const RNetSession *s, int slot, rnet_u32 *tip)
{
    if (s == NULL || slot < 0 || slot >= (int)s->cfg.slot_count ||
        slot == (int)s->cfg.local_slot || tip == NULL)
    {
        return 0;
    }
    if (!rnet_config_slot_occupied(&s->cfg, (rnet_u8)slot))
    {
        return 0;
    }
    *tip = rnet_ring_highest_valid(&s->remote_rings[slot]);
    return 1;
}

void rnet_session_set_rb_peer_slot(RNetSession *s, int slot)
{
    if (s == NULL)
    {
        return;
    }
    s->rb_peer_slot = slot;
}

int rnet_session_take_rb_frame_commit(RNetSession *s, rnet_u32 *through_tick,
                                      rnet_u32 *state_hash)
{
    if (s == NULL || s->rb_fc_q_count <= 0)
    {
        return 0;
    }
    if (through_tick)
    {
        *through_tick = s->rb_fc_tick[s->rb_fc_q_tail];
    }
    if (state_hash)
    {
        *state_hash = s->rb_fc_hash[s->rb_fc_q_tail];
    }
    s->rb_last_from = s->rb_fc_from[s->rb_fc_q_tail];
    s->rb_fc_q_tail = (s->rb_fc_q_tail + 1) % RNET_RB_FC_QUEUE;
    s->rb_fc_q_count--;
    return 1;
}

int rnet_session_prepare_local_tip(RNetSession *s, rnet_u32 sim_tick)
{
    rnet_u32 sample_wire;
    RNetInputSample local_future;

    if (s == NULL || s->phase != RNET_PHASE_RUNNING)
    {
        return 0;
    }
    if (sim_tick != s->sim_tick)
    {
        return 0;
    }
    sample_wire = rnet_wire_tick_from_sim(sim_tick, s->delay);
    /* Fill EVERY missing wire in [sim .. sim+D], not just the tip. In steady
     * state only sample_wire is missing (one sample/admit, unchanged). After
     * a mid-session DELAY_SYNC increase the production tip jumps from
     * sim-1+D_old to sim+D_new, leaving [sim+D_old .. sim+D_new-1] unproduced
     * — without the back-fill the peer must invent across the gap. A
     * consumer at wire = sim (real-delay rollback, lockstep play) also
     * relies on the whole prefix existing. Missing rows repeat the current
     * sample (hold-current), matching prime_delay_inputs semantics. */
    {
        rnet_u32 w;
        int have_sample = 0;
        for (w = sim_tick; w <= sample_wire; ++w)
        {
            RNetInputSample existing;
            if (rnet_ring_get(&s->local_ring, w, &existing))
            {
                continue;
            }
            if (!have_sample)
            {
                memset(&local_future, 0, sizeof(local_future));
                if (s->host.sample_local)
                {
                    s->host.sample_local(sim_tick, &local_future, s->host.ctx);
                }
                local_future.valid = 1;
                if (local_future.size > RNET_INPUT_MAX)
                {
                    local_future.size = RNET_INPUT_MAX;
                }
                have_sample = 1;
            }
            local_future.tick = w;
            rnet_ring_store(&s->local_ring, &local_future);
        }
    }
    send_input_bundle(s);
    return 1;
}

rnet_u32 rnet_session_remote_arrival_age_ms(const RNetSession *s, int slot,
                                            rnet_u32 wire_tick)
{
    rnet_u32 idx;
    rnet_u64 now;
    if (s == NULL || slot < 0 || slot >= (int)s->cfg.slot_count ||
        slot == (int)s->cfg.local_slot)
    {
        return 0xffffffffu;
    }
    idx = wire_tick % RNET_HISTORY_LENGTH;
    if (s->remote_arr_ms[slot][idx] == 0ull ||
        s->remote_arr_tick[slot][idx] != wire_tick)
    {
        return 0xffffffffu;
    }
    now = session_now((RNetSession *)s);
    if (now <= s->remote_arr_ms[slot][idx])
    {
        return 0u;
    }
    return (rnet_u32)(now - s->remote_arr_ms[slot][idx]);
}

int rnet_session_peek_input(const RNetSession *s, int slot, rnet_u32 wire_tick,
                            RNetInputSample *out)
{
    if (s == NULL || out == NULL || slot < 0 || slot >= (int)s->cfg.slot_count)
    {
        return 0;
    }
    if (slot == (int)s->cfg.local_slot)
    {
        return rnet_ring_get(&s->local_ring, wire_tick, out);
    }
    return rnet_ring_get(&s->remote_rings[slot], wire_tick, out);
}

int rnet_session_peek_remote_input(const RNetSession *s, int slot, rnet_u32 wire_tick,
                                   RNetInputSample *out)
{
    if (s == NULL || slot == (int)s->cfg.local_slot)
    {
        return 0;
    }
    if (slot < 0 || slot >= (int)s->cfg.slot_count)
    {
        return 0;
    }
    if (!rnet_config_slot_occupied(&s->cfg, (rnet_u8)slot))
    {
        if (out)
        {
            memset(out, 0, sizeof(*out));
            out->tick = wire_tick;
            out->valid = 1;
        }
        return 1;
    }
    return rnet_session_peek_input(s, slot, wire_tick, out);
}

void rnet_session_set_sim_tick(RNetSession *s, rnet_u32 sim_tick)
{
    if (s == NULL)
    {
        return;
    }
    s->sim_tick = sim_tick;
    apply_pending_delay(s);
}

void rnet_session_clear_remote_inputs(RNetSession *s)
{
    rnet_u8 i;
    if (s == NULL)
    {
        return;
    }
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
    {
        if (i == s->cfg.local_slot)
            continue;
        rnet_ring_clear(&s->remote_rings[i]);
    }
    s->highest_remote_ack = 0;
    for (i = 0; i < RNET_MAX_SLOTS; ++i)
        s->remote_contiguous_ack[i] = 0xffffffffu;
    memset(s->peer_ack_seen, 0, sizeof(s->peer_ack_seen));
}

int rnet_session_send_rb_sync(RNetSession *s, rnet_u32 epoch_id, rnet_u32 mismatch_tick,
                              rnet_u32 load_tick, rnet_u32 target_tick,
                              rnet_u8 corrected_slot, rnet_u8 op, rnet_u8 flags)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    int n;
    if (s == NULL || s->phase != RNET_PHASE_RUNNING)
        return -1;
    n = rnet_proto_encode_rb_sync(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                  s->wire_slot, epoch_id, mismatch_tick, load_tick,
                                  target_tick, corrected_slot, op, flags);
    if (n <= 0)
        return -1;
    send_raw(s, buf, n);
    return 0;
}

int rnet_session_take_rb_sync(RNetSession *s, rnet_u32 *epoch_id, rnet_u32 *mismatch_tick,
                              rnet_u32 *load_tick, rnet_u32 *target_tick,
                              rnet_u8 *corrected_slot, rnet_u8 *op, rnet_u8 *flags)
{
    if (s == NULL || s->rb_sync_count <= 0)
        return 0;
    if (epoch_id)
        *epoch_id = s->rb_sync_q[s->rb_sync_tail].epoch_id;
    if (mismatch_tick)
        *mismatch_tick = s->rb_sync_q[s->rb_sync_tail].mismatch_tick;
    if (load_tick)
        *load_tick = s->rb_sync_q[s->rb_sync_tail].load_tick;
    if (target_tick)
        *target_tick = s->rb_sync_q[s->rb_sync_tail].target_tick;
    if (corrected_slot)
        *corrected_slot = s->rb_sync_q[s->rb_sync_tail].corrected_slot;
    if (op)
        *op = s->rb_sync_q[s->rb_sync_tail].initiator;
    if (flags)
        *flags = s->rb_sync_q[s->rb_sync_tail].flags;
    s->rb_last_from = s->rb_sync_q[s->rb_sync_tail].from;
    s->rb_sync_tail = (s->rb_sync_tail + 1) % RNET_RB_CTRL_QUEUE;
    s->rb_sync_count--;
    return 1;
}

int rnet_session_send_rb_seal_rows(RNetSession *s, rnet_u32 epoch_id, rnet_u32 mismatch_tick,
                                   rnet_u32 target_tick, rnet_u8 slot, rnet_u32 row_begin,
                                   const RNetRbFrame *rows, rnet_u16 row_count)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    RNetRbWireFrame wire[RNET_RB_SEAL_ROWS_CHUNK_MAX];
    rnet_u16 i, n;
    int enc;
    if (s == NULL || s->phase != RNET_PHASE_RUNNING || rows == NULL)
        return -1;
    n = row_count;
    if (n > RNET_RB_SEAL_ROWS_CHUNK_MAX)
        n = RNET_RB_SEAL_ROWS_CHUNK_MAX;
    for (i = 0; i < n; ++i)
    {
        wire[i].buttons = rows[i].buttons;
        wire[i].stick_x = (rnet_s8)rows[i].stick_x;
        wire[i].stick_y = (rnet_s8)rows[i].stick_y;
        wire[i].source = rows[i].analog;
        wire[i].is_predicted = rows[i].is_predicted;
        wire[i].is_valid = rows[i].is_valid;
        wire[i].rx = rows[i].rx;
        wire[i].ry = rows[i].ry;
    }
    enc = rnet_proto_encode_rb_seal_rows(buf, sizeof(buf), s->cfg.protocol_magic,
                                         s->cfg.session_id, s->wire_slot, epoch_id,
                                         mismatch_tick, target_tick, slot, row_begin, wire, n);
    if (enc <= 0)
        return -1;
    send_raw(s, buf, enc);
    return 0;
}

int rnet_session_take_rb_seal_rows(RNetSession *s, rnet_u32 *epoch_id, rnet_u32 *mismatch_tick,
                                   rnet_u32 *target_tick, rnet_u8 *slot, rnet_u32 *row_begin,
                                   RNetRbFrame *rows, rnet_u16 *row_count)
{
    rnet_u16 i, n;
    if (s == NULL || s->rb_seal_count <= 0)
        return 0;
    if (epoch_id)
        *epoch_id = s->rb_seal_q[s->rb_seal_tail].epoch_id;
    if (mismatch_tick)
        *mismatch_tick = s->rb_seal_q[s->rb_seal_tail].mismatch_tick;
    if (target_tick)
        *target_tick = s->rb_seal_q[s->rb_seal_tail].target_tick;
    if (slot)
        *slot = s->rb_seal_q[s->rb_seal_tail].slot;
    if (row_begin)
        *row_begin = s->rb_seal_q[s->rb_seal_tail].row_begin;
    n = s->rb_seal_q[s->rb_seal_tail].row_count;
    if (rows && n > 0)
    {
        for (i = 0; i < n; ++i)
        {
            rows[i].tick = s->rb_seal_q[s->rb_seal_tail].row_begin + i;
            rows[i].buttons = s->rb_seal_q[s->rb_seal_tail].rows[i].buttons;
            rows[i].stick_x = (int8_t)s->rb_seal_q[s->rb_seal_tail].rows[i].stick_x;
            rows[i].stick_y = (int8_t)s->rb_seal_q[s->rb_seal_tail].rows[i].stick_y;
            rows[i].analog = s->rb_seal_q[s->rb_seal_tail].rows[i].source;
            rows[i].is_predicted = s->rb_seal_q[s->rb_seal_tail].rows[i].is_predicted;
            rows[i].is_valid = s->rb_seal_q[s->rb_seal_tail].rows[i].is_valid;
            rows[i].rx = s->rb_seal_q[s->rb_seal_tail].rows[i].rx;
            rows[i].ry = s->rb_seal_q[s->rb_seal_tail].rows[i].ry;
        }
    }
    if (row_count)
        *row_count = n;
    s->rb_last_from = s->rb_seal_q[s->rb_seal_tail].from;
    s->rb_seal_tail = (s->rb_seal_tail + 1) % RNET_RB_CTRL_QUEUE;
    s->rb_seal_count--;
    return 1;
}

int rnet_session_send_rb_baseline(RNetSession *s, rnet_u32 epoch_id, rnet_u32 load_tick,
                                  rnet_u32 digest_master, rnet_u32 digest_a, rnet_u32 digest_b,
                                  rnet_u32 digest_c)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    int n;
    if (s == NULL || s->phase != RNET_PHASE_RUNNING)
        return -1;
    n = rnet_proto_encode_rb_baseline(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                      s->wire_slot, epoch_id, load_tick, digest_master,
                                      digest_a, digest_b, digest_c);
    if (n <= 0)
        return -1;
    send_raw(s, buf, n);
    return 0;
}

int rnet_session_take_rb_baseline(RNetSession *s, rnet_u32 *epoch_id, rnet_u32 *load_tick,
                                  rnet_u32 *digest_master, rnet_u32 *digest_a, rnet_u32 *digest_b,
                                  rnet_u32 *digest_c)
{
    if (s == NULL || s->rb_base_count <= 0)
        return 0;
    if (epoch_id)
        *epoch_id = s->rb_base_q[s->rb_base_tail].epoch_id;
    if (load_tick)
        *load_tick = s->rb_base_q[s->rb_base_tail].load_tick;
    if (digest_master)
        *digest_master = s->rb_base_q[s->rb_base_tail].digest_master;
    if (digest_a)
        *digest_a = s->rb_base_q[s->rb_base_tail].digest_a;
    if (digest_b)
        *digest_b = s->rb_base_q[s->rb_base_tail].digest_b;
    if (digest_c)
        *digest_c = s->rb_base_q[s->rb_base_tail].digest_c;
    s->rb_last_from = s->rb_base_q[s->rb_base_tail].from;
    s->rb_base_tail = (s->rb_base_tail + 1) % RNET_RB_CTRL_QUEUE;
    s->rb_base_count--;
    return 1;
}

int rnet_session_send_rb_post(RNetSession *s, rnet_u32 epoch_id, rnet_u32 target_tick,
                              rnet_u32 digest_master, rnet_u32 input_digest, rnet_u8 match)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    int n;
    if (s == NULL || s->phase != RNET_PHASE_RUNNING)
        return -1;
    n = rnet_proto_encode_rb_post(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                  s->wire_slot, epoch_id, target_tick, digest_master,
                                  input_digest, match);
    if (n <= 0)
        return -1;
    send_raw(s, buf, n);
    return 0;
}

int rnet_session_take_rb_post(RNetSession *s, rnet_u32 *epoch_id, rnet_u32 *target_tick,
                              rnet_u32 *digest_master, rnet_u32 *input_digest, rnet_u8 *match)
{
    if (s == NULL || s->rb_post_count <= 0)
        return 0;
    if (epoch_id)
        *epoch_id = s->rb_post_q[s->rb_post_tail].epoch_id;
    if (target_tick)
        *target_tick = s->rb_post_q[s->rb_post_tail].target_tick;
    if (digest_master)
        *digest_master = s->rb_post_q[s->rb_post_tail].digest_master;
    if (input_digest)
        *input_digest = s->rb_post_q[s->rb_post_tail].input_digest;
    if (match)
        *match = s->rb_post_q[s->rb_post_tail].match;
    s->rb_last_from = s->rb_post_q[s->rb_post_tail].from;
    s->rb_post_tail = (s->rb_post_tail + 1) % RNET_RB_CTRL_QUEUE;
    s->rb_post_count--;
    return 1;
}

int rnet_session_rb_last_take_from(const RNetSession *s)
{
    return (s != NULL) ? s->rb_last_from : -1;
}

rnet_u32 rnet_session_rb_ctrl_dropped(const RNetSession *s)
{
    return (s != NULL) ? s->rb_ctrl_dropped : 0u;
}

int rnet_session_send_sio_multi_xfer(RNetSession *s, rnet_u8 unit_id, rnet_u32 seq,
                                     rnet_u16 send, rnet_u16 confirm_pad)
{
    rnet_u8 buf[64];
    int n;
    if (s == NULL || s->transport.mode == RNET_TRANSPORT_NONE)
        return -1;
    /* Allow during READY/RUNNING so Cable Club can start as soon as UDP is up. */
    if (s->phase != RNET_PHASE_RUNNING && s->phase != RNET_PHASE_READY)
        return -1;
    n = rnet_proto_encode_sio_multi_xfer(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                         s->wire_slot, unit_id, seq, send, confirm_pad);
    if (n <= 0)
        return -1;
    /* Small redundancy: Multi barrier cannot hide behind INPUT FEC. */
    send_raw(s, buf, n);
    send_raw(s, buf, n);
    return 0;
}

int rnet_session_poll_sio_multi_xfer(RNetSession *s, rnet_u8 *unit_id, rnet_u32 *seq,
                                     rnet_u16 *send, rnet_u16 *confirm_pad)
{
    if (s == NULL || s->sio_xfer_count <= 0)
        return 0;
    if (unit_id)
        *unit_id = s->sio_xfer_q[s->sio_xfer_tail].unit_id;
    if (seq)
        *seq = s->sio_xfer_q[s->sio_xfer_tail].seq;
    if (send)
        *send = s->sio_xfer_q[s->sio_xfer_tail].send;
    if (confirm_pad)
        *confirm_pad = s->sio_xfer_q[s->sio_xfer_tail].confirm_pad;
    s->sio_xfer_tail = (s->sio_xfer_tail + 1) % RNET_SIO_XFER_QUEUE;
    s->sio_xfer_count--;
    return 1;
}

int rnet_session_peer_wire_version_mismatch(const RNetSession *s)
{
    if (s == NULL || s->peer_wire_version_mismatch == 0u)
        return 0;
    return s->peer_wire_version_mismatch == 0xFFu ? -1 : (int)s->peer_wire_version_mismatch;
}

int rnet_session_send_modset(RNetSession *s, const char *text)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    int n;
    if ((s == NULL) || (text == NULL) || (s->phase != RNET_PHASE_RUNNING))
    {
        return -1;
    }
    n = rnet_proto_encode_modset(buf, sizeof(buf), s->cfg.protocol_magic,
                                 s->cfg.session_id, (rnet_u8)s->wire_slot,
                                 text);
    if (n <= 0)
    {
        return -1;
    }
    rnet_transport_send(&s->transport, buf, (size_t)n);
    return 0;
}

int rnet_session_take_modset(RNetSession *s, char *out, rnet_u32 cap)
{
    if ((s == NULL) || (out == NULL) || (cap == 0u) || (s->modset_pending == 0u))
    {
        return 0;
    }
    s->modset_pending = 0u;
    if ((rnet_u32)strlen(s->modset_text) >= cap)
    {
        return 0; /* caller's buffer cannot hold it; never hand back a prefix */
    }
    memcpy(out, s->modset_text, strlen(s->modset_text) + 1u);
    return 1;
}

int rnet_session_send_modset_ack(RNetSession *s, rnet_u8 status,
                                 const char *reason)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    int n;
    if ((s == NULL) || (s->phase != RNET_PHASE_RUNNING))
    {
        return -1;
    }
    n = rnet_proto_encode_modset_ack(buf, sizeof(buf), s->cfg.protocol_magic,
                                     s->cfg.session_id,
                                     (rnet_u8)s->wire_slot, status,
                                     reason);
    if (n <= 0)
    {
        return -1;
    }
    rnet_transport_send(&s->transport, buf, (size_t)n);
    return 0;
}

int rnet_session_take_modset_ack(RNetSession *s, rnet_u8 *status, char *reason,
                                 rnet_u32 cap)
{
    int from;
    if ((s == NULL) || (s->modset_ack_pending == 0u))
    {
        return 0;
    }
    /* Lowest seat first; each seat's answer is taken once. The sender is
     * rnet_session_rb_last_take_from(), as for the rb_* takes. */
    for (from = 0; from < RNET_MAX_SLOTS; ++from)
    {
        if (s->modset_ack_pending & (1u << from))
        {
            break;
        }
    }
    s->modset_ack_pending &= ~(1u << from);
    s->rb_last_from = from;
    if (status != NULL)
    {
        *status = s->modset_ack_status[from];
    }
    if ((reason != NULL) && (cap > 0u))
    {
        rnet_u32 n = (rnet_u32)strlen(s->modset_ack_reason[from]);
        if (n >= cap)
        {
            n = cap - 1u;
        }
        memcpy(reason, s->modset_ack_reason[from], n);
        reason[n] = '\0';
    }
    return 1;
}

int rnet_session_send_rb_resolved(RNetSession *s, rnet_u32 resolved_through)
{
    rnet_u8 buf[RNET_MAX_PACKET];
    int n;
    if (s == NULL || s->phase != RNET_PHASE_RUNNING)
        return -1;
    n = rnet_proto_encode_rb_resolved(buf, sizeof(buf), s->cfg.protocol_magic, s->cfg.session_id,
                                      s->wire_slot, resolved_through);
    if (n <= 0)
        return -1;
    send_raw(s, buf, n);
    return 0;
}

int rnet_session_take_rb_resolved(RNetSession *s, rnet_u32 *resolved_through)
{
    if (s == NULL || s->rb_resolved_count <= 0)
        return 0;
    if (resolved_through)
        *resolved_through = s->rb_resolved_q[s->rb_resolved_tail].through;
    s->rb_last_from = s->rb_resolved_q[s->rb_resolved_tail].from;
    s->rb_resolved_tail = (s->rb_resolved_tail + 1) % RNET_RB_CTRL_QUEUE;
    s->rb_resolved_count--;
    return 1;
}
