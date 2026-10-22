/*
 * The lobby protocol client's codecs and state machine, driven directly.
 *
 * Moved here from snesrecomp tests/netplay/lobby_mod_plan_test.c with the
 * client it tests (the cases up to case_launch_transport_survives_a_lobby_update
 * are that file's, renamed); the cases after it cover what the lift added --
 * the engine caps codec, input_prediction, the configured seat ceiling and
 * environment alias, escaping of every free-text field, and the launch path.
 *
 * It includes the client's translation unit so it tests the REAL codec and
 * state machine rather than a copy, and it is deliberately NOT linked against
 * recomp_net: every network entry point the client could reach is replaced
 * below by a trap that aborts, so a case that starts depending on a socket
 * fails loudly instead of passing against a fake. Built by CMake as
 * lobby_client_test (tests/CMakeLists section in the top-level file).
 *
 * The plan is what the LOBBY SERVER reads to decide whether a joiner may sit
 * down. Its required_mod_rows() takes match_caps.mods as a JSON ARRAY of
 * objects and matches each {id, ver} against the joiner's offer; anything it
 * cannot read as an array yields an empty requirement list and seats everyone.
 * So the encoding has to be an array, and it has to survive the trip exactly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/lobby/rnet_lobby_client.c"

/* Link-time double for the one socket call the lobby client's other paths
 * reach for. None of the cases below open a socket, so this must never run --
 * it aborts rather than returning a plausible port, so a future case that
 * starts depending on real networking fails loudly instead of quietly passing
 * against a fake. */
int rnet_udp_find_free_port(int preferred, int span)
{
    (void)preferred; (void)span;
    fprintf(stderr, "lobby_client_test: rnet_udp_find_free_port was called; "
                    "this test does no networking\n");
    abort();
}

/* The lobby client now drives an ICE transfer agent. None of the cases below
 * open one, so every entry point aborts rather than returning a plausible
 * value -- linking the real agent would drag libjuice into a test about JSON,
 * and a silent stub would let a future case "pass" against a fake network. */
#define XFER_TRAP(name) \
    do { fprintf(stderr, "lobby_client_test: %s called; this test does no " \
                         "networking\n", name); abort(); } while (0)

int  rnet_ice_xfer_open(RNetIceXfer **o, const RNetIceConfig *c,
                        RNetIceXferSignalEmitFn e, void *u)
{ (void)o; (void)c; (void)e; (void)u; XFER_TRAP("rnet_ice_xfer_open"); }
void rnet_ice_xfer_close(RNetIceXfer **x) { (void)x; XFER_TRAP("close"); }
void rnet_ice_xfer_push_signal(RNetIceXfer *x, const RNetSignal *m)
{ (void)x; (void)m; XFER_TRAP("push_signal"); }
void rnet_ice_xfer_pump(RNetIceXfer *x) { (void)x; XFER_TRAP("pump"); }
int  rnet_ice_xfer_queue_blob(RNetIceXfer *x, uint8_t *d, size_t l)
{ (void)x; (void)d; (void)l; XFER_TRAP("queue_blob"); }
int  rnet_ice_xfer_send_idle(const RNetIceXfer *x) { (void)x; XFER_TRAP("send_idle"); }
int  rnet_ice_xfer_take_blob(RNetIceXfer *x, uint8_t **d, size_t *l)
{ (void)x; (void)d; (void)l; XFER_TRAP("take_blob"); }
int  rnet_ice_xfer_progress(const RNetIceXfer *x) { (void)x; XFER_TRAP("progress"); }
int  rnet_ice_xfer_failed(const RNetIceXfer *x, char *e, size_t c)
{ (void)x; (void)e; (void)c; XFER_TRAP("failed"); }
void rnet_ice_xfer_path(const RNetIceXfer *x, char *o, size_t c)
{ (void)x; (void)o; (void)c; XFER_TRAP("path"); }
RNetIceState rnet_ice_xfer_state(const RNetIceXfer *x) { (void)x; XFER_TRAP("state"); }
const char *rnet_ice_state_name(RNetIceState st) { (void)st; XFER_TRAP("state_name"); }

/* Same rule for the lobby socket: these cases drive the client's parsers, not
 * its transport. A write that escapes to a real fd would mean a case started
 * talking to a server that is not there. */
int rnet_ws_write_text(int fd, const char *text, int client_mask)
{
    (void)fd; (void)text; (void)client_mask;
    fprintf(stderr, "lobby_client_test: rnet_ws_write_text was called; "
                    "this test does no networking\n");
    abort();
}
int rnet_ws_tx_queue_text(RNetWsTx *tx, const char *text, int client_mask)
{
    (void)tx; (void)text; (void)client_mask;
    fprintf(stderr, "lobby_client_test: rnet_ws_tx_queue_text was called; "
                    "this test does no networking\n");
    abort();
}
long rnet_ws_tx_flush(RNetWsTx *tx, int fd)
{
    (void)tx; (void)fd;
    fprintf(stderr, "lobby_client_test: rnet_ws_tx_flush was called; "
                    "this test does no networking\n");
    abort();
}
/* Pure bookkeeping, no socket: disconnect() frees the (empty) buffer. The
 * real ones are exercised against a socket by lobby_ws_backlog_test. */
size_t rnet_ws_tx_pending(const RNetWsTx *tx) { return tx ? tx->len - tx->off : 0u; }
void rnet_ws_tx_free(RNetWsTx *tx)
{
    if (!tx) return;
    free(tx->buf);
    memset(tx, 0, sizeof(*tx));
}

const char *rnet_account_session(void)
{
    XFER_TRAP("rnet_account_session");
}

/* Stands in for the mod runtime: two installed packages. */
static int two_pkg_offer(RNetLobbyModPkg *out, int max, void *ctx)
{
    (void)ctx;
    if (max < 2) return 0;
    memset(out, 0, sizeof(*out) * 2);
    snprintf(out[0].id, sizeof(out[0].id), "gwed.localization");
    snprintf(out[0].ver, sizeof(out[0].ver), "1.0.0");
    snprintf(out[1].id, sizeof(out[1].id), "gwed.enhancement.widescreen");
    snprintf(out[1].ver, sizeof(out[1].ver), "1.0.0");
    return 2;
}

/* The shape the server builds in slot_json(): the offer object verbatim,
 * beside the other per-seat fields. */
static void std_snprintf_slot(char *dst, size_t cap, const char *offer)
{
    snprintf(dst, cap,
             "{\"slot\":1,\"player_id\":\"them\",\"display_name\":\"Bob\","
             "\"ready\":true%s}", offer);
}

static int fails;

static void ck(int cond, const char *what)
{
    if (!cond) { printf("    FAIL %s\n", what); fails++; }
}

/* The shape the server insists on, checked as text: `"mods":[` and objects
 * carrying "id"/"ver". A string-encoded plan is valid JSON and passes every
 * round-trip test you could write against ourselves -- it just fails at the
 * server. That regression is what this assertion is here to catch. */
static void ck_is_json_array(const char *json)
{
    const char *m = strstr(json, "\"mod_plan\":");
    ck(m != NULL, "match_caps carries a mod_plan field");
    if (!m) return;
    ck(m[11] == '[', "mod_plan is a JSON array, not a string");
    /* The seat gate must stay inert. `mods` is the key the lobby server
     * enforces by refusing to seat; publishing it would put the gate back on
     * the door, where this title does not want it -- a peer without the mods
     * is supposed to get in and download them. */
    ck(strstr(json, "\"mods\":") == NULL,
       "the server's seat-gate key is NOT published");
}

static void case_rows(void)
{
    RNetLobbyMatchCaps caps;
    RNetLobbyModPkg back[RNET_LOBBY_MAX_MODS];
    char json[RNET_LOBBY_MAX_MODS * 256 + 512];
    char obj[RNET_LOBBY_MAX_MODS * 256 + 512];
    int n;

    memset(&caps, 0, sizeof(caps));
    caps.valid = 1;
    caps.input_delay = 2;
    caps.mod_count = 2;
    snprintf(caps.mods[0].id, sizeof(caps.mods[0].id), "gwed.localization");
    snprintf(caps.mods[0].ver, sizeof(caps.mods[0].ver), "1.0.0");
    snprintf(caps.mods[0].name, sizeof(caps.mods[0].name), "Localization");
    snprintf(caps.mods[0].feats, sizeof(caps.mods[0].feats), "localization");
    snprintf(caps.mods[1].id, sizeof(caps.mods[1].id),
             "gwed.enhancement.widescreen");
    snprintf(caps.mods[1].ver, sizeof(caps.mods[1].ver), "1.0.0");
    snprintf(caps.mods[1].name, sizeof(caps.mods[1].name), "Widescreen");
    snprintf(caps.mods[1].feats, sizeof(caps.mods[1].feats), "widescreen");

    ck(append_match_caps_json(json, sizeof(json), &caps) > 0, "caps encoded");
    printf("  json: %s\n", json);
    ck_is_json_array(json);
    /* No stray comma before the first element, which would make the whole
     * caps blob unparseable and take every other setting down with it. */
    ck(strstr(json, "[,") == NULL, "no leading comma in the array");
    ck(strstr(json, "\"id\":\"gwed.localization\"") != NULL, "row 0 id present");
    ck(strstr(json, "\"ver\":\"1.0.0\"") != NULL, "row 0 ver present");

    ck(json_extract_object(json, "match_caps", obj, sizeof(obj)) != 0,
       "match_caps object extracts whole");
    n = parse_mod_pkg_array(obj, "mod_plan", back, RNET_LOBBY_MAX_MODS);
    printf("  parsed back n=%d\n", n);
    ck(n == 2, "two rows round-tripped");
    if (n == 2) {
        ck(!strcmp(back[0].id, "gwed.localization"), "row 0 id");
        ck(!strcmp(back[0].ver, "1.0.0"), "row 0 ver");
        ck(!strcmp(back[0].feats, "localization"), "row 0 feats");
        ck(!strcmp(back[1].id, "gwed.enhancement.widescreen"), "row 1 id");
    }
}

static void case_empty(void)
{
    RNetLobbyMatchCaps caps;
    RNetLobbyModPkg back[RNET_LOBBY_MAX_MODS];
    char json[512];

    memset(&caps, 0, sizeof(caps));
    caps.valid = 1;
    caps.mod_count = 0;
    ck(append_match_caps_json(json, sizeof(json), &caps) > 0, "empty encoded");
    ck(strstr(json, "\"mod_plan\":[]") != NULL, "empty plan is an empty array");
    ck(parse_mod_pkg_array(json, "mod_plan", back, RNET_LOBBY_MAX_MODS) == 0,
       "empty array parses to no rows");
}

/* The superseded encoding must parse to NOTHING rather than to rows. Reading
 * it would leave this peer believing in requirements the server has already
 * ignored, so the two disagree about what the match needs. */
static void case_old_string_encoding_is_not_revived(void)
{
    RNetLobbyModPkg back[RNET_LOBBY_MAX_MODS];
    const char *old = "{\"v\":1,\"mod_plan\":\"gwed.localization@1.0.0/localization\"}";
    ck(parse_mod_pkg_array(old, "mods", back, RNET_LOBBY_MAX_MODS) == 0,
       "a string-encoded plan yields no rows");
}

/* A row without both halves of its identity is not a lesser row, it is a
 * different package -- the server matches on the pair. */
static void case_partial_rows_dropped(void)
{
    RNetLobbyModPkg back[RNET_LOBBY_MAX_MODS];
    const char *j =
        "{\"mod_plan\":[{\"id\":\"a.one\"},{\"ver\":\"2.0\"},"
        "{\"id\":\"b.two\",\"ver\":\"2.0\"}]}";
    int n = parse_mod_pkg_array(j, "mod_plan", back, RNET_LOBBY_MAX_MODS);
    ck(n == 1, "rows missing id or ver are dropped");
    if (n == 1) ck(!strcmp(back[0].id, "b.two"), "the complete row survives");
}

/* Refusing to publish beats publishing a short plan: a plan that names fewer
 * requirements than the host has seats a peer that cannot play. */
static void case_overflow_refuses(void)
{
    RNetLobbyMatchCaps caps;
    char small[64];
    int i;

    memset(&caps, 0, sizeof(caps));
    caps.valid = 1;
    caps.mod_count = RNET_LOBBY_MAX_MODS;
    for (i = 0; i < RNET_LOBBY_MAX_MODS; ++i) {
        snprintf(caps.mods[i].id, sizeof(caps.mods[i].id), "pkg.number.%02d", i);
        snprintf(caps.mods[i].ver, sizeof(caps.mods[i].ver), "1.0.0");
    }
    ck(append_match_caps_json(small, sizeof(small), &caps) == 0,
       "a plan that does not fit publishes nothing at all");
}

/* Set up a two-seat lobby: us in seat 0, a peer in seat 1 holding `peer_pkgs`,
 * and a host plan of `plan_pkgs`. Returns the launch gate's verdict. */
static int gate_with(const RNetLobbyModPkg *plan, int plan_n,
                     const RNetLobbyModPkg *peer, int peer_n,
                     char *who, size_t who_cap, char *what, size_t what_cap)
{
    memset(&g_lc.match_caps, 0, sizeof(g_lc.match_caps));
    g_lc.match_caps.valid = 1;
    g_lc.match_caps.mod_count = plan_n;
    memcpy(g_lc.match_caps.mods, plan, sizeof(RNetLobbyModPkg) * (size_t)plan_n);

    g_lc.in_lobby = 1;
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "me");
    g_lc.member_count = 2;
    snprintf(g_lc.members[0].player_id, sizeof(g_lc.members[0].player_id), "me");
    snprintf(g_lc.members[1].player_id, sizeof(g_lc.members[1].player_id), "them");
    snprintf(g_lc.members[1].display_name,
             sizeof(g_lc.members[1].display_name), "Bob");
    g_lc.member_offer_count[0] = 0;
    g_lc.member_offer_count[1] = peer_n;
    memcpy(g_lc.member_offer[1], peer, sizeof(RNetLobbyModPkg) * (size_t)peer_n);
    return rnet_lobby_match_blocked_by_mods(who, who_cap, what, what_cap);
}

static RNetLobbyModPkg row(const char *id, const char *ver)
{
    RNetLobbyModPkg r;
    memset(&r, 0, sizeof(r));
    snprintf(r.id, sizeof(r.id), "%s", id);
    snprintf(r.ver, sizeof(r.ver), "%s", ver);
    return r;
}

/* The launch gate asks "do you have this package", not "at this version".
 * Version agreement is settled by the mod-set exchange at session start, which
 * is finer (per feature, with resolved option values) and can explain itself.
 * Checking it here too would block a lobby over a difference that may not even
 * change the simulation. */
static void case_gate_matches_on_id_only(void)
{
    RNetLobbyModPkg plan[2];
    RNetLobbyModPkg peer[2];
    char who[64], what[160];

    plan[0] = row("gwed.localization", "1.0.0");
    peer[0] = row("gwed.localization", "2.5.1");   /* same package, other ver */
    ck(gate_with(plan, 1, peer, 1, who, sizeof(who), what, sizeof(what)) == 0,
       "a different version of the same package does NOT block the match");

    peer[0] = row("something.else", "1.0.0");
    ck(gate_with(plan, 1, peer, 1, who, sizeof(who), what, sizeof(what)) == 1,
       "a genuinely absent package blocks the match");
    ck(!strcmp(who, "Bob"), "the blocked player is named");
    ck(!strcmp(what, "gwed.localization@1.0.0"), "the missing package is named");

    plan[1] = row("gwed.enhancement.widescreen", "1.0.0");
    peer[0] = row("gwed.localization", "0.9");
    peer[1] = row("gwed.enhancement.widescreen", "9.9");
    ck(gate_with(plan, 2, peer, 2, who, sizeof(who), what, sizeof(what)) == 0,
       "every package present at any version starts the match");

    ck(gate_with(plan, 2, peer, 0, who, sizeof(who), what, sizeof(what)) == 2,
       "a peer announcing nothing is missing everything");
}

/* A peer can hold a package and still be unable to run one of its features:
 * the feature needs a file only the player can supply (another game's ROM).
 * The offer says which features those are, for every feature of the package;
 * the files gate counts only the ones the host's plan enables. */
static void case_gate_holds_for_missing_files(void)
{
    RNetLobbyModPkg plan[1];
    RNetLobbyModPkg peer[1];
    RNetLobbyModPkg back[RNET_LOBBY_MAX_MODS];
    char who[64], what[160], files[256], json[1024];
    int mods_missing = -1, files_missing = -1;

    plan[0] = row("megaman-x.character.zero", "0.0.1");
    snprintf(plan[0].feats, sizeof(plan[0].feats), "zero");
    peer[0] = row("megaman-x.character.zero", "0.0.1");
    snprintf(peer[0].nf, sizeof(peer[0].nf), "other,zero");

    ck(append_mod_pkg_array(json, sizeof(json), "pkgs", peer, 1) > 0,
       "an offer row with missing files encodes");
    ck(strstr(json, "\"nf\":\"other,zero\"") != NULL, "nf is on the wire");
    ck(parse_mod_pkg_array(json, "pkgs", back, RNET_LOBBY_MAX_MODS) == 1 &&
           !strcmp(back[0].nf, "other,zero"),
       "nf round-trips");
    ck(append_mod_pkg_array(json, sizeof(json), "pkgs", plan, 1) > 0 &&
           strstr(json, "\"nf\"") == NULL,
       "a row with nothing missing does not carry nf");

    ck(gate_with(plan, 1, peer, 1, who, sizeof(who), what, sizeof(what)) == 0,
       "having the package satisfies the mods gate");
    ck(rnet_lobby_match_blocked_by_files(who, sizeof(who), what,
                                         sizeof(what)) == 1,
       "a plan feature without its files blocks the match");
    ck(!strcmp(who, "Bob"), "the player choosing a file is named");
    ck(!strcmp(what, "megaman-x.character.zero/zero"),
       "the feature needing files is named");
    ck(rnet_lobby_member_mod_readiness(1, &mods_missing, &files_missing, files,
                                       sizeof(files)) == 1 &&
           mods_missing == 0 && files_missing == 1 &&
           !strcmp(files, "megaman-x.character.zero/zero"),
       "member readiness reports the missing files");

    snprintf(peer[0].nf, sizeof(peer[0].nf), "other");
    (void)gate_with(plan, 1, peer, 1, who, sizeof(who), what, sizeof(what));
    ck(rnet_lobby_match_blocked_by_files(who, sizeof(who), what,
                                         sizeof(what)) == 0,
       "files missing for a feature the host did not enable do not block");

    (void)gate_with(plan, 1, peer, 0, who, sizeof(who), what, sizeof(what));
    ck(rnet_lobby_match_blocked_by_files(who, sizeof(who), what,
                                         sizeof(what)) == 0,
       "a missing package is the mods gate's business, not the files gate's");
}

/* The transfer hooks must outlive a lobby reconnect.
 *
 * They used to live in LobbyClient, which rnet_lobby_disconnect() memsets --
 * and connect() calls disconnect() first. So the host installed them at
 * start-up, wiped them the moment it joined a lobby, and then refused every
 * download with "could not pack the mod" while being entirely able to pack
 * it. Anything that is a property of the BUILD rather than of the connection
 * belongs outside that struct. */
static int hook_calls;
static int fake_export(const char *id, const char *ver, uint8_t **out,
                       uint32_t *out_len, char *sha, uint32_t sha_cap,
                       char *err, uint32_t err_cap, void *ctx)
{
    (void)id; (void)ver; (void)out; (void)out_len; (void)sha; (void)sha_cap;
    (void)err; (void)err_cap; (void)ctx;
    hook_calls++;
    return 0;
}
static void fake_free(uint8_t *b) { (void)b; }
static int fake_install(const uint8_t *d, uint32_t l, const char *sha,
                        char *id, uint32_t ic, char *ver, uint32_t vc,
                        char *err, uint32_t ec, void *ctx)
{
    (void)d; (void)l; (void)sha; (void)id; (void)ic; (void)ver; (void)vc;
    (void)err; (void)ec; (void)ctx;
    return 0;
}

static void case_hooks_survive_disconnect(void)
{
    rnet_lobby_set_mod_transfer_hooks(fake_export, fake_free, fake_install, NULL);
    ck(g_mod_export_fn == fake_export, "export hook installs");
    /* connect() begins with exactly this call. */
    rnet_lobby_disconnect();
    ck(g_mod_export_fn == fake_export,
       "export hook survives the disconnect that connect() performs");
    ck(g_mod_install_fn == fake_install, "install hook survives too");
    ck(g_mod_offer_fn == NULL || g_mod_offer_fn != NULL, "offer supplier intact");
    rnet_lobby_set_mod_transfer_hooks(NULL, NULL, NULL, NULL);
}

/* The ICE signal a peer emits is not the one its partner must be handed. */
static void case_ice_local_becomes_remote(void)
{
    ck(mod_ice_type_for_push((int)RNET_SIGNAL_LOCAL_SDP) ==
       (int)RNET_SIGNAL_REMOTE_SDP, "a peer's LOCAL_SDP is pushed as REMOTE_SDP");
    ck(mod_ice_type_for_push((int)RNET_SIGNAL_LOCAL_CANDIDATE) ==
       (int)RNET_SIGNAL_REMOTE_CANDIDATE,
       "a peer's LOCAL_CANDIDATE is pushed as REMOTE_CANDIDATE");
    /* These mean the same on both sides. */
    ck(mod_ice_type_for_push((int)RNET_SIGNAL_GATHERING_DONE) ==
       (int)RNET_SIGNAL_GATHERING_DONE, "GATHERING_DONE passes through");
    ck(mod_ice_type_for_push((int)RNET_SIGNAL_SET_CONTROLLING) ==
       (int)RNET_SIGNAL_SET_CONTROLLING, "SET_CONTROLLING passes through");
    /* Forwarding REMOTE_* would mean somebody already translated it once. */
    ck(mod_ice_type_for_push((int)RNET_SIGNAL_REMOTE_SDP) ==
       (int)RNET_SIGNAL_REMOTE_SDP, "REMOTE_SDP is not translated twice");
}

/* The offer this peer SENDS must be readable by the code that RECEIVES one.
 *
 * They are written in different places -- append_mod_offer builds it, and the
 * slot parser reads it back out of the server's echo -- and they disagreed:
 * one wrote {"pkgs":[...]}, the other looked for a bare array. Every peer
 * therefore looked empty-handed, and a guest with every mod installed was
 * still refused at Play. This asserts the two ends against each other rather
 * than each against its own idea of the format. */
static void case_offer_round_trips_through_a_slot_row(void)
{
    char offer[RNET_LOBBY_MAX_MODS * 256 + 64];
    char slot_row[RNET_LOBBY_MAX_MODS * 256 + 256];
    char obj[RNET_LOBBY_MAX_MODS * 256 + 64];
    RNetLobbyModPkg back[RNET_LOBBY_MAX_MODS];
    int n;

    rnet_lobby_set_mod_offer_supplier(two_pkg_offer, NULL);
    ck(append_mod_offer(offer, sizeof(offer)) > 0, "offer encodes");
    /* The server stores the object and echoes it inside the slot row. */
    std_snprintf_slot(slot_row, sizeof(slot_row), offer);
    ck(strstr(slot_row, "\"mod_offer\":{") != NULL,
       "the offer travels as an object, which is what the server accepts");

    ck(json_extract_object(slot_row, "mod_offer", obj, sizeof(obj)) != 0,
       "the offer object extracts from a slot row");
    n = parse_mod_pkg_array(obj, "pkgs", back, RNET_LOBBY_MAX_MODS);
    ck(n == 2, "both offered packages are read back");
    if (n == 2) {
        ck(!strcmp(back[0].id, "gwed.localization"), "offer row 0 id");
        ck(!strcmp(back[1].id, "gwed.enhancement.widescreen"), "offer row 1 id");
    }
    rnet_lobby_set_mod_offer_supplier(NULL, NULL);
}

/* The host's exact configuration has to survive the wire, because a guest
 * adopts it verbatim. The plan says which packages; this says how they are
 * set up, and a guest that owns both mods but enabled neither passes the
 * plan and is still refused at launch. */
static void case_mod_set_round_trips(void)
{
    RNetLobbyMatchCaps caps;
    RNetLobbyMatchCaps back;
    char json[RNET_LOBBY_MAX_MODS * 256 + 1024];
    char obj[RNET_LOBBY_MAX_MODS * 256 + 1024];

    memset(&caps, 0, sizeof(caps));
    caps.valid = 1;
    caps.mod_count = 0;
    snprintf(caps.mod_set, sizeof(caps.mod_set),
             "gwed.enhancement.widescreen@1.0.0/widescreen;"
             "gwed.localization@1.0.0/localization language=en");
    ck(append_match_caps_json(json, sizeof(json), &caps) > 0, "caps encode");
    ck(json_extract_object(json, "match_caps", obj, sizeof(obj)) != 0,
       "caps object extracts");
    memset(&back, 0, sizeof(back));
    parse_match_caps_object(obj, &back);
    ck(!strcmp(back.mod_set, caps.mod_set),
       "the host's effective set survives the round trip verbatim");
    /* Option values are part of the identity, not decoration. */
    ck(strstr(back.mod_set, "language=en") != NULL,
       "resolved option values survive");
}


/* ---- relay size cap -----------------------------------------------------
 *
 * A relayed transfer is carried by the TURN server, so its bytes are the
 * relay operator's cost, not the two players'. Direct pairs are free to both
 * of us and stay uncapped at any size; only "relay" is priced.
 *
 * rnet_lobby_mod_relay_size_allows is pure on purpose: the decision it makes
 * is the whole policy, and testing it here means testing it without a socket,
 * a TURN server, or a peer that has to be persuaded not to find a direct
 * route. */
#define ONE_MB (1024u * 1024u)

static void case_relay_cap_is_five_mb(void)
{
    char why[256];
    printf("  relay cap\n");

    ck(RNET_LOBBY_MOD_RELAY_MAX_BYTES == 5u * ONE_MB,
       "the cap is 5 MiB");

    /* Exactly at the cap is allowed: the rule is "over 5 MB", and a package
     * that is 5 MB to the byte is not over it. */
    ck(rnet_lobby_mod_relay_size_allows("relay", 5u * ONE_MB, "m", why,
                                        sizeof(why)) == 1,
       "5 MB exactly still goes over the relay");
    ck(why[0] == '\0', "an allowed transfer writes no reason");

    ck(rnet_lobby_mod_relay_size_allows("relay", 5u * ONE_MB + 1u, "m", why,
                                        sizeof(why)) == 0,
       "one byte over the cap is refused on the relay");
}

static void case_direct_paths_are_never_capped(void)
{
    char why[256];
    const char *direct[] = { "host", "srflx", "prflx" };
    size_t i;
    printf("  direct paths uncapped\n");

    /* 400 MB over a direct pair is fine. This is the case the cap exists to
     * leave alone -- a peer-to-peer link costs nobody but the two players,
     * and refusing a big mod on it would be a limit invented for no reason. */
    for (i = 0; i < sizeof(direct) / sizeof(direct[0]); ++i) {
        why[0] = 'x';
        ck(rnet_lobby_mod_relay_size_allows(direct[i], 400u * ONE_MB, "big",
                                            why, sizeof(why)) == 1,
           direct[i]);
        ck(why[0] == '\0', "no reason on an allowed direct transfer");
    }
}

static void case_unknown_path_allows(void)
{
    char why[256];
    printf("  unknown path\n");

    /* Asked before the pair is named, the answer is yes. A "no" here would
     * cap direct transfers on nothing more than a query that came too early,
     * which is the failure this policy must not have. The callers are what
     * make this safe: both wait for a named path first, and the host says so
     * in the log on the rare occasion it gives up waiting. */
    ck(rnet_lobby_mod_relay_size_allows("unknown", 400u * ONE_MB, "big", why,
                                        sizeof(why)) == 1,
       "an unnamed path does not refuse");
    ck(rnet_lobby_mod_relay_size_allows(NULL, 400u * ONE_MB, "big", why,
                                        sizeof(why)) == 1,
       "a NULL path does not refuse");
    ck(rnet_lobby_mod_relay_size_allows("none", 400u * ONE_MB, "big", why,
                                        sizeof(why)) == 1,
       "a build without ICE does not refuse");
}

static void case_refusal_tells_the_player_what_to_do(void)
{
    char why[256];
    printf("  refusal text\n");

    why[0] = '\0';
    (void)rnet_lobby_mod_relay_size_allows("relay", 12u * ONE_MB + 512u * 1024u,
                                           "gwed.enormous", why, sizeof(why));
    /* The three things a player needs and cannot work out for themselves:
     * which mod, how big it is, and that the fix is to fetch it elsewhere
     * rather than to click again. */
    ck(strstr(why, "gwed.enormous") != NULL, "the reason names the package");
    ck(strstr(why, "12.5 MB") != NULL, "the reason states the actual size");
    ck(strstr(why, "5 MB") != NULL, "the reason states the cap");
    ck(strstr(why, "original source") != NULL,
       "the reason says where to get it instead");
    ck(strlen(why) < 256, "the reason fits the transfer error buffer");

    /* A missing id still produces a sentence rather than an empty gap. */
    why[0] = '\0';
    (void)rnet_lobby_mod_relay_size_allows("relay", 9u * ONE_MB, NULL, why,
                                           sizeof(why));
    ck(strstr(why, "that mod") != NULL, "an unnamed package still reads");
}

static void case_refusal_survives_a_short_buffer(void)
{
    char why[16];
    printf("  short buffer\n");

    /* The verdict is the return value, never the length of the reason. A
     * caller with a small buffer still gets refused. */
    ck(rnet_lobby_mod_relay_size_allows("relay", 90u * ONE_MB, "m", why,
                                        sizeof(why)) == 0,
       "a truncated reason is still a refusal");
    ck(why[sizeof(why) - 1] == '\0', "the short reason stays terminated");
    ck(rnet_lobby_mod_relay_size_allows("relay", 90u * ONE_MB, "m", NULL, 0)
           == 0,
       "no reason buffer at all is still a refusal");
}


/* ---- spectator seats ----------------------------------------------------
 *
 * Two arrays of identical rows land in one membership table tagged by role.
 * The parser is the whole client-side contract: a role read wrong here is a
 * spectator that sends input, or a player that cannot.
 */
static void case_spectator_rows_are_tagged(void)
{
    const char *json =
        "{\"op\":\"lobby_update\",\"player_count\":2,\"max_slots\":2,"
        "\"allow_spectators\":true,\"max_spectators\":4,"
        "\"spectator_count\":2,\"spectator_slot_base\":64,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"Host\",\"ready\":true},"
        "{\"slot\":1,\"player_id\":\"g\",\"display_name\":\"Guest\",\"ready\":false}],"
        "\"spectators\":[{\"slot\":64,\"player_id\":\"s0\",\"display_name\":\"Watcher\",\"ready\":false},"
        "{\"slot\":66,\"player_id\":\"s2\",\"display_name\":\"Other\",\"ready\":false}]}";
    printf("  spectator rows\n");
    memset(&g_lc, 0, sizeof(g_lc));
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "s2");
    parse_slots_array(json);

    ck(g_lc.member_count == 4, "both tables land in one membership list");
    ck(g_lc.members[0].is_spectator == 0, "row 0 is a player");
    ck(g_lc.members[1].is_spectator == 0, "row 1 is a player");
    ck(g_lc.members[2].is_spectator == 1, "row 2 is a spectator");
    ck(g_lc.members[3].is_spectator == 1, "row 3 is a spectator");
    /* Seat indices survive verbatim -- they are what kick / move send back. */
    ck(g_lc.members[2].slot == 64, "spectator seat index is preserved");
    ck(g_lc.members[3].slot == 66, "a sparse gallery keeps its indices");
    ck(strcmp(g_lc.members[3].display_name, "Other") == 0, "names still parse");

    /* The local client is s2, in the gallery. */
    ck(rnet_lobby_local_is_spectator() == 1, "this client knows it is watching");
    ck(g_lc.join.local_slot == 66, "and which seat it holds");
    ck(rnet_lobby_allow_spectators() == 1, "allow_spectators round-trips");
    ck(rnet_lobby_max_spectators() == 4, "max_spectators round-trips");
    ck(rnet_lobby_spectator_slot_base() == 64, "the base comes from the server");
}

static void case_a_player_is_not_a_spectator(void)
{
    const char *json =
        "{\"allow_spectators\":true,\"max_spectators\":4,\"spectator_slot_base\":64,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"me\",\"display_name\":\"Me\",\"ready\":true}],"
        "\"spectators\":[{\"slot\":64,\"player_id\":\"s\",\"display_name\":\"S\",\"ready\":false}]}";
    printf("  player role\n");
    memset(&g_lc, 0, sizeof(g_lc));
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "me");
    parse_slots_array(json);
    ck(rnet_lobby_local_is_spectator() == 0, "a seated player is not watching");
    ck(g_lc.local_ready == 1, "and its own ready still tracks");
}

static void case_promotion_flips_the_role(void)
{
    /* The host moved us out of the gallery. The role has to move with the
     * seat in the SAME update, because everything downstream -- whether this
     * build sends input at all -- reads it. */
    const char *watching =
        "{\"allow_spectators\":true,\"spectator_slot_base\":64,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\",\"ready\":false}],"
        "\"spectators\":[{\"slot\":64,\"player_id\":\"me\",\"display_name\":\"Me\",\"ready\":false}]}";
    const char *playing =
        "{\"allow_spectators\":true,\"spectator_slot_base\":64,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\",\"ready\":false},"
        "{\"slot\":1,\"player_id\":\"me\",\"display_name\":\"Me\",\"ready\":false}],"
        "\"spectators\":[]}";
    printf("  promotion\n");
    memset(&g_lc, 0, sizeof(g_lc));
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "me");
    parse_slots_array(watching);
    ck(rnet_lobby_local_is_spectator() == 1, "watching first");
    parse_slots_array(playing);
    ck(rnet_lobby_local_is_spectator() == 0, "playing after the host moved us");
    ck(g_lc.join.local_slot == 1, "and holding the seat we were moved to");
    ck(g_lc.member_count == 2, "the empty gallery contributes no rows");
}

static void case_a_server_without_spectators_reads_as_before(void)
{
    /* The compatibility case. No "spectators" key at all: the lobby is
     * exactly the lobby this client saw before the feature existed. */
    const char *json =
        "{\"player_count\":2,\"max_slots\":2,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\",\"ready\":true},"
        "{\"slot\":1,\"player_id\":\"me\",\"display_name\":\"Me\",\"ready\":true}]}";
    printf("  legacy server\n");
    memset(&g_lc, 0, sizeof(g_lc));
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "me");
    parse_slots_array(json);
    ck(g_lc.member_count == 2, "both players parse");
    ck(g_lc.members[0].is_spectator == 0 && g_lc.members[1].is_spectator == 0,
       "nobody is tagged as a spectator");
    ck(rnet_lobby_allow_spectators() == 0, "and the gallery is reported closed");
    ck(rnet_lobby_local_is_spectator() == 0, "so this client is a player");
}

static void case_launch_does_not_erase_the_gallery(void)
{
    /* `launch` carries spectators/spectator_count but no allow_spectators --
     * it has no reason to. Defaulting that to 0 would erase the gallery at
     * the exact moment the client decides whether it is in it. */
    const char *update =
        "{\"allow_spectators\":true,\"max_spectators\":4,\"spectator_slot_base\":64,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\",\"ready\":true}],"
        "\"spectators\":[{\"slot\":64,\"player_id\":\"me\",\"display_name\":\"Me\",\"ready\":false}]}";
    const char *launch =
        "{\"op\":\"launch\",\"player_count\":1,\"spectator_count\":1,"
        "\"spectator_slot_base\":64,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\",\"ready\":true}],"
        "\"spectators\":[{\"slot\":64,\"player_id\":\"me\",\"display_name\":\"Me\",\"ready\":false}]}";
    printf("  launch keeps the role\n");
    memset(&g_lc, 0, sizeof(g_lc));
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "me");
    parse_slots_array(update);
    ck(rnet_lobby_allow_spectators() == 1, "gallery open in the lobby");
    parse_slots_array(launch);
    ck(rnet_lobby_allow_spectators() == 1, "still open through launch");
    ck(rnet_lobby_max_spectators() == 4, "and the size is remembered");
    ck(rnet_lobby_local_is_spectator() == 1, "we launch as a spectator");
}

static void case_gallery_seat_addressing(void)
{
    printf("  seat addressing\n");
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.join.spectator_slot_base = 64;
    ck(rnet_lobby_spectator_slot(0) == 64, "gallery 0 addresses seat 64");
    ck(rnet_lobby_spectator_slot(3) == 67, "gallery 3 addresses seat 67");
    ck(rnet_lobby_spectator_slot(4) == -1, "past the gallery is refused");
    ck(rnet_lobby_spectator_slot(-1) == -1, "and so is a negative index");
    /* Player seats must keep the indices they always had, or every existing
     * `slot` on the wire changes meaning. */
    ck(RNET_LOBBY_MAX_PLAYERS < RNET_LOBBY_SPECTATOR_SLOT_BASE,
       "the two halves of the namespace cannot collide");
}


/* ---- lobby chat ---------------------------------------------------------
 *
 * The server echoes every line to everyone INCLUDING the sender, and that
 * echo is the copy the ring keeps -- the client never appends its own send.
 * So "is this mine" is a question about player ids, not about who called
 * send, and the ring's order is the room's order.
 */
static void case_chat_ring_keeps_room_order(void)
{
    RNetLobbyChatMsg got;
    int i;
    printf("  chat ring\n");
    memset(&g_lc, 0, sizeof(g_lc));
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "me");

    chat_push("them", "", "Them", "first", "", 0);
    chat_push("me",   "", "Me",   "second", "", 0);
    chat_push("",     "", "",     "third", "", 1);

    ck(rnet_lobby_chat_count() == 3, "three lines land");
    ck(rnet_lobby_chat_get(0, &got) && strcmp(got.text, "first") == 0,
       "index 0 is the oldest");
    ck(rnet_lobby_chat_get(2, &got) && strcmp(got.text, "third") == 0,
       "index 2 is the newest");

    ck(rnet_lobby_chat_get(0, &got) && got.is_local == 0,
       "a peer's line is not local");
    ck(rnet_lobby_chat_get(1, &got) && got.is_local == 1,
       "our own echoed line is local");
    ck(rnet_lobby_chat_get(2, &got) && got.is_system == 1 && got.is_local == 0,
       "a system line is neither ours nor a player's");

    /* seq is what the UI compares to notice a new line. */
    {
        uint32_t a = 0, b = 0;
        (void)rnet_lobby_chat_get(0, &got); a = got.seq;
        (void)rnet_lobby_chat_get(2, &got); b = got.seq;
        ck(b > a, "seq increases with arrival order");
    }
    ck(!rnet_lobby_chat_get(3, &got), "reading past the end is refused");
    ck(!rnet_lobby_chat_get(-1, &got), "so is a negative index");
    (void)i;
}

static void case_chat_ring_wraps_oldest_first(void)
{
    RNetLobbyChatMsg got;
    int i;
    char buf[32];
    printf("  chat wrap\n");
    memset(&g_lc, 0, sizeof(g_lc));
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "me");

    /* Overfill by ten. The ring must drop the OLDEST, not the newest -- a
     * chat that discards what was just said is worse than no chat. */
    for (i = 0; i < RNET_LOBBY_CHAT_RING + 10; ++i) {
        snprintf(buf, sizeof(buf), "line%d", i);
        chat_push("them", "", "Them", buf, "", 0);
    }
    ck(rnet_lobby_chat_count() == RNET_LOBBY_CHAT_RING,
       "the ring stops at its capacity");
    ck(rnet_lobby_chat_get(0, &got) && strcmp(got.text, "line10") == 0,
       "the oldest surviving line is the 11th sent");
    snprintf(buf, sizeof(buf), "line%d", RNET_LOBBY_CHAT_RING + 9);
    ck(rnet_lobby_chat_get(RNET_LOBBY_CHAT_RING - 1, &got) &&
       strcmp(got.text, buf) == 0,
       "the newest line is the last one sent");
}

static void case_chat_ignores_empty_and_clears(void)
{
    printf("  chat empty/clear\n");
    memset(&g_lc, 0, sizeof(g_lc));
    chat_push("them", "", "Them", "", "", 0);
    chat_push("them", "", "Them", NULL, "", 0);
    ck(rnet_lobby_chat_count() == 0, "an empty line is not a line");

    chat_push("them", "", "Them", "hello", "", 0);
    ck(rnet_lobby_chat_count() == 1, "a real line is");
    {
        RNetLobbyChatMsg m;
        uint32_t before;
        (void)rnet_lobby_chat_get(0, &m);
        before = m.seq;
        rnet_lobby_chat_clear();
        ck(rnet_lobby_chat_count() == 0, "clear empties the room log");
        chat_push("them", "", "Them", "new room", "", 0);
        (void)rnet_lobby_chat_get(0, &m);
        /* seq must NOT restart: a UI tracking "newest seen" would otherwise
         * mistake the first line of a new room for one it already scrolled
         * past, and never scroll to it. */
        ck(m.seq > before, "seq keeps counting across a clear");
    }
}


/* A spectator's ICE offer must never reach a player's netplay agent.
 *
 * This is the bug that ended a live match: the lobby relay forwards a
 * broadcast `signal` to EVERYONE, a spectator's agent gathers and offers just
 * like a player's, and the netplay session owns exactly one ICE agent. Handing
 * that agent a third party's SDP after it already has a remote description
 * reads to it as a peer ICE restart -- it destroys the connected agent and
 * rebinds to the wrong party. Both players watched the match freeze the moment
 * someone opened the gallery.
 *
 * The asserts below are on the real predicate the WS handler calls, against a
 * real membership table, because the failure was precisely that nobody asked
 * WHO sent it. */
static void case_the_gallery_does_not_negotiate(void)
{
    const char *json =
        "{\"op\":\"lobby_update\",\"player_count\":2,\"max_slots\":2,"
        "\"allow_spectators\":true,\"max_spectators\":4,"
        "\"spectator_count\":1,\"spectator_slot_base\":64,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"Host\",\"ready\":true},"
        "{\"slot\":1,\"player_id\":\"g\",\"display_name\":\"Guest\",\"ready\":false}],"
        "\"spectators\":[{\"slot\":64,\"player_id\":\"s0\",\"display_name\":\"Watcher\",\"ready\":false}]}";
    printf("  gallery does not negotiate\n");

    /* We are the host, seated. */
    memset(&g_lc, 0, sizeof(g_lc));
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "h");
    parse_slots_array(json);

    ck(ice_signal_is_for_us((int)RNET_SIGNAL_LOCAL_SDP, "g") == 1,
       "the other player's SDP is the one we are negotiating with");
    ck(ice_signal_is_for_us((int)RNET_SIGNAL_LOCAL_CANDIDATE, "g") == 1,
       "and so are its candidates");
    ck(ice_signal_is_for_us((int)RNET_SIGNAL_LOCAL_SDP, "s0") == 0,
       "a spectator's SDP is refused -- accepting it destroys the live agent");
    ck(ice_signal_is_for_us((int)RNET_SIGNAL_LOCAL_CANDIDATE, "s0") == 0,
       "and its candidates with it");
    ck(ice_signal_is_for_us((int)RNET_SIGNAL_GATHERING_DONE, "s0") == 0,
       "every ICE type is filtered, not just the SDP");

    /* A sender we cannot attribute stays accepted. Dropping it would break a
     * working two-player match against a server that predates from_player_id,
     * to close a hole that server cannot open -- it has nobody to broadcast a
     * third offer from. */
    ck(ice_signal_is_for_us((int)RNET_SIGNAL_LOCAL_SDP, "") == 1,
       "an unattributable sender is handled as before");
    ck(ice_signal_is_for_us((int)RNET_SIGNAL_LOCAL_SDP, NULL) == 1,
       "and so is a missing one");

    /* Not an ICE signal at all: not this predicate's business. */
    ck(ice_signal_is_for_us(RNET_LOBBY_SIG_RTT_PING, "s0") == 1,
       "a non-ICE signal is not judged by an ICE rule");

    /* Now run the same table as the spectator. It rides the server input
     * relay and has no ICE peer at all, so nothing is for it. */
    memset(&g_lc, 0, sizeof(g_lc));
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "s0");
    parse_slots_array(json);
    ck(rnet_lobby_local_is_spectator() == 1, "this client is watching");
    ck(ice_signal_is_for_us((int)RNET_SIGNAL_LOCAL_SDP, "h") == 0,
       "a spectator ingests nothing, not even the host's offer");
    ck(ice_signal_is_for_us((int)RNET_SIGNAL_LOCAL_SDP, "g") == 0,
       "nor the other player's");
}


/* The transport the LAUNCH assigned survives whatever the lobby says next.
 *
 * This is the regression that produced a spectator's black screen. The server
 * allocated its UDP input relay and said so with relay_endpoint; the client
 * recorded that in match_caps.force_input_relay -- a field the HOST's own
 * published caps also write, where it means the host's UI toggle and is
 * normally false. Any lobby_update arriving before the game actually started
 * (a mod-plan republish, a ready toggle, someone joining) re-ingested those
 * caps and erased the assignment, so the match fell back to p2p ICE. Two
 * players merely lost the relay; a spectator lost its only possible route and
 * rendered black. It "happened inconsistently" because it turned on whether a
 * lobby_update happened to land in that window.
 *
 * So the assertion is not "the flag is set" -- it was, briefly. It is that the
 * flag is still set after the traffic that used to clear it, and that the two
 * meanings of the name are read from two different places. */
/* A spectator does not re-arm ready on every lobby_update: the server never
 * stores its ready (the gallery cannot hold up a start) but answers each
 * set_ready with another update, so re-arming was an endless storm. A player
 * still re-arms. */
static void case_gallery_does_not_rearm_ready(void)
{
    const char *json =
        "{\"op\":\"lobby_update\",\"player_count\":2,\"max_slots\":2,"
        "\"allow_spectators\":true,\"max_spectators\":4,"
        "\"spectator_count\":1,\"spectator_slot_base\":64,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"Host\",\"ready\":false},"
        "{\"slot\":1,\"player_id\":\"g\",\"display_name\":\"Guest\",\"ready\":false}],"
        "\"spectators\":[{\"slot\":64,\"player_id\":\"s0\",\"display_name\":\"Watcher\",\"ready\":false}]}";
    printf("  gallery does not re-arm ready\n");

    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = 1000;
    g_lc.connected = 1;
    g_lc.in_lobby = 1;
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "s0");
    handle_server_json(json);
    ck(g_lc.join.local_is_spectator == 1, "we are in the gallery");
    ck(g_lc.pending_n == 0, "a spectator queues no set_ready on a lobby_update");
    handle_server_json(json);
    ck(g_lc.pending_n == 0, "nor on the next one (was: one per update, forever)");

    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = 1000;
    g_lc.connected = 1;
    g_lc.in_lobby = 1;
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "g");
    handle_server_json(json);
    ck(g_lc.pending_n == 1 && strstr(g_lc.pending_tx[0], "set_ready") != NULL,
       "a player still re-arms ready");
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = -1;
}

/* A peer with no game image to boot holds the match by staying not ready.
 * Every set_ready says false while blocked -- the lobby_update auto re-arm
 * included, which used to flip it straight back -- and the host's start gate
 * names that peer. Lifting the block announces ready again. */
static void case_launch_blocked_holds_ready(void)
{
    const char *json =
        "{\"op\":\"lobby_update\",\"player_count\":2,\"max_slots\":2,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"Host\",\"ready\":true},"
        "{\"slot\":1,\"player_id\":\"g\",\"display_name\":\"Guest\",\"ready\":false}]}";
    char who[64];
    printf("  a blocked launch holds ready\n");

    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = 1000;
    g_lc.connected = 1;
    g_lc.in_lobby = 1;
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "g");
    rnet_lobby_set_launch_blocked(1);
    ck(g_lc.pending_n == 1 &&
           strstr(g_lc.pending_tx[0], "\"ready\":false") != NULL,
       "blocking announces not ready");
    handle_server_json(json);
    ck(g_lc.pending_n == 1, "a lobby_update does not re-arm a blocked peer");
    (void)rnet_lobby_set_ready(1);
    ck(g_lc.pending_n == 2 &&
           strstr(g_lc.pending_tx[1], "\"ready\":false") != NULL,
       "an explicit set_ready(1) still says false while blocked");
    rnet_lobby_set_launch_blocked(0);
    ck(g_lc.pending_n == 3 &&
           strstr(g_lc.pending_tx[2], "\"ready\":true") != NULL,
       "lifting the block announces ready");

    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = 1000;
    g_lc.connected = 1;
    g_lc.in_lobby = 1;
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "h");
    handle_server_json(json);
    ck(rnet_lobby_match_blocked_by_unready(who, sizeof(who)) == 1 &&
           !strcmp(who, "Guest"),
       "the host's gate names the peer that is not ready");
    g_lc.members[1].ready = 1;
    ck(rnet_lobby_match_blocked_by_unready(who, sizeof(who)) == 0,
       "a ready peer does not block");
    g_lc.members[1].ready = 0;
    g_lc.members[1].is_spectator = 1;
    ck(rnet_lobby_match_blocked_by_unready(who, sizeof(who)) == 0,
       "a spectator never blocks a start");

    g_lc.members[1].is_spectator = 0;
    g_lc.members[1].ready = 1;
    g_lc.is_host = 1;
    g_launch_blocked = 1;
    ck(rnet_lobby_request_start(NULL) == -1 &&
           !strcmp(g_lc.join.last_error, "local_not_ready"),
       "a host with nothing to boot does not start");
    g_launch_blocked = 0;
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = -1;
}

static void case_launch_transport_survives_a_lobby_update(void)
{
    const char *launch =
        "{\"op\":\"launch\",\"ok\":true,\"lobby_id\":\"L\",\"session_id\":8,"
        "\"host_endpoint\":\"relay.example:8777\","
        "\"guest_endpoint\":\"relay.example:8777\","
        "\"relay_endpoint\":\"relay.example:8777\",\"transport\":\"sfu\","
        "\"player_count\":2,\"max_slots\":2,\"spectator_relay_base\":2,"
        "\"match_caps\":{\"v\":1,\"input_delay\":9,\"force_input_relay\":false},"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"Host\"},"
        "{\"slot\":1,\"player_id\":\"g\",\"display_name\":\"Guest\"}]}";
    /* The host republishing its plan. Its caps carry force_input_relay=false,
     * because on the way OUT that field means the host's toggle. */
    const char *update =
        "{\"op\":\"lobby_update\",\"lobby_id\":\"L\",\"player_count\":2,"
        "\"max_slots\":2,\"host_endpoint\":\"relay.example:8777\","
        "\"guest_endpoint\":\"relay.example:8777\","
        "\"match_caps\":{\"v\":1,\"input_delay\":9,\"force_input_relay\":false},"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"Host\"},"
        "{\"slot\":1,\"player_id\":\"g\",\"display_name\":\"Guest\"}]}";
    RNetLobbyJoinInfo out;

    printf("  launch transport survives lobby traffic\n");
    memset(&g_lc, 0, sizeof(g_lc));
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "g");
    g_lc.connected = 1;

    handle_server_json(launch);
    ck(g_lc.join.force_input_relay == 1,
       "a launch carrying relay_endpoint is a relayed match");
    ck(strcmp(g_lc.join.peer_hostport, "relay.example:8777") == 0,
       "and everyone dials the relay");
    ck(strcmp(g_lc.join.bind_hostport, "0.0.0.0:0") == 0,
       "from an ephemeral local bind");

    /* The traffic that used to undo it. */
    handle_server_json(update);
    ck(g_lc.match_caps.force_input_relay == 0,
       "the host's published caps still say what the host's toggle says");
    ck(g_lc.join.force_input_relay == 1,
       "but the match is still relayed — the launch decided that, not the caps");

    /* And this is the value the session is actually started with. Reading the
     * caps copy here instead is the shipped bug, and fails this case. */
    ck(rnet_lobby_try_fill_launch(&out) == 1, "the launch still fills");
    ck(out.force_input_relay == 1,
       "so the session starts on the relay, not on p2p ICE");

    /* A p2p launch must not inherit the 1 from the relayed match before it. */
    handle_server_json(
        "{\"op\":\"launch\",\"ok\":true,\"lobby_id\":\"L\",\"session_id\":9,"
        "\"host_endpoint\":\"1.2.3.4:5000\",\"guest_endpoint\":\"5.6.7.8:6000\","
        "\"transport\":\"ice_p2p\",\"player_count\":2,\"max_slots\":2,"
        "\"match_caps\":{\"v\":1,\"input_delay\":9,\"force_input_relay\":false},"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"Host\"},"
        "{\"slot\":1,\"player_id\":\"g\",\"display_name\":\"Guest\"}]}");
    ck(g_lc.join.force_input_relay == 0,
       "a p2p launch does not inherit the previous match's relay");
}

/* Two instances on one machine (or two players behind one NAT) can advertise
 * the same public ip:port: the host relay takes its port first-free from 7777,
 * the guest first-free from 7778, and a busy 7777 puts both on :7778. An ICE
 * launch with those endpoints is still ICE -- the server named the transport
 * and allocated no relay. Read as a relay, both peers dialled the host's own
 * public address with nothing listening and timed out (2026-10-09). */
/* The room list's latency before joining: our ping/pong round trip to the
 * lobby server plus the host's, which the server publishes per row. */
static void case_list_latency_estimate(void)
{
    printf("  room list latency = ours + the host's, through the server\n");
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.connected = 1;
    g_srv_rtt_ms = -1; g_srv_rtt_reported = -1; g_srv_ping_sent_ms = 0;
    handle_server_json(
        "{\"op\":\"lobby_list\",\"lobbies\":["
        "{\"lobby_id\":\"A\",\"name\":\"Near\",\"game_name\":\"G\",\"player_count\":1,"
        "\"max_slots\":2,\"host_rtt_ms\":12},"
        "{\"lobby_id\":\"B\",\"name\":\"Old server\",\"game_name\":\"G\",\"player_count\":1,"
        "\"max_slots\":2}]}");
    ck(g_lc.list_count == 2, "both rooms listed");
    ck(rnet_lobby_list_latency_estimate_ms(0) == -1, "unknown until we have measured ourselves");
    g_srv_ping_sent_ms = lobby_mono_ms() - 40;
    handle_server_json("{\"op\":\"pong\"}");
    ck(g_srv_rtt_ms >= 40 && g_srv_rtt_ms < 100, "a pong times our round trip to the server");
    ck(g_srv_rtt_reported == g_srv_rtt_ms, "and reports it, so the server can publish ours");
    ck(rnet_lobby_list_latency_estimate_ms(0) == g_srv_rtt_ms + 12, "estimate = ours + the host's");
    ck(rnet_lobby_list_latency_estimate_ms(1) == -1, "a row without host_rtt_ms stays unknown");
    ck(rnet_lobby_list_latency_estimate_ms(2) == -1, "out of range is unknown");
}

static void case_list_direct_latency(void)
{
    static const char *list_json =
        "{\"op\":\"lobby_list\",\"lobbies\":["
        "{\"lobby_id\":\"A\",\"name\":\"Open\",\"game_name\":\"G\",\"player_count\":1,"
        "\"max_slots\":2,\"host_rtt_ms\":12},"
        "{\"lobby_id\":\"P\",\"name\":\"Locked\",\"game_name\":\"G\",\"player_count\":1,"
        "\"max_slots\":2,\"has_password\":true,\"host_rtt_ms\":30}]}";
    int direct = -1;
    printf("  room list latency: the direct probe's figure, else the estimate\n");
    memset(&g_lc, 0, sizeof(g_lc));
    list_probe_reset();
    g_lc.connected = 1;
    g_srv_rtt_ms = 20;
    handle_server_json(list_json);
    ck(rnet_lobby_list_latency_ms(0, &direct) == 32 && direct == 0,
       "before a probe lands, the estimate stands in (marked as one)");
    /* What list_probe_finish leaves behind for room A. */
    g_lp[0].used = 1;
    g_lp[0].state = LP_DONE;
    g_lp[0].rtt_ms = 3;
    snprintf(g_lp[0].lobby_id, sizeof(g_lp[0].lobby_id), "%s", "A");
    ck(rnet_lobby_list_latency_ms(0, &direct) == 3 && direct == 1, "a measured room reads direct");
    ck(rnet_lobby_list_latency_ms(1, &direct) == 50 && direct == 0,
       "a password room is never probed and keeps the estimate");
    ck(rnet_lobby_list_latency_ms(1, NULL) == 50, "is_direct may be NULL");
    handle_server_json(list_json);
    ck(rnet_lobby_list_latency_ms(0, NULL) == 3,
       "the server's own list push keeps the measurement (no re-probe)");
    g_list_probe_armed = 1;
    handle_server_json("{\"op\":\"lobby_list\",\"lobbies\":[]}");
    ck(!g_list_probe_armed, "an answered request disarms: the next push probes nothing");
    ck(!g_lp[0].used, "a room that left the list forgets its figure");
    snprintf(g_lc.join.last_error, sizeof(g_lc.join.last_error), "%s", "");
    g_lp[1].used = 1;
    g_lp[1].state = LP_QUEUED;
    handle_server_json("{\"op\":\"error\",\"code\":\"unknown_op\","
                       "\"detail\":\"probe_signal\",\"ok\":false}");
    ck(g_lc.join.last_error[0] == '\0', "an older server's unknown_op is not a join error");
    ck(g_list_probe_unsupported && g_lp[1].state == LP_DONE,
       "and stops probing on that connection");
    list_probe_reset();
    g_srv_rtt_ms = -1;
}

static void case_ice_launch_with_equal_endpoints_stays_ice(void)
{
    printf("  an ICE launch with equal endpoints stays ICE\n");
    memset(&g_lc, 0, sizeof(g_lc));
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "g");
    snprintf(g_lc.my_bind, sizeof(g_lc.my_bind), "%s", "0.0.0.0:7779");
    g_lc.connected = 1;
    handle_server_json(
        "{\"op\":\"launch\",\"ok\":true,\"lobby_id\":\"L\",\"session_id\":65,"
        "\"host_endpoint\":\"216.154.76.149:7778\","
        "\"guest_endpoint\":\"216.154.76.149:7778\","
        "\"transport\":\"ice\",\"player_count\":2,\"max_slots\":2,"
        "\"match_caps\":{\"v\":1,\"input_delay\":9,\"force_input_relay\":false},"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"Host\"},"
        "{\"slot\":1,\"player_id\":\"g\",\"display_name\":\"Guest\"}]}");
    ck(g_lc.join.force_input_relay == 0, "the launch is not taken for a server relay");
    ck(strcmp(g_lc.join.bind_hostport, "0.0.0.0:0") != 0,
       "so the guest keeps its own bind instead of an ephemeral relay one");

    /* The same endpoints from a server that names no transport keep the old
     * reading: that server did rewrite both endpoints to its relay. */
    handle_server_json(
        "{\"op\":\"launch\",\"ok\":true,\"lobby_id\":\"L\",\"session_id\":66,"
        "\"host_endpoint\":\"relay.example:8777\","
        "\"guest_endpoint\":\"relay.example:8777\","
        "\"player_count\":2,\"max_slots\":2,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"Host\"},"
        "{\"slot\":1,\"player_id\":\"g\",\"display_name\":\"Guest\"}]}");
    ck(g_lc.join.force_input_relay == 1,
       "a transport-less launch with equal endpoints is still a relay");
}


/* ---- what the lift added ------------------------------------------------ */

/* A stand-in engine codec: three keys in ext, one of them free text. */
typedef struct { int wide; int margin; char tag[24]; } TestExt;

static int test_write_extra(const RNetLobbyMatchCaps *caps, char *out,
                            size_t cap, void *ctx)
{
    const TestExt *e = (const TestExt *)caps->ext.bytes;
    char tag_esc[64];
    int n;
    (void)ctx;
    rnet_lobby_json_escape(e->tag, tag_esc, sizeof(tag_esc));
    n = snprintf(out, cap, ",\"t_wide\":%s,\"t_margin\":%d,\"t_tag\":\"%s\"",
                 e->wide ? "true" : "false", e->margin, tag_esc);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

static void test_parse_extra(const char *json, RNetLobbyMatchCaps *caps,
                             void *ctx)
{
    TestExt *e = (TestExt *)caps->ext.bytes;
    (void)ctx;
    e->wide = rnet_lobby_json_get_bool(json, "t_wide", 0);
    e->margin = rnet_lobby_json_get_int(json, "t_margin", 0);
    rnet_lobby_json_get_str(json, "t_tag", e->tag, sizeof(e->tag));
}

static int test_write_too_big(const RNetLobbyMatchCaps *caps, char *out,
                              size_t cap, void *ctx)
{
    (void)caps; (void)out; (void)cap; (void)ctx;
    return -1;
}

static void case_codec_round_trips_engine_keys(void)
{
    RNetLobbyMatchCaps caps, back;
    RNetLobbyCapsCodec codec;
    TestExt *e;
    char json[RNET_LOBBY_MAX_MODS * 256 + 1024];
    char obj[RNET_LOBBY_MAX_MODS * 256 + 1024];
    printf("  engine caps codec\n");

    memset(&codec, 0, sizeof(codec));
    codec.write_extra = test_write_extra;
    codec.parse_extra = test_parse_extra;
    rnet_lobby_set_caps_codec(&codec);

    memset(&caps, 0, sizeof(caps));
    caps.valid = 1;
    caps.input_delay = 7;
    caps.rollback = 1;
    e = (TestExt *)caps.ext.bytes;
    e->wide = 1;
    e->margin = 43;
    snprintf(e->tag, sizeof(e->tag), "a\"b\\c");
    ck(append_match_caps_json(json, sizeof(json), &caps) > 0, "caps encode");
    ck(strstr(json, "\"t_margin\":43") != NULL, "the engine's key is on the wire");
    ck(strstr(json, "\"t_tag\":\"a\\\"b\\\\c\"") != NULL,
       "the engine's free text is escaped by the helper it was handed");
    ck(strstr(json, "\"widescreen\"") == NULL,
       "no SNES key is written by the library itself");
    ck(json_extract_object(json, "match_caps", obj, sizeof(obj)) != 0,
       "object extracts");
    memset(&back, 0xAB, sizeof(back));
    parse_match_caps_object(obj, &back);
    e = (TestExt *)back.ext.bytes;
    ck(back.valid && back.input_delay == 7 && back.rollback == 1,
       "generic keys survive");
    ck(e->wide == 1 && e->margin == 43, "engine keys survive");
    ck(!strcmp(e->tag, "a\"b\\c"), "engine free text survives verbatim");

    /* A caps object from a peer that sent none of the engine keys must leave
     * ext ZERO, not holding whatever the struct held before. */
    memset(&back, 0xAB, sizeof(back));
    parse_match_caps_object("{\"v\":1,\"input_delay\":4}", &back);
    e = (TestExt *)back.ext.bytes;
    ck(e->wide == 0 && e->margin == 0 && e->tag[0] == '\0',
       "absent engine keys parse to zero");

    /* A codec that cannot fit its keys withholds the whole object. */
    codec.write_extra = test_write_too_big;
    rnet_lobby_set_caps_codec(&codec);
    ck(append_match_caps_json(json, sizeof(json), &caps) == 0,
       "an engine overflow publishes nothing rather than a short object");

    rnet_lobby_set_caps_codec(NULL);
    ck(append_match_caps_json(json, sizeof(json), &caps) > 0,
       "without a codec the generic object still encodes");
}

static void case_input_prediction_only_when_published(void)
{
    RNetLobbyMatchCaps caps, back;
    char json[RNET_LOBBY_MAX_MODS * 256 + 1024];
    char obj[RNET_LOBBY_MAX_MODS * 256 + 1024];
    printf("  input_prediction\n");

    memset(&caps, 0, sizeof(caps));
    caps.valid = 1;
    caps.input_delay = 6;
    ck(append_match_caps_json(json, sizeof(json), &caps) > 0, "encode");
    ck(strstr(json, "input_prediction") == NULL,
       "an unset runway is not published (the blob stays what old clients sent)");
    ck(json_extract_object(json, "match_caps", obj, sizeof(obj)) != 0, "extract");
    parse_match_caps_object(obj, &back);
    ck(back.input_prediction == 0, "and reads back as unpublished");

    caps.input_prediction = 11;
    ck(append_match_caps_json(json, sizeof(json), &caps) > 0, "encode P");
    ck(strstr(json, "\"input_prediction\":11") != NULL, "a set runway is sent");
    ck(json_extract_object(json, "match_caps", obj, sizeof(obj)) != 0, "extract");
    parse_match_caps_object(obj, &back);
    ck(back.input_prediction == 11, "and read back");

    parse_match_caps_object("{\"input_prediction\":99}", &back);
    ck(back.input_prediction == 16, "a published runway is clamped to 16");
    parse_match_caps_object("{\"input_prediction\":1}", &back);
    ck(back.input_prediction == 2, "and to 2 from below");
}

/* Every free-text value in an outbound frame is escaped (snesrecomp 960a32c).
 * Checked on the real frames the ops queue, read back through the client's own
 * parser, because "valid JSON" and "the same string" are the two failures. */
static void case_free_text_is_escaped_on_every_op(void)
{
    const char *nasty = "Room \"1\"\\ \n\ttab";
    const char *pw = "p\"w\\d";
    char got[256];
    printf("  escaping\n");

    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = 1000;              /* "connected"; nothing is ever written: */
    g_lc.connected = 1;          /* flush_pending needs handshake_done */
    snprintf(g_lc.display_name, sizeof(g_lc.display_name), "%s", "Na\"me");
    ck(rnet_lobby_create(nasty, "Game \"X\"", "1.0", pw, "0.0.0.0:7777", NULL,
                         2) == 0, "create queues");
    ck(g_lc.pending_n == 1, "one frame queued");
    rnet_lobby_json_get_str(g_lc.pending_tx[0], "name", got, sizeof(got));
    /* json_escape drops control characters other than \n \r \t and keeps
     * those as escapes, so the name comes back exactly. */
    ck(!strcmp(got, nasty), "the room name survives the wire");
    rnet_lobby_json_get_str(g_lc.pending_tx[0], "password", got, sizeof(got));
    ck(!strcmp(got, pw), "the password survives the wire byte for byte");
    rnet_lobby_json_get_str(g_lc.pending_tx[0], "display_name", got, sizeof(got));
    ck(!strcmp(got, "Na\"me"), "the display name survives");
    rnet_lobby_json_get_str(g_lc.pending_tx[0], "game_name", got, sizeof(got));
    ck(!strcmp(got, "Game \"X\""), "the title survives");

    g_lc.pending_n = 0;
    g_lc.in_lobby = 1;
    ck(rnet_lobby_send_chat("say \"hi\"\\") == 0, "chat queues");
    rnet_lobby_json_get_str(g_lc.pending_tx[0], "text", got, sizeof(got));
    ck(!strcmp(got, "say \"hi\"\\"), "a chat line survives");

    g_lc.pending_n = 0;
    ck(rnet_lobby_join("L\"1", "x\"y", "0.0.0.0:7778") == 0, "join queues");
    rnet_lobby_json_get_str(g_lc.pending_tx[0], "lobby_id", got, sizeof(got));
    ck(!strcmp(got, "L\"1"), "the lobby id survives");
    rnet_lobby_json_get_str(g_lc.pending_tx[0], "password", got, sizeof(got));
    ck(!strcmp(got, "x\"y"), "the join password survives");
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = -1;
}

static void case_seat_ceiling_is_configured(void)
{
    RNetLobbyConfig cfg;
    printf("  configured seat ceiling\n");

    memset(&cfg, 0, sizeof(cfg));
    cfg.max_players = 4;
    cfg.platform = "n64";
    rnet_lobby_configure(&cfg);
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = 1000;
    g_lc.connected = 1;
    ck(rnet_lobby_create("r", "G", "1", "", "0.0.0.0:7777", NULL, 8) == 0,
       "create queues");
    ck(rnet_lobby_json_get_int(g_lc.pending_tx[0], "max_slots", 0) == 4,
       "an 8-seat request is clamped to the title's 4");
    g_lc.pending_n = 0;
    ck(rnet_lobby_create("r", "G", "1", "", "0.0.0.0:7777", NULL, 3) == 0,
       "create queues");
    ck(rnet_lobby_json_get_int(g_lc.pending_tx[0], "max_slots", 0) == 3,
       "3 of 4 is sent as asked");

    cfg.max_players = 2;
    rnet_lobby_configure(&cfg);
    g_lc.pending_n = 0;
    ck(rnet_lobby_create("r", "G", "1", "", "0.0.0.0:7777", NULL, 4) == 0,
       "create queues");
    ck(rnet_lobby_json_get_int(g_lc.pending_tx[0], "max_slots", 0) == 2,
       "a two-seat title never asks for more");
    ck(!strcmp(g_cfg.platform, "n64"), "the platform tag is kept for reports");
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = -1;
    cfg.max_players = 0;
    cfg.platform = NULL;
    rnet_lobby_configure(&cfg);
    ck(g_cfg.max_players == 4 && !strcmp(g_cfg.platform, "unknown"),
       "zero fields take the documented defaults");
}

static void case_legacy_env_alias(void)
{
    RNetLobbyConfig cfg;
    printf("  environment alias\n");

    unsetenv("RNET_LOBBY_URL");
    setenv("SNES_NET_LOBBY_URL", "ws://legacy:1", 1);
    memset(&cfg, 0, sizeof(cfg));
    rnet_lobby_configure(&cfg);
    ck(!strcmp(rnet_lobby_default_url(), "ws://netplay.retcomm.net:8765"),
       "without an alias the old name is not read");
    cfg.legacy_env_prefix = "SNES_NET_";
    rnet_lobby_configure(&cfg);
    ck(!strcmp(rnet_lobby_default_url(), "ws://legacy:1"),
       "with the SNES prefix registered, SNES_NET_LOBBY_URL works");
    setenv("RNET_LOBBY_URL", "ws://generic:2", 1);
    ck(!strcmp(rnet_lobby_default_url(), "ws://generic:2"),
       "the generic name wins when both are set");
    unsetenv("RNET_LOBBY_URL");
    unsetenv("SNES_NET_LOBBY_URL");

    setenv("SNES_NET_GAME_VERSION", "forced-1", 1);
    rnet_lobby_set_game_identity("G", "real+abc");
    ck(!strcmp(rnet_lobby_game_version(), "forced-1"),
       "the version override honours the alias too");
    ck(rnet_lobby_version_filter_strict() == 0,
       "and an overridden run lists unfiltered");
    unsetenv("SNES_NET_GAME_VERSION");
    rnet_lobby_set_game_identity("G", "1.2.3");
    ck(!strcmp(rnet_lobby_game_version(), "1.2.3"), "unset, the real pin");
    ck(rnet_lobby_version_filter_strict() == 1, "a plain release filters");
    cfg.legacy_env_prefix = NULL;
    rnet_lobby_configure(&cfg);
}

/* The host path end to end, from the server's own message shapes
 * (recomp-net-server src/ws_lobby.rs created / lobby_update / launch). */
static void case_host_created_update_launch(void)
{
    RNetLobbyJoinInfo out;
    printf("  host: created -> lobby_update -> launch\n");
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = 1000;
    g_lc.connected = 1;
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "h");
    snprintf(g_lc.my_bind, sizeof(g_lc.my_bind), "%s", "0.0.0.0:7777");

    handle_server_json(
        "{\"op\":\"created\",\"ok\":true,\"lobby_id\":\"L\",\"session_id\":42,"
        "\"local_slot\":0,\"host_endpoint\":\"9.9.9.9:7777\",\"guest_endpoint\":\"\","
        "\"host_player_id\":\"h\",\"player_count\":1,\"max_slots\":4,"
        "\"allow_spectators\":false,\"max_spectators\":0,\"spectator_count\":0,"
        "\"spectator_slot_base\":64,\"spectators\":[],"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\",\"ready\":false}]}");
    ck(rnet_lobby_in_lobby() && rnet_lobby_is_host(), "seated as host");
    ck(rnet_lobby_join_info()->max_slots == 4,
       "created's own max_slots is used, not a constant 2");
    ck(rnet_lobby_join_info()->player_count == 1, "one player so far");
    ck(g_lc.pending_n >= 1 && strstr(g_lc.pending_tx[g_lc.pending_n - 1],
                                     "\"op\":\"set_ready\"") != NULL,
       "the host auto-readies (older servers gate start on all_ready)");

    handle_server_json(
        "{\"op\":\"lobby_update\",\"lobby_id\":\"L\",\"session_id\":42,"
        "\"host_endpoint\":\"9.9.9.9:7777\",\"guest_endpoint\":\"8.8.8.8:7778\","
        "\"host_player_id\":\"h\",\"player_count\":3,\"max_slots\":4,\"all_ready\":true,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\",\"ready\":true},"
        "{\"slot\":1,\"player_id\":\"g1\",\"display_name\":\"G1\",\"ready\":true},"
        "{\"slot\":3,\"player_id\":\"g3\",\"display_name\":\"G3\",\"ready\":true}]}");
    ck(rnet_lobby_member_count() == 3, "three members");
    ck(rnet_lobby_all_ready() == 1, "all ready");

    handle_server_json(
        "{\"op\":\"launch\",\"ok\":true,\"lobby_id\":\"L\",\"session_id\":77,"
        "\"host_endpoint\":\"9.9.9.9:7777\",\"guest_endpoint\":\"8.8.8.8:7778\","
        "\"player_count\":3,\"max_slots\":4,\"host_spectates\":false,"
        "\"match_caps\":{\"v\":1,\"input_delay\":5,\"input_prediction\":9,\"rollback\":true},"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\"},"
        "{\"slot\":1,\"player_id\":\"g1\",\"display_name\":\"G1\"},"
        "{\"slot\":3,\"player_id\":\"g3\",\"display_name\":\"G3\"}],"
        "\"spectators\":[]}");
    ck(rnet_lobby_launch_pending() == 1, "launch pending");
    ck(rnet_lobby_try_fill_launch(&out) == 1, "the launch fills");
    ck(out.session_id == 77, "the launch's session id, not the room's");
    ck(out.local_slot == 0 && out.player_count == 3 && out.max_slots == 4,
       "seat and counts");
    ck(!strcmp(out.bind_hostport, "0.0.0.0:7777"),
       "the host binds every interface on its advertised port");
    ck(!strcmp(out.peer_hostport, "8.8.8.8:7778"), "peer from guest_endpoint");
    ck(out.host_spectates == 0, "host plays");
    ck(rnet_lobby_match_caps()->input_delay == 5 &&
       rnet_lobby_match_caps()->input_prediction == 9,
       "the start's caps are the ones the launch carries");

    handle_server_json("{\"op\":\"left\"}");
    ck(!rnet_lobby_in_lobby() && !rnet_lobby_launch_pending(),
       "leaving clears the seat and the launch");
    ck(rnet_lobby_join_info()->session_id == 0, "and the join info");
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = -1;
}

static void case_guest_joined_and_host_spectates(void)
{
    RNetLobbyJoinInfo out;
    printf("  guest: joined -> launch (host in the gallery)\n");
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = 1000;
    g_lc.connected = 1;
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", "g");
    snprintf(g_lc.my_bind, sizeof(g_lc.my_bind), "%s", "0.0.0.0:7778");

    handle_server_json(
        "{\"op\":\"joined\",\"ok\":true,\"lobby_id\":\"L\",\"session_id\":5,"
        "\"local_slot\":1,\"spectator\":false,\"spectator_slot_base\":64,"
        "\"host_endpoint\":\"1.1.1.1:7777\",\"guest_endpoint\":\"2.2.2.2:7778\"}");
    ck(rnet_lobby_in_lobby() && !rnet_lobby_is_host(), "seated as a guest");
    ck(rnet_lobby_join_info()->max_slots == 2 &&
       rnet_lobby_join_info()->player_count == 2,
       "joined carries no counts; the historic defaults stand");
    handle_server_json(
        "{\"op\":\"launch\",\"ok\":true,\"lobby_id\":\"L\",\"session_id\":6,"
        "\"host_endpoint\":\"1.1.1.1:7777\",\"guest_endpoint\":\"2.2.2.2:7778\","
        "\"player_count\":1,\"max_slots\":2,\"host_spectates\":true,"
        "\"slots\":[{\"slot\":1,\"player_id\":\"g\",\"display_name\":\"G\"}],"
        "\"spectators\":[{\"slot\":64,\"player_id\":\"h\",\"display_name\":\"H\"}]}");
    ck(rnet_lobby_try_fill_launch(&out) == 1, "fills");
    ck(out.host_spectates == 1, "host_spectates is carried to the launch");
    ck(!strcmp(out.peer_hostport, "1.1.1.1:7777"), "the guest dials the host");
    ck(!strcmp(out.bind_hostport, "0.0.0.0:7778"), "on its own advertised bind");
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = -1;
}

/* A frame is queued whole or refused -- never cut. The slot used to be 2048
 * bytes, and a create carrying a full caps object (the 4000 bytes the caps
 * encoder allows) went out truncated, which the server drops whole. */
static void case_large_frames_are_not_cut(void)
{
    RNetLobbyMatchCaps caps;
    int i;
    size_t len;
    printf("  large frames\n");
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = 1000;
    g_lc.connected = 1;
    memset(&caps, 0, sizeof(caps));
    caps.valid = 1;
    caps.input_delay = 6;
    caps.mod_count = 16;
    for (i = 0; i < 40 && strlen(caps.mod_set) + sizeof("pkg@1/feature opt=v;") <= sizeof(caps.mod_set); ++i)
        strcat(caps.mod_set, "pkg@1/feature opt=v;");
    for (i = 0; i < caps.mod_count; ++i) {
        snprintf(caps.mods[i].id, sizeof(caps.mods[i].id),
                 "some.fairly.long.package.identifier.%02d", i);
        snprintf(caps.mods[i].ver, sizeof(caps.mods[i].ver), "1.0.%d", i);
        snprintf(caps.mods[i].name, sizeof(caps.mods[i].name),
                 "A Package With A Descriptive Name %d", i);
        snprintf(caps.mods[i].feats, sizeof(caps.mods[i].feats),
                 "feature_one,feature_two");
    }
    ck(rnet_lobby_create("Big", "G", "1", "", "0.0.0.0:7777", &caps, 2) == 0,
       "create with a large plan queues");
    len = strlen(g_lc.pending_tx[0]);
    printf("    create frame is %zu bytes\n", len);
    ck(len > 2048, "the case really exceeds the old 2048-byte slot");
    ck(g_lc.pending_tx[0][len - 1] == '}', "and ends where JSON ends");
    ck(strstr(g_lc.pending_tx[0], "\"mod_cosmetic_allow\"") != NULL,
       "the caps object's last key is on the wire");
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = -1;
}

static void case_session_variant(void)
{
    RNetLobbyMatchCaps caps, back;
    char json[4096], obj[4096];
    memset(&caps, 0, sizeof(caps));
    caps.valid = 1;
    ck(append_match_caps_json(json, sizeof(json), &caps) > 0, "default caps encode");
    ck(strstr(json, "session_variant") == NULL, "default preserves legacy caps encoding");
    caps.session_variant = 37;
    ck(append_match_caps_json(json, sizeof(json), &caps) > 0, "variant caps encode");
    ck(json_extract_object(json, "match_caps", obj, sizeof(obj)), "variant caps extract");
    parse_match_caps_object(obj, &back);
    ck(back.session_variant == 37, "opaque variant round-trips online");
    parse_match_caps_object("{\"v\":1}", &back);
    ck(back.session_variant == 0, "old host resets variant to default");
}


/* ---- host relay over ICE: signalling, hold, launch ----------------------
 * This test build has no ICE (RNET_ENABLE_ICE is not defined here), so no
 * agent ever exists: everything below is the lobby client's own job -- the
 * type range, who is allowed to signal, where an early signal waits -- and
 * the real agents are exercised by host_ice_test / lobby_ice_launch_test. */

static const char *HI_UPDATE =
    "{\"op\":\"lobby_update\",\"lobby_id\":\"L\",\"session_id\":3,"
    "\"host_player_id\":\"h\",\"player_count\":3,\"max_slots\":4,"
    "\"match_caps\":{\"v\":1,\"input_delay\":5,\"relay\":\"host\",\"relay_via\":\"ice\"},"
    "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\",\"ready\":true},"
    "{\"slot\":1,\"player_id\":\"g1\",\"display_name\":\"G1\",\"ready\":true},"
    "{\"slot\":2,\"player_id\":\"g2\",\"display_name\":\"G2\",\"ready\":true}],"
    "\"spectators\":[{\"slot\":64,\"player_id\":\"sp\",\"display_name\":\"S\"}]}";

static void hi_room(const char *me, int is_host)
{
    rnet_host_ice_destroy(&g_host_ice);
    memset(&g_lc, 0, sizeof(g_lc));
    memset(&g_il, 0, sizeof(g_il));
    g_lc.fd = 1000;
    g_lc.connected = 1;
    g_lc.in_lobby = 1;
    g_lc.is_host = is_host;
    snprintf(g_lc.player_id, sizeof(g_lc.player_id), "%s", me);
    snprintf(g_lc.host_player_id, sizeof(g_lc.host_player_id), "h");
    handle_server_json(HI_UPDATE);
    host_ice_step();   /* what rnet_lobby_pump does every frame */
}

static void hi_signal(const char *from, int type, int flag)
{
    char js[256];
    snprintf(js, sizeof(js),
             "{\"op\":\"signal\",\"from_player_id\":\"%s\",\"type\":%d,\"flag\":%d,"
             "\"text\":\"v=0 x\"}", from, type, flag);
    handle_server_json(js);
}

static void case_hostice_caps_and_range(void)
{
    RNetLobbyMatchCaps caps;
    char json[1024];
    printf("  host-ice: caps field and signal range\n");
    ck(RNET_LOBBY_SIG_HOSTICE_BASE == 130, "the range starts at 130");
    ck(rnet_host_ice_sig_is_ours(131) && rnet_host_ice_sig_is_ours(136), "131..136 are ours");
    ck(!rnet_host_ice_sig_is_ours(130) && !rnet_host_ice_sig_is_ours(137) &&
       !rnet_host_ice_sig_is_ours(126) && !rnet_host_ice_sig_is_ours(102) &&
       !rnet_host_ice_sig_is_ours(6),
       "legacy 1-6, RTT 100-102 and mod transfer 110/111/121-126 are not");
    memset(&caps, 0, sizeof(caps));
    parse_match_caps_object("{\"v\":1,\"relay\":\"host\",\"relay_via\":\"ice\"}", &caps);
    ck(caps.relay_host == 1 && caps.relay_via_ice == 1, "relay_via ice parses");
    parse_match_caps_object("{\"v\":1,\"relay\":\"host\"}", &caps);
    ck(caps.relay_host == 1 && caps.relay_via_ice == 0, "absent relay_via is not ice");
    /* This build cannot run ICE, so a host never advertises it. */
    caps.relay_host = 1;
    g_relay_via_ice_pref = 1;
    ck(append_match_caps_json(json, sizeof(json), &caps) > 0, "caps encode");
    ck(strstr(json, "\"relay\":\"host\"") && !strstr(json, "relay_via"),
       "no relay_via is published by a build without ICE");
}

static void case_hostice_dispatch_and_seating(void)
{
    int sig_count_before;
    printf("  host-ice: dispatch by sender, seated players only\n");
    hi_room("h", 1);
    ck(room_relays_via_ice(), "the room's caps ask for ICE");
    sig_count_before = g_lc.sig_count;

    hi_signal("g1", 131, 1);   /* LOCAL_SDP from a seated guest */
    ck(rnet_host_ice_held_count(g_host_ice, "g1") == 1,
       "a seated guest's signal reaches the module (held: no agent in this build)");
    ck(g_lc.sig_count == sig_count_before, "and does not enter the legacy game queue");
    hi_signal("g2", 133, 1);
    ck(rnet_host_ice_held_count(g_host_ice, "g2") == 1 &&
       rnet_host_ice_held_count(g_host_ice, "g1") == 1,
       "each sender has its own hold");

    hi_signal("stranger", 131, 1);
    ck(rnet_host_ice_held_count(g_host_ice, "stranger") == 0, "an unseated sender is refused");
    hi_signal("sp", 131, 1);
    ck(rnet_host_ice_held_count(g_host_ice, "sp") == 0, "a gallery spectator is refused");
    hi_signal("g1", 136, 1);
    ck(rnet_host_ice_held_count(g_host_ice, "g1") == 1, "SET_CONTROLLING is refused");
    hi_signal("g1", 120 + 1, 0);
    ck(rnet_host_ice_held_count(g_host_ice, "g1") == 1 &&
       rnet_sig_hold_count(&g_lc.sig_hold, "g1") == 1,
       "a mod-transfer signal still takes the mod path, not ours");
    hi_signal("g1", 1, 0);
    ck(g_lc.sig_count == sig_count_before + 1 || g_lc.sig_count == sig_count_before,
       "legacy types are untouched (queued for the game, or dropped by its own rule)");

    /* A guest hears the host and nobody else. */
    hi_room("g1", 0);
    hi_signal("g2", 131, 1);
    ck(rnet_host_ice_held_count(g_host_ice, "g2") == 0, "a guest refuses another guest");
    hi_signal("h", 131, 1);
    ck(rnet_host_ice_held_count(g_host_ice, "h") == 1, "a guest accepts the host");
    {
        RNetHostIceStatus st;
        ck(rnet_lobby_host_ice_status(&st) == 1 && st.role == 2, "the guest role is running");
        ck(st.reports_sent == 1 && !strcmp(st.last_report, "fail"),
           "a build that cannot run ICE reports path \"fail\" honestly");
    }
    rnet_host_ice_destroy(&g_host_ice);
}

static void case_hostice_hold_is_per_peer_for_mod_signals(void)
{
    printf("  mod-transfer hold: one bucket per sender\n");
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = 1000;
    g_lc.connected = 1;
    hi_signal("g1", 121, 0);
    hi_signal("g2", 121, 0);
    hi_signal("g1", 123, 0);
    ck(rnet_sig_hold_count(&g_lc.sig_hold, "g1") == 2 &&
       rnet_sig_hold_count(&g_lc.sig_hold, "g2") == 1,
       "a second sender no longer wipes the first sender's held signals");
    ck(rnet_sig_hold_get(&g_lc.sig_hold, "g1", 0)->sig.type == RNET_SIGNAL_LOCAL_SDP ||
       rnet_sig_hold_get(&g_lc.sig_hold, "g1", 0)->sig.type == RNET_SIGNAL_REMOTE_SDP,
       "in arrival order");
    rnet_sig_hold_drop(&g_lc.sig_hold, "g1");
    ck(rnet_sig_hold_count(&g_lc.sig_hold, "g1") == 0 &&
       rnet_sig_hold_count(&g_lc.sig_hold, "g2") == 1,
       "consuming one peer's hold leaves the other's");
    rnet_sig_hold_clear(&g_lc.sig_hold);
}

static void case_hostice_launch_without_agents_is_refused(void)
{
    const char *launch =
        "{\"op\":\"launch\",\"ok\":true,\"lobby_id\":\"L\",\"session_id\":9,"
        "\"transport\":\"host\",\"relay_via\":\"ice\",\"host_endpoint\":\"\","
        "\"guest_endpoint\":\"\",\"player_count\":3,\"max_slots\":4,"
        "\"match_caps\":{\"v\":1,\"input_delay\":5,\"relay\":\"host\",\"relay_via\":\"ice\"},"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\"},"
        "{\"slot\":1,\"player_id\":\"g1\",\"display_name\":\"G1\"},"
        "{\"slot\":2,\"player_id\":\"g2\",\"display_name\":\"G2\"}]}";
    printf("  host-ice: a launch no agent backs is refused with a reason\n");
    hi_room("h", 1);
    handle_server_json(launch);
    ck(g_lc.join.transport_ice_hub == 1 && g_lc.join.transport_host == 1,
       "the launch is read as ICE hub");
    ck(rnet_lobby_launch_pending() == 0, "not launched");
    ck(!strcmp(g_lc.join.last_error, "ice_not_connected"), "last_error says why");
    ck(strstr(rnet_lobby_ice_launch_error(), "G1") != NULL,
       "and the reason names the guest that is not connected");
    ck(rnet_lobby_ice_take_hub((RNetLobbyIceSeat[2]){{0}}, 2) == -1,
       "there is nothing to take");
    ck(rnet_lobby_ice_take_guest_agent() == NULL, "nor as a guest");
    /* the legacy launch path is untouched */
    snprintf(g_lc.my_bind, sizeof(g_lc.my_bind), "%s", "0.0.0.0:7777");
    handle_server_json(
        "{\"op\":\"launch\",\"ok\":true,\"lobby_id\":\"L\",\"session_id\":9,"
        "\"transport\":\"host\",\"host_endpoint\":\"9.9.9.9:7777\","
        "\"guest_endpoint\":\"\",\"player_count\":2,\"max_slots\":4,"
        "\"slots\":[{\"slot\":0,\"player_id\":\"h\",\"display_name\":\"H\"},"
        "{\"slot\":1,\"player_id\":\"g1\",\"display_name\":\"G1\"}]}");
    ck(g_lc.join.transport_ice_hub == 0 && g_lc.join.transport_host == 1,
       "a launch without relay_via keeps the endpoint path");
    ck(rnet_lobby_launch_pending() == 1, "and launches");
    rnet_host_ice_destroy(&g_host_ice);
    memset(&g_lc, 0, sizeof(g_lc));
    g_lc.fd = -1;
}

int main(void)
{
    case_rows();
    case_empty();
    case_old_string_encoding_is_not_revived();
    case_partial_rows_dropped();
    case_overflow_refuses();
    case_gate_matches_on_id_only();
    case_gate_holds_for_missing_files();
    case_launch_blocked_holds_ready();
    case_hooks_survive_disconnect();
    case_ice_local_becomes_remote();
    case_offer_round_trips_through_a_slot_row();
    case_mod_set_round_trips();
    case_relay_cap_is_five_mb();
    case_direct_paths_are_never_capped();
    case_unknown_path_allows();
    case_refusal_tells_the_player_what_to_do();
    case_refusal_survives_a_short_buffer();
    case_spectator_rows_are_tagged();
    case_a_player_is_not_a_spectator();
    case_promotion_flips_the_role();
    case_a_server_without_spectators_reads_as_before();
    case_launch_does_not_erase_the_gallery();
    case_gallery_seat_addressing();
    case_the_gallery_does_not_negotiate();
    case_launch_transport_survives_a_lobby_update();
    case_ice_launch_with_equal_endpoints_stays_ice();
    case_list_latency_estimate();
    case_list_direct_latency();
    case_gallery_does_not_rearm_ready();
    case_chat_ring_keeps_room_order();
    case_chat_ring_wraps_oldest_first();
    case_chat_ignores_empty_and_clears();
    case_codec_round_trips_engine_keys();
    case_input_prediction_only_when_published();
    case_session_variant();
    case_free_text_is_escaped_on_every_op();
    case_seat_ceiling_is_configured();
    case_legacy_env_alias();
    case_host_created_update_launch();
    case_guest_joined_and_host_spectates();
    case_large_frames_are_not_cut();
    case_hostice_caps_and_range();
    case_hostice_dispatch_and_seating();
    case_hostice_hold_is_per_peer_for_mod_signals();
    case_hostice_launch_without_agents_is_refused();
    printf(fails ? "\n%d failure(s)\n" : "\nall lobby client cases passed\n", fails);
    return fails != 0;
}
