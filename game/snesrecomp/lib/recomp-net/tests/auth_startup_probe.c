/* First account request, without a lobby/debug socket or a real browser. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "recomp_net/auth.h"
#include <stdio.h>
#include <stdatomic.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

static atomic_int browser_requested;

int rnet_open_url(const char *url)
{
    if (url && url[0]) atomic_store(&browser_requested, 1);
    return 0; /* Finish the worker without opening or completing sign-in. */
}

int main(int argc, char **argv)
{
    int n;
    if (argc != 2) return 2;
    rnet_account_set_secret_path("unused-isolated-secret");
    rnet_account_init(argv[1]);
    if (rnet_account_login_begin() != 0) return 3;
    for (n = 0; n < 1000 && rnet_account_state() == RNET_ACCOUNT_WAITING; ++n) {
#if defined(_WIN32)
        Sleep(10);
#else
        struct timespec delay = {0, 10000000};
        nanosleep(&delay, NULL);
#endif
    }
    if (!atomic_load(&browser_requested)) {
        fprintf(stderr, "First account HTTP request failed: %s\n", rnet_account_error());
        return 1;
    }
    return 0;
}
