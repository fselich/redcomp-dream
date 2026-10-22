# Fixed execution profiles at session startup

`recomp_net/startup_profiles.h` provides an optional barrier before execution.
The caller advertises actual CPU features, available backends and a mask of
qualified profiles in a versioned qualification namespace. Bit 0 is always
the functioning portable fallback; the highest common qualified bit wins.
Architecture is diagnostic. Different architectures may advertise the same
profile only after their implementations have qualified equivalent semantics.
OS versions and successful compilation alone establish no compatibility.

Call `rnet_session_profiles_begin` once on a fresh session before opening its
transport. `pump` exchanges copied offers and matching acknowledgements from
every occupied seat, including sparse multi-seat rooms. Status 1 supplies a
profile index and a canonical capability-transcript fingerprint. The caller
must wait for it before core init/guest execution and append both to its
existing compatibility identity. Existing build/content gates stay in force.
The library also blocks delay-sync admission while the barrier is pending.

The receive path consumes reserved RB_SYNC operations 7 (offer) and 8 (ACK)
outside the rollback episode queues. Offer fields are qualification hash in
epoch, CPU bits in mismatch, backends in load, qualified profile mask in target,
architecture in corrected_slot. ACK fields are the same qualification, chosen
bit index in mismatch, transcript fingerprint in load, received-ACK seat mask
in target, corrected_slot zero. Flags are zero. The transcript uses ordered
little-endian u32 words: qualification, seat count, occupied mask, then each
occupied seat index, CPU, backends, profiles, architecture; selected bit index
last. Hashing is the existing FNV-1a fingerprint convention, not authentication.

Identical offers/ACKs retransmit until every peer has acknowledged receipt.
These retries can outlive one peer's local completion; they deliver the same
startup decision and never probe capabilities or select another profile.
Changed offers, a different qualification namespace or contradictory ACK
refuse the agreement. Rollback/hard resync do not clear it. An older peer that
does not implement this opt-in barrier cannot release it; callers time out
and refuse instead of silently starting with an unagreed fallback. Sessions
that do not opt in retain their existing wire behavior. Accordingly these
reserved additive operations do not change the existing wire version.

Module ABI minor 1 adds the three public functions; older loaders can still
load the newer module, while a newer loader refuses a module lacking them.
Windows static and Linux static/module consumers use the same portable C
barrier. `startup_profiles_test` exercises actual UDP pairs, three seats,
sparse four-seat rooms, partial/common optimizations, equivalent profiles on
different synthetic architectures, missing legacy support, namespace refusal,
changed offers and contradictory ACKs. The second CTest runs with deterministic
30% packet loss and latency/jitter. These are transport tests, not native CPU
qualification or an internet/NAT playtest.
