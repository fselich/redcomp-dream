/* Browser handoff for Discord sign-in. The URL is always one argv element;
 * OAuth query strings must never pass through a shell. */
#include "rnet_open_url.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* Locate the executable before forking: the child of a multithreaded game
 * must do only async-signal-safe work before execve. */
static int find_opener(const char *name, char *out, size_t cap) {
    const char *path = getenv("PATH");
    const char *part;
    if (!path) path = "/usr/local/bin:/usr/bin:/bin";
    part = path;
    while (*part) {
        const char *end = strchr(part, ':');
        size_t len = end ? (size_t)(end - part) : strlen(part);
        if (len > 0) {
            int n = snprintf(out, cap, "%.*s/%s", (int)len, part, name);
            if (n > 0 && (size_t)n < cap && access(out, X_OK) == 0)
                return 1;
        }
        if (!end) break;
        part = end + 1;
    }
    return 0;
}

#if !defined(__APPLE__)
static const char *const appimage_drop[] = {
    "LD_LIBRARY_PATH=", "LD_PRELOAD=", "LD_AUDIT=",
    "GTK_PATH=", "GTK_EXE_PREFIX=", "GTK_DATA_PREFIX=", "GTK_IM_MODULE_FILE=",
    "GDK_PIXBUF_MODULE_FILE=", "GDK_PIXBUF_MODULEDIR=", "GIO_MODULE_DIR=",
    "GSETTINGS_SCHEMA_DIR=", "GST_PLUGIN_SYSTEM_PATH=",
    "GST_PLUGIN_SYSTEM_PATH_1_0=", "GST_PLUGIN_PATH=", "QT_PLUGIN_PATH=",
    "QML2_IMPORT_PATH=", "QML_IMPORT_PATH=", "FONTCONFIG_FILE=",
    "FONTCONFIG_PATH=", "PYTHONHOME=", "PYTHONPATH=", "PERLLIB=",
    "PERL5LIB=", "APPDIR=", "APPIMAGE=", "ARGV0=", "OWD=",
    "RECOMP_HOST_LD_LIBRARY_PATH="
};

static int drop_appimage_var(const char *entry) {
    size_t i;
    for (i = 0; i < sizeof(appimage_drop) / sizeof(appimage_drop[0]); ++i) {
        size_t len = strlen(appimage_drop[i]);
        if (strncmp(entry, appimage_drop[i], len) == 0) return 1;
    }
    return 0;
}

/* A Linux AppImage adds its own libraries to LD_LIBRARY_PATH. Letting the
 * system xdg-open/wslview inherit those can crash its GTK/Qt dependencies.
 * Keep the host's original path when AppRun provided it. */
static char **host_environment(void) {
    size_t count = 0, used = 0;
    char **out, **entry;
    const char *host_path;
    if (!getenv("APPIMAGE") && !getenv("APPDIR") &&
        !getenv("RECOMP_HOST_LD_LIBRARY_PATH"))
        return environ;
    for (entry = environ; *entry; ++entry) ++count;
    out = (char **)calloc(count + 2, sizeof(*out));
    if (!out) return NULL;
    for (entry = environ; *entry; ++entry) {
        if (!drop_appimage_var(*entry)) out[used++] = *entry;
    }
    host_path = getenv("RECOMP_HOST_LD_LIBRARY_PATH");
    if (host_path && *host_path) {
        size_t len = strlen("LD_LIBRARY_PATH=") + strlen(host_path) + 1;
        char *saved = (char *)malloc(len);
        if (!saved) { free(out); return NULL; }
        snprintf(saved, len, "LD_LIBRARY_PATH=%s", host_path);
        out[used++] = saved;
    }
    return out;
}

static void free_host_environment(char **env) {
    if (env && env != environ) {
        size_t i;
        for (i = 0; env[i]; ++i) {
            if (strncmp(env[i], "LD_LIBRARY_PATH=", 16) == 0) free(env[i]);
        }
        free(env);
    }
}
#endif

/* An opener can exec successfully and then fail (for example, xdg-open exits
 * 3 because no browser is configured). Give it a short window to report that
 * failure so the next opener can be tried. An opener that stays alive beyond
 * the window may be the browser itself; treating it as started avoids opening
 * the same URL twice. The detached supervisor reaps it when it eventually
 * exits, so the game does not accumulate zombies. */
static int launch(const char *program, const char *url, char *const env[],
                  int gio_open) {
    int report[2], status, result = -1;
    ssize_t got;
    pid_t child, waited;
    struct pollfd ready;
    char *const simple_argv[] = { (char *)program, (char *)url, NULL };
    char *const gio_argv[] = { (char *)program, "open", (char *)url, NULL };
    char *const *argv = gio_open ? gio_argv : simple_argv;
    if (pipe(report) != 0) return 0;
    if (fcntl(report[1], F_SETFD, FD_CLOEXEC) != 0) {
        close(report[0]); close(report[1]); return 0;
    }
    child = fork();
    if (child == 0) {
        pid_t supervisor;
        close(report[0]);
        /* The supervisor is reparented; it waits for the opener after the
         * game has moved on, even if that opener stays alive for hours. */
        supervisor = fork();
        if (supervisor == 0) {
            pid_t opener = fork();
            if (opener == 0) {
                execve(program, argv, env);
                _exit(127);
            }
            if (opener > 0) {
                do { waited = waitpid(opener, &status, 0); }
                while (waited < 0 && errno == EINTR);
                if (waited == opener && WIFEXITED(status))
                    result = WEXITSTATUS(status);
            }
            (void)write(report[1], &result, sizeof(result));
            _exit(0);
        }
        if (supervisor < 0)
            (void)write(report[1], &result, sizeof(result));
        _exit(supervisor < 0 ? 127 : 0);
    }
    close(report[1]);
    if (child < 0) { close(report[0]); return 0; }
    status = 0;
    do { waited = waitpid(child, &status, 0); }
    while (waited < 0 && errno == EINTR);
    if (waited != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        close(report[0]);
        return 0;
    }
    ready.fd = report[0];
    ready.events = POLLIN | POLLHUP;
    do { status = poll(&ready, 1, 2000); }
    while (status < 0 && errno == EINTR);
    if (status == 0) { close(report[0]); return 1; }
    got = status > 0 ? read(report[0], &result, sizeof(result)) : -1;
    close(report[0]);
    return got == (ssize_t)sizeof(result) && result == 0;
}
#endif

int rnet_open_url(const char *url) {
    if (!url || (strncmp(url, "http://", 7) != 0 &&
                 strncmp(url, "https://", 8) != 0)) {
        fprintf(stderr, "rnet_account: refusing to open a non-http(s) URL\n");
        return 0;
    }
#if defined(_WIN32)
    if ((INT_PTR)ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL) > 32)
        return 1;
#else
    {
        char opener[4096];
        char **env;
#if defined(__APPLE__)
        if (!find_opener("open", opener, sizeof(opener))) goto fail;
        env = environ;
        if (launch(opener, url, env, 0)) return 1;
#else
        static const char *const linux_openers[] = {
            "xdg-open", "gio", "sensible-browser", "wslview"
        };
        size_t i;
        int wsl = getenv("WSL_INTEROP") != NULL;
        /* wslview is the WSL bridge to the Windows default browser. A WSL
         * installation can also contain xdg-open without a usable Linux
         * browser, so prefer the bridge when WSL_INTEROP is available. */
        env = host_environment();
        if (!env) goto fail;
        if (wsl && find_opener("wslview", opener, sizeof(opener))) {
            if (launch(opener, url, env, 0)) {
                free_host_environment(env);
                return 1;
            }
            fprintf(stderr, "rnet_account: wslview failed; trying another browser opener\n");
        }
        for (i = 0; i < sizeof(linux_openers) / sizeof(linux_openers[0]); ++i) {
            const char *name = linux_openers[i];
            if (wsl && strcmp(name, "wslview") == 0) continue;
            if (!find_opener(name, opener, sizeof(opener))) continue;
            if (launch(opener, url, env, strcmp(name, "gio") == 0)) {
                free_host_environment(env);
                return 1;
            }
            fprintf(stderr, "rnet_account: %s failed; trying another browser opener\n", name);
        }
        free_host_environment(env);
#endif
    }
#endif
#if !defined(_WIN32)
fail:
#endif
    fprintf(stderr, "rnet_account: could not launch a browser; visit:\n  %s\n", url);
    return 0;
}
