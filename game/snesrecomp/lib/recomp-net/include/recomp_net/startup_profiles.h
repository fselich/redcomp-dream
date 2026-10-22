#ifndef RECOMP_NET_STARTUP_PROFILES_H
#define RECOMP_NET_STARTUP_PROFILES_H

#include "recomp_net/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RNetSession RNetSession;

/* Meanings belong to the caller's versioned qualification namespace. CPU and
 * backend bits report actual availability; profiles advertise only validated
 * implementations. Bit 0 MUST be the functioning portable profile. Higher
 * numbered bits are preferred. Equal namespace/profile means equal simulation
 * semantics, not equal OS, instruction set, or a proof from a version string.
 * Existing build/content compatibility checks remain mandatory. */
typedef struct RNetProfileCaps {
    rnet_u32 cpu_features;
    rnet_u32 backends;
    rnet_u32 profiles;
    rnet_u32 architecture; /* 0..255, diagnostic; never selects a profile */
} RNetProfileCaps;

/* Once per fresh seated session, BEFORE starting its transport. Copies caps.
 * Every occupied seat must opt in with the same qualification namespace hash.
 * No legacy timeout downgrade: an older peer cannot acknowledge this barrier.
 * Returns 0 on success, -1 for invalid/repeated/late setup. */
int rnet_session_profiles_begin(RNetSession *s, rnet_u32 qualification,
                                const RNetProfileCaps *caps);

/* pump() exchanges the immutable offers and acknowledgements. Status is 0
 * pending, 1 agreed, -1 refused. Outputs are the chosen bit INDEX and a
 * canonical per-seat capability transcript fingerprint to fold into existing
 * session identity. No frame may run before 1. Selection never changes;
 * retries only deliver the SAME startup agreement, even after local completion.
 * No CPU probing, renegotiation, HLE/LLE handoff, or live backend switching. */
int rnet_session_profiles_status(const RNetSession *s, rnet_u32 *profile,
                                 rnet_u32 *transcript);
/* 1 and a copy when an occupied seat's immutable offer is available. */
int rnet_session_profiles_peer(const RNetSession *s, int seat, RNetProfileCaps *out);

#ifdef __cplusplus
}
#endif
#endif
