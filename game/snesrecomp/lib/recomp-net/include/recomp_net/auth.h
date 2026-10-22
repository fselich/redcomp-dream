/* recomp_net/auth.h -- Discord sign-in and the device key, for the launcher.
 *
 * ONE copy, shared by every runner. This lived twice -- psx_netplay_auth.c and
 * snes_netplay_auth.c -- and the duplication cost a real bug within a day: a
 * URL screen that refused every OAuth authorize URL had to be found and fixed
 * in both. It belongs here, where both runners already vendor it.
 *
 * Backs recomp-ui's optional `account_*` netplay callbacks. Everything here is
 * optional at runtime: a lobby server whose operator never configured Discord
 * answers 503 to a login start, and this reports "not available" so the
 * launcher draws no sign-in at all. A player who never signs in is a guest,
 * which is what the runtime has always been.
 *
 * Two credentials, and the difference matters:
 *
 *   netplay_secret  Long-lived, per device, written to a file beside the
 *                   config. NEVER sent anywhere. Proved by HMAC over a
 *                   server nonce, because no runner has TLS yet and a
 *                   permanent bearer token in cleartext would be captured
 *                   once and reused forever.
 *   session         Short-lived JWT, minted from that proof, and the only
 *                   thing that goes to the lobby (in `hello`).
 *
 * The secret file is what makes a browserless device work: sign in once on a
 * PC, and the file travels with a build installed on a handheld. It is a
 * password in a file -- do not put it in anything you distribute.
 */
#ifndef RECOMP_NET_AUTH_H
#define RECOMP_NET_AUTH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors recomp-ui's RECOMP_LAUNCHER_ACCOUNT_* so this header does not
 * depend on the launcher's. */
enum {
    RNET_ACCOUNT_GUEST = 0,
    RNET_ACCOUNT_WAITING = 1,
    RNET_ACCOUNT_SIGNED_IN = 2,
    RNET_ACCOUNT_FAILED = 3
};

/*
 * Where the secret file lives. Call BEFORE rnet_account_init, once, with an
 * absolute path.
 *
 * Without this the path is the bare relative name "netplay_secret", which
 * resolves against the CURRENT WORKING DIRECTORY -- so the same installed
 * build signs itself out depending on where it was launched from, and a
 * rebuild that runs from a different directory looks like a lost login. Hosts
 * that know their own executable directory should say so here; that is the
 * "travels with the build" behaviour this file's header describes.
 *
 * If the configured path does not exist but a legacy CWD-relative
 * "netplay_secret" does, the legacy one is read and then migrated to the
 * configured path, so turning this on does not sign anyone out.
 */
void rnet_account_set_secret_path(const char *path);

/* `ws_url` is the lobby URL the client is configured with; the HTTP endpoints
 * live on the same host and port. Safe to call again when the URL changes. */
void rnet_account_init(const char *ws_url);

/* Call once per frame. Drives the worker handshake and, when a stored key
 * exists, redeems it for a session on first use. */
void rnet_account_pump(void);

int  rnet_account_available(void);
int  rnet_account_login_begin(void);
int  rnet_account_state(void);
const char *rnet_account_handle(void);
const char *rnet_account_username(void);
const char *rnet_account_error(void);
int  rnet_account_sign_out(void);
int  rnet_account_set_handle(const char *handle);

/* The session for `hello`, or "" when this client is a guest. */
const char *rnet_account_session(void);

#ifdef __cplusplus
}
#endif

#endif /* RECOMP_NET_AUTH_H */
