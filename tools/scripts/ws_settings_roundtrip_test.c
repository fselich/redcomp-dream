/* Round-trip harness for WsSettingsSave. Built and run by
 * test_ws_settings_roundtrip.sh; not part of the game target. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "config.h"
#include "ws_frame.h"
#include "ws_settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Config g_config;

const char *WsEnvStr(const char *name) { return getenv(name); }

int WsCiEq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++, b++;
    }
    return *a == *b;
}

int WsParseBool(const char *v, int def)
{
    if (WsCiEq(v, "1") || WsCiEq(v, "true") || WsCiEq(v, "on") ||
        WsCiEq(v, "yes"))
        return 1;
    if (WsCiEq(v, "0") || WsCiEq(v, "false") || WsCiEq(v, "off") ||
        WsCiEq(v, "no"))
        return 0;
    return def;
}

int WsConfigReadStr(const char *path, const char *section, const char *key,
                    char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int in_section = 0, found = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *hash = strpbrk(line, ";#");
        if (hash) *hash = 0;
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        char *e = p + strlen(p);
        while (e > p && isspace((unsigned char)e[-1])) e--;
        *e = 0;
        if (!*p) continue;
        if (*p == '[') {
            in_section = WsCiEq(p, section);
            continue;
        }
        if (!in_section) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = p;
        char *ke = k + strlen(k);
        while (ke > k && isspace((unsigned char)ke[-1])) ke--;
        *ke = 0;
        if (WsCiEq(k, key)) {
            char *v = eq + 1;
            while (*v && isspace((unsigned char)*v)) v++;
            char *ve = v + strlen(v);
            while (ve > v && isspace((unsigned char)ve[-1])) ve--;
            *ve = 0;
            snprintf(out, cap, "%s", v);
            found = 1;
        }
    }
    fclose(f);
    return found;
}

void WsOwReset(void) {}

static char *ReadAll(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)(n > 0 ? n : 0) + 1);
    if (buf && n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf) buf[n > 0 ? n : 0] = 0;
    fclose(f);
    return buf;
}

static int Fail(const char *what)
{
    fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

int main(void)
{
    const char *before =
        "; hand-written comment\n"
        "[Graphics]\n"
        "Widescreen = 0\n"
        "Fullscreen = 0\n"
        "\n"
        "[Video]\n"
        "MiniMap = 1\n"
        "UnknownKey = keep me\n"
        "\n"
        "[KeyMap]\n"
        "Load = F1, F2, F3\n";
    FILE *f = fopen("config.ini", "w");
    if (!f) return Fail("write fixture");
    fputs(before, f);
    fclose(f);

    WsSettingsLoad();
    if (!WsSettingsSetValue(WS_SET_MINIMAP, "0")) return Fail("set MiniMap");
    if (WsSettingsSave() <= 0) return Fail("save MiniMap");

    char *after = ReadAll("config.ini");
    if (!after) return Fail("read result");
    const char *expected =
        "; hand-written comment\n"
        "[Graphics]\n"
        "Widescreen = 0\n"
        "Fullscreen = 0\n"
        "\n"
        "[Video]\n"
        "MiniMap = 0\n"
        "UnknownKey = keep me\n"
        "\n"
        "[KeyMap]\n"
        "Load = F1, F2, F3\n";
    if (strstr(after, "[Widescreen]")) return Fail("[Widescreen] appeared");
    if (strcmp(after, expected) != 0) {
        fprintf(stderr, "--- got ---\n%s--- expected ---\n%s", after, expected);
        return Fail("round trip changed more than MiniMap");
    }
    free(after);

    /* A file without [Widescreen]: saving Aspect appends the section. */
    f = fopen("config.ini", "w");
    if (!f) return Fail("rewrite fixture");
    fputs("; top\n[Video]\nMiniMap = 0\n", f);
    fclose(f);
    WsSettingsLoad();
    if (!WsSettingsSetValue(WS_SET_ASPECT, "21:9")) return Fail("set Aspect");
    if (WsSettingsSave() <= 0) return Fail("save Aspect");
    after = ReadAll("config.ini");
    if (!after) return Fail("read section result");
    if (!strstr(after, "[Widescreen]\nAspect = 21:9\n"))
        return Fail("missing new [Widescreen] section");
    if (!strstr(after, "; top\n[Video]\nMiniMap = 0\n"))
        return Fail("existing lines changed");
    free(after);

    /* Precedence and locking. */
    f = fopen("config.ini", "w");
    fputs("[Video]\nMiniMap = 1\n[Widescreen]\nHud = 1\nMargin = ring\n"
          "Aspect = 21:9\nMaxWidth = 900\n", f);
    fclose(f);
    remove("config.local.ini");
    setenv("SNESRECOMP_WS_MINIMAP", "0", 1);
    WsSettingsLoad();
    if (WsSettingBool(WS_SET_MINIMAP) != 0) return Fail("env minimap");
    if (WsSettingSourceOf(WS_SET_MINIMAP) != WS_SOURCE_ENV)
        return Fail("minimap source");
    if (!WsSettingLocked(WS_SET_MINIMAP)) return Fail("minimap locked");
    if (WsSettingInt(WS_SET_MAX_WIDTH) != 512) return Fail("MaxWidth clamp");
    if (strcmp(WsSettingValue(WS_SET_ASPECT), "21:9") != 0)
        return Fail("Aspect from config");
    unsetenv("SNESRECOMP_WS_MINIMAP");

    f = fopen("config.local.ini", "w");
    fputs("[Widescreen]\nHud = 0\n", f);
    fclose(f);
    WsSettingsLoad();
    if (WsSettingBool(WS_SET_HUD) != 0) return Fail("local HUD");
    if (WsSettingSourceOf(WS_SET_HUD) != WS_SOURCE_LOCAL)
        return Fail("local source");
    remove("config.local.ini");

    setenv("SNESRECOMP_WS_TRACK_CLAMP", "1", 1);
    WsSettingsLoad();
    if (strcmp(WsSettingValue(WS_SET_MARGIN), "clamp") != 0)
        return Fail("legacy track clamp");
    if (WsSettingSourceOf(WS_SET_MARGIN) != WS_SOURCE_ENV)
        return Fail("legacy source");
    unsetenv("SNESRECOMP_WS_TRACK_CLAMP");

    g_config.widescreen = 1;
    setenv("SNESRECOMP_WS", "0", 1);
    WsSettingsLoad();
    if (WsSettingBool(WS_SET_WIDESCREEN) != 0) return Fail("widescreen env");
    unsetenv("SNESRECOMP_WS");

    printf("ok: settings precedence and writer round trip\n");
    return 0;
}
