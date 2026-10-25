/* The single settings model for the port's user-facing options.
 *
 * See ws_settings.h. The table below is the one place that knows an option's
 * config.ini key, environment variable, values, default and how it applies.
 * WsSettingsLoad() resolves the precedence env > config.local.ini >
 * config.ini > default and records where each value came from; modules read
 * the resolved value instead of calling getenv or opening the config.
 */
#include "ws_settings.h"

#include "config.h"
#include "ws_frame.h"
#include "ws_margin.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const kBoolValues[] = { "0", "1" };
static const char *const kBoolLabels[] = { "OFF", "ON" };
static const char *const kAspectValues[] = { "auto", "16:9", "21:9", "32:9" };
static const char *const kAspectLabels[] = { "AUTO", "16:9", "21:9", "32:9" };
static const int kMaxWidthPresets[] = { 384, 448, 512 };
static const int kVolumePresets[] = {
    0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100
};
static const char *const kMarginValues[] = { "source", "clamp" };
static const char *const kMarginLabels[] = { "SOURCE", "CLAMP" };
static const char *const kFullscreenValues[] = { "0", "1", "2" };
static const char *const kFullscreenLabels[] = {
    "WINDOWED", "BORDERLESS", "EXCLUSIVE"
};
static const char *const kPixelValues[] = { "0", "1", "2" };
static const char *const kPixelLabels[] = { "CRT 4:3", "8:7", "1:1" };

#define BOOL_DESC(id, label, section, key, env, def, applies, host) \
    { id, label, section, key, env, NULL, WS_KIND_BOOL, \
      kBoolValues, kBoolLabels, NULL, 2, 0, 0, def, applies, host }
#define ENUM_DESC(id, label, section, key, env, legacy, vals, labs, n, def, \
                  applies, host) \
    { id, label, section, key, env, legacy, WS_KIND_ENUM, \
      vals, labs, NULL, n, 0, 0, def, applies, host }

static const WsSettingDesc kSettings[WS_SET_COUNT] = {
    BOOL_DESC(WS_SET_WIDESCREEN, "WIDESCREEN", "Graphics", "Widescreen",
              "SNESRECOMP_WS", "1", WS_APPLY_LIVE, 1),
    ENUM_DESC(WS_SET_ASPECT, "ASPECT", "Widescreen", "Aspect",
              "SNESRECOMP_WS_ASPECT", NULL, kAspectValues, kAspectLabels, 4,
              "auto", WS_APPLY_LIVE, 0),
    { WS_SET_MAX_WIDTH, "MAX WIDTH", "Widescreen", "MaxWidth", NULL, NULL,
      WS_KIND_INT, NULL, NULL, kMaxWidthPresets, 3, 342, 512, "512",
      WS_APPLY_LIVE, 0 },
    ENUM_DESC(WS_SET_MARGIN, "TRACK MARGIN", "Widescreen", "Margin",
              "SNESRECOMP_WS_OFFWINDOW", "SNESRECOMP_WS_TRACK_CLAMP",
              kMarginValues, kMarginLabels, 2, "source", WS_APPLY_LIVE, 0),
    BOOL_DESC(WS_SET_HUD, "EXPANDED HUD", "Widescreen", "Hud",
              "SNESRECOMP_WS_HUD", "1", WS_APPLY_LIVE, 0),
    BOOL_DESC(WS_SET_CULL, "RIVAL IN MARGINS", "Widescreen", "Cull",
              "SNESRECOMP_WS_CULL", "1", WS_APPLY_LIVE, 0),
    ENUM_DESC(WS_SET_FULLSCREEN, "FULLSCREEN", "Graphics", "Fullscreen",
              NULL, NULL, kFullscreenValues, kFullscreenLabels, 3, "0",
              WS_APPLY_LIVE, 1),
    ENUM_DESC(WS_SET_PIXEL_ASPECT, "PIXEL ASPECT", "Graphics",
              "DisplayAspect", NULL, NULL, kPixelValues, kPixelLabels, 3, "0",
              WS_APPLY_LIVE, 1),
    { WS_SET_VOLUME, "VOLUME", "Sound", "Volume", NULL, NULL,
      WS_KIND_INT, NULL, NULL, kVolumePresets, 11, 0, 100, "100",
      WS_APPLY_LIVE, 1 },
    BOOL_DESC(WS_SET_MINIMAP, "MINIMAP", "Video", "MiniMap",
              "SNESRECOMP_WS_MINIMAP", "1", WS_APPLY_LIVE, 0),
    BOOL_DESC(WS_SET_UNLOCK_TOURS, "UNLOCK ALL TOURS", "Emulation",
              "UnlockAllTours", "SNESRECOMP_UNLOCK_TOURS", "0",
              WS_APPLY_PERMANENT_NOTE, 0),
};

typedef struct {
    char value[64];
    WsSettingSource source;
} WsSettingRuntime;

static WsSettingRuntime s_values[WS_SET_COUNT];
static int s_changed[WS_SET_COUNT];


const WsSettingDesc *WsSetting(WsSettingId id)
{
    if (id < 0 || id >= WS_SET_COUNT) return NULL;
    return &kSettings[id];
}

int WsSettingsCount(void) { return WS_SET_COUNT; }

static int FindChoice(const WsSettingDesc *d, const char *value)
{
    if (!d || !d->values) return -1;
    for (int i = 0; i < d->num_choices; i++)
        if (WsCiEq(d->values[i], value)) return i;
    return -1;
}

/* Canonical spelling of `raw` for the descriptor. When the value is not one
 * of the listed choices an Aspect keeps the file's spelling (a decimal, say);
 * every other option falls back to its default. */
static void Canonicalize(const WsSettingDesc *d, const char *raw,
                         char *out, size_t cap)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", raw ? raw : "");
    const char *v = WsTrim(buf);

    if (!*v) {
        snprintf(out, cap, "%s", d->default_value);
        return;
    }

    switch (d->kind) {
    case WS_KIND_BOOL: {
        int def = WsParseBool(d->default_value, 0);
        int b = WsParseBool(v, def);
        snprintf(out, cap, "%d", b != 0);
        return;
    }
    case WS_KIND_INT: {
        long n = strtol(v, NULL, 10);
        if (n < d->min_value) n = d->min_value;
        if (n > d->max_value) n = d->max_value;
        snprintf(out, cap, "%ld", n);
        return;
    }
    case WS_KIND_ENUM:
    default: {
        int idx = FindChoice(d, v);
        if (idx >= 0) {
            snprintf(out, cap, "%s", d->values[idx]);
            return;
        }
        if (d->id == WS_SET_ASPECT) {
            snprintf(out, cap, "%s", v);
            return;
        }
        /* Values of the removed policies: `wrap`/`0`/`off` asked for no
         * fill, the closest remaining policy is clamp; `ring`, `ghost` and
         * anything else fall through to the default (source). */
        if (d->id == WS_SET_MARGIN &&
            (WsCiEq(v, "wrap") || WsCiEq(v, "0") || WsCiEq(v, "off"))) {
            snprintf(out, cap, "clamp");
            return;
        }
        snprintf(out, cap, "%s", d->default_value);
        return;
    }
    }
}

static void Store(WsSettingId id, const char *raw, WsSettingSource source)
{
    const WsSettingDesc *d = &kSettings[id];
    Canonicalize(d, raw, s_values[id].value, sizeof(s_values[id].value));
    s_values[id].source = source;
}

/* The host's default config text has no [Graphics] Widescreen key, so a
 * fresh install would otherwise boot in 4:3. Widescreen is this port's
 * default; an explicit key in config.ini/config.local.ini or SNESRECOMP_WS
 * (including 0) still wins. */
static int WidescreenConfigured(void)
{
    char buf[16];
    if (WsConfigReadStr("config.ini", "[Graphics]", "Widescreen", buf,
                        sizeof(buf)))
        return 1;
    if (WsConfigReadStr("config.local.ini", "[Graphics]", "Widescreen", buf,
                        sizeof(buf)))
        return 1;
    {
        const char *e = WsEnvStr("SNESRECOMP_WS");
        if (e && *e) return 1;
    }
    return 0;
}

void WsSettingsLoad(void)
{
    char buf[128];

    if (!WidescreenConfigured()) g_config.widescreen = true;

    memset(s_changed, 0, sizeof(s_changed));
    for (int i = 0; i < WS_SET_COUNT; i++) {
        const WsSettingDesc *d = &kSettings[i];
        Store(d->id, d->default_value, WS_SOURCE_DEFAULT);
        if (d->host_owned) continue;

        char section[32];
        snprintf(section, sizeof(section), "[%s]", d->section);
        if (WsConfigReadStr("config.ini", section, d->key, buf,
                            sizeof(buf)) && buf[0])
            Store(d->id, buf, WS_SOURCE_CONFIG);
        if (WsConfigReadStr("config.local.ini", section, d->key, buf,
                            sizeof(buf)) && buf[0])
            Store(d->id, buf, WS_SOURCE_LOCAL);

        if (d->env) {
            const char *e = WsEnvStr(d->env);
            if (e && *e) {
                Store(d->id, e, WS_SOURCE_ENV);
                continue;
            }
        }
        if (d->legacy_env) {
            const char *e = WsEnvStr(d->legacy_env);
            if (e && *e) Store(d->id, "clamp", WS_SOURCE_ENV);
        }
    }

    /* Host-owned [Graphics] values: the host has already parsed config.ini
     * and config.local.ini in order. Only widescreen has an environment
     * override. */
    Store(WS_SET_WIDESCREEN, g_config.widescreen ? "1" : "0",
          WS_SOURCE_CONFIG);
    {
        const char *e = WsEnvStr("SNESRECOMP_WS");
        if (e && *e) Store(WS_SET_WIDESCREEN, e, WS_SOURCE_ENV);
    }
    snprintf(buf, sizeof(buf), "%d", g_config.fullscreen ? g_config.fullscreen : 0);
    Store(WS_SET_FULLSCREEN, buf, WS_SOURCE_CONFIG);
    snprintf(buf, sizeof(buf), "%d", (int)SnesDisplayAspect_Clamp(g_config.display_aspect));
    Store(WS_SET_PIXEL_ASPECT, buf, WS_SOURCE_CONFIG);
    snprintf(buf, sizeof(buf), "%d", g_config.volume);
    Store(WS_SET_VOLUME, buf, WS_SOURCE_CONFIG);
}

void WsSettingsSyncHost(void)
{
    char buf[16];
    if (s_values[WS_SET_WIDESCREEN].source != WS_SOURCE_ENV) {
        snprintf(buf, sizeof(buf), "%d", g_config.widescreen ? 1 : 0);
        Store(WS_SET_WIDESCREEN, buf, WS_SOURCE_CONFIG);
    }
    snprintf(buf, sizeof(buf), "%d", g_config.fullscreen ? g_config.fullscreen : 0);
    Store(WS_SET_FULLSCREEN, buf, WS_SOURCE_CONFIG);
    snprintf(buf, sizeof(buf), "%d", (int)SnesDisplayAspect_Clamp(g_config.display_aspect));
    Store(WS_SET_PIXEL_ASPECT, buf, WS_SOURCE_CONFIG);
    snprintf(buf, sizeof(buf), "%d", g_config.volume);
    Store(WS_SET_VOLUME, buf, WS_SOURCE_CONFIG);
}

const char *WsSettingValue(WsSettingId id)
{
    if (id < 0 || id >= WS_SET_COUNT) return "";
    return s_values[id].value;
}

const char *WsSettingDisplayValue(WsSettingId id)
{
    const WsSettingDesc *d = WsSetting(id);
    if (!d) return "";
    if (id == WS_SET_VOLUME) {
        static char percent[8];
        snprintf(percent, sizeof(percent), "%d%%", WsSettingInt(id));
        return percent;
    }
    if (d->kind == WS_KIND_BOOL) {
        int idx = WsParseBool(s_values[id].value, 0);
        return d->labels ? d->labels[idx ? 1 : 0] : s_values[id].value;
    }
    if (d->kind == WS_KIND_ENUM) {
        int idx = FindChoice(d, s_values[id].value);
        if (idx >= 0 && d->labels) return d->labels[idx];
    }
    return s_values[id].value;
}

int WsSettingInt(WsSettingId id)
{
    const WsSettingDesc *d = WsSetting(id);
    if (!d) return 0;
    if (d->kind == WS_KIND_BOOL)
        return WsParseBool(s_values[id].value, 0);
    return (int)strtol(s_values[id].value, NULL, 10);
}

int WsSettingBool(WsSettingId id)
{
    return WsSettingInt(id) != 0;
}

WsSettingSource WsSettingSourceOf(WsSettingId id)
{
    if (id < 0 || id >= WS_SET_COUNT) return WS_SOURCE_DEFAULT;
    return s_values[id].source;
}

int WsSettingLocked(WsSettingId id)
{
    WsSettingSource src = WsSettingSourceOf(id);
    return src == WS_SOURCE_ENV || src == WS_SOURCE_LOCAL;
}

const char *WsSettingLockedBy(WsSettingId id)
{
    const WsSettingDesc *d = WsSetting(id);
    if (!d) return NULL;
    if (s_values[id].source == WS_SOURCE_LOCAL) return "config.local.ini";
    if (s_values[id].source == WS_SOURCE_ENV) {
        if (d->env) {
            const char *e = WsEnvStr(d->env);
            if (e && *e) return d->env;
        }
        if (d->legacy_env) return d->legacy_env;
        return "environment";
    }
    return NULL;
}

const char *WsSettingSourceName(WsSettingSource source)
{
    switch (source) {
    case WS_SOURCE_ENV: return "ENV";
    case WS_SOURCE_LOCAL: return "LOCAL";
    case WS_SOURCE_CONFIG: return "CONFIG";
    case WS_SOURCE_DEFAULT:
    default: return "DEFAULT";
    }
}

int WsSettingsSetValue(WsSettingId id, const char *value)
{
    const WsSettingDesc *d = WsSetting(id);
    if (!d || WsSettingLocked(id)) return 0;
    char next[64];
    Canonicalize(d, value, next, sizeof(next));
    if (strcmp(next, s_values[id].value) == 0) return 0;
    snprintf(s_values[id].value, sizeof(s_values[id].value), "%s", next);
    s_changed[id] = 1;
    return 1;
}

int WsSettingCycle(WsSettingId id, int direction)
{
    const WsSettingDesc *d = WsSetting(id);
    if (!d || WsSettingLocked(id) || direction == 0) return 0;

    if (d->kind == WS_KIND_BOOL) {
        int next = !WsSettingBool(id);
        return WsSettingsSetValue(id, next ? "1" : "0");
    }

    if (d->kind == WS_KIND_ENUM) {
        int idx = FindChoice(d, s_values[id].value);
        int n = d->num_choices;
        if (n <= 0) return 0;
        if (idx < 0)
            idx = direction > 0 ? -1 : 0;
        idx = (idx + direction + n) % n;
        return WsSettingsSetValue(id, d->values[idx]);
    }

    /* Int: the presets plus the current value, sorted, so a value read from
     * the file is reachable in the cycle. */
    int vals[16];
    int n = 0;
    for (int i = 0; i < d->num_choices && n < 16; i++)
        vals[n++] = d->presets[i];
    int cur = WsSettingInt(id);
    int have_cur = 0;
    for (int i = 0; i < n; i++)
        if (vals[i] == cur) have_cur = 1;
    if (!have_cur && n < 16) vals[n++] = cur;
    for (int i = 1; i < n; i++) {
        int v = vals[i], j = i - 1;
        while (j >= 0 && vals[j] > v) { vals[j + 1] = vals[j]; j--; }
        vals[j + 1] = v;
    }
    int idx = 0;
    for (int i = 0; i < n; i++)
        if (vals[i] == cur) { idx = i; break; }
    idx = (idx + direction + n) % n;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", vals[idx]);
    return WsSettingsSetValue(id, buf);
}

void WsSettingsOnChange(WsSettingId id, const char *old_value)
{
    if (id == WS_SET_MARGIN && old_value &&
        strcmp(old_value, s_values[id].value) != 0)
        WsMarginReset();
}

static int SettingDiffers(const WsSettingDesc *d)
{
    char raw[128];
    char section[32];
    snprintf(section, sizeof(section), "[%s]", d->section);
    if (!WsConfigReadStr("config.ini", section, d->key, raw, sizeof(raw)))
        return 1;
    char canon[64];
    Canonicalize(d, raw, canon, sizeof(canon));
    return strcmp(canon, s_values[d->id].value) != 0;
}

int WsSettingsHasUnsaved(void)
{
    for (int i = 0; i < WS_SET_COUNT; i++) {
        const WsSettingDesc *d = &kSettings[i];
        if (d->host_owned || WsSettingLocked(d->id)) continue;
        if (s_changed[d->id] && SettingDiffers(d)) return 1;
    }
    return 0;
}

/* ── Writer ─────────────────────────────────────────────────────────────── */

typedef struct {
    char section[32];
    char key[32];
    char value[64];
} PendingLine;

static int PendingId(WsSettingId *pending, int count, const char *section,
                     const char *key)
{
    for (int i = 0; i < count; i++) {
        if (pending[i] == (WsSettingId)-1) continue;
        const WsSettingDesc *d = &kSettings[pending[i]];
        if (WsCiEq(d->section, section) && WsCiEq(d->key, key))
            return i;
    }
    return -1;
}

static void FlushSection(FILE *out, const char *section,
                         WsSettingId *pending, int *count)
{
    if (!section || !*section) return;
    for (int i = 0; i < *count; i++) {
        WsSettingId id = pending[i];
        if (id == (WsSettingId)-1) continue;
        const WsSettingDesc *d = &kSettings[id];
        if (!WsCiEq(d->section, section)) continue;
        fprintf(out, "%s = %s\n", d->key, s_values[id].value);
        pending[i] = (WsSettingId)-1;
    }
}

static void WriteValueLine(FILE *out, const char *line, size_t len,
                           const char *value, const char *eol, size_t eollen)
{
    const char *end = line + len;
    const char *eq = memchr(line, '=', len);
    if (!eq) {
        fwrite(line, 1, len, out);
        fwrite(eol, 1, eollen, out);
        return;
    }
    const char *vstart = eq + 1;
    while (vstart < end && (*vstart == ' ' || *vstart == '\t')) vstart++;
    const char *vend = vstart;
    while (vend < end && *vend != ';' && *vend != '#') vend++;
    const char *vtrim = vend;
    while (vtrim > vstart && (vtrim[-1] == ' ' || vtrim[-1] == '\t' ||
                              vtrim[-1] == '\r'))
        vtrim--;
    fwrite(line, 1, (size_t)(vstart - line), out);
    fputs(value, out);
    fwrite(vtrim, 1, (size_t)(end - vtrim), out);
    fwrite(eol, 1, eollen, out);
}

static int SectionSeen(char seen[][32], int nseen, const char *section)
{
    for (int i = 0; i < nseen; i++)
        if (WsCiEq(seen[i], section)) return 1;
    return 0;
}

int WsSettingsSave(void)
{
    WsSettingId pending[WS_SET_COUNT];
    int count = 0;
    for (int i = 0; i < WS_SET_COUNT; i++) {
        const WsSettingDesc *d = &kSettings[i];
        if (d->host_owned || WsSettingLocked(d->id)) continue;
        if (s_changed[d->id] && SettingDiffers(d)) pending[count++] = d->id;
    }
    if (count == 0) return 0;

    FILE *in = fopen("config.ini", "rb");
    char *data = NULL;
    size_t size = 0;
    if (in) {
        fseek(in, 0, SEEK_END);
        long sz = ftell(in);
        fseek(in, 0, SEEK_SET);
        if (sz > 0) {
            data = (char *)malloc((size_t)sz + 1);
            if (data && fread(data, 1, (size_t)sz, in) != (size_t)sz) {
                free(data);
                data = NULL;
                sz = 0;
            }
        }
        if (data) data[sz] = 0;
        size = (size_t)(sz > 0 ? sz : 0);
        fclose(in);
    }

    FILE *out = fopen("config.ini.tmp", "wb");
    if (!out) {
        free(data);
        return 0;
    }

    char section[32] = "";
    char seen[16][32];
    int nseen = 0;
    size_t pos = 0;
    while (pos < size) {
        size_t start = pos;
        while (pos < size && data[pos] != '\n') pos++;
        size_t line_len = pos - start;
        const char *eol = "";
        size_t eol_len = 0;
        if (pos < size) {
            eol = "\n";
            eol_len = 1;
            if (line_len > 0 && data[start + line_len - 1] == '\r') {
                line_len--;
                eol = "\r\n";
                eol_len = 2;
            }
            pos++;
        }
        const char *line = data + start;
        size_t i = 0;
        while (i < line_len && (line[i] == ' ' || line[i] == '\t')) i++;

        if (i < line_len && line[i] == '[') {
            FlushSection(out, section, pending, &count);
            size_t n = 0;
            for (size_t j = i + 1; j < line_len && line[j] != ']' &&
                                      n + 1 < sizeof(section); j++)
                section[n++] = line[j];
            section[n] = 0;
            if (nseen < 16) snprintf(seen[nseen++], sizeof(seen[0]), "%s", section);
            fwrite(line, 1, line_len, out);
            fwrite(eol, 1, eol_len, out);
            continue;
        }

        int matched = -1;
        const char *eq = memchr(line, '=', line_len);
        if (*section && eq) {
            char key[64];
            size_t klen = 0;
            const char *k = line + i;
            while (k < eq && (*k == ' ' || *k == '\t')) k++;
            const char *kend = eq;
            while (kend > k && (kend[-1] == ' ' || kend[-1] == '\t')) kend--;
            while (k < kend && klen + 1 < sizeof(key)) key[klen++] = *k++;
            key[klen] = 0;
            matched = PendingId(pending, count, section, key);
        }
        if (matched >= 0) {
            const WsSettingId id = pending[matched];
            WriteValueLine(out, line, line_len, s_values[id].value,
                           eol, eol_len);
            pending[matched] = (WsSettingId)-1;
        } else {
            fwrite(line, 1, line_len, out);
            fwrite(eol, 1, eol_len, out);
        }
    }
    FlushSection(out, section, pending, &count);

    /* Sections that were not in the file at all: create them at the end. */
    for (int i = 0; i < count; i++) {
        WsSettingId id = pending[i];
        if (id == (WsSettingId)-1) continue;
        const WsSettingDesc *d = &kSettings[id];
        if (SectionSeen(seen, nseen, d->section)) continue;
        fprintf(out, "\n[%s]\n", d->section);
        if (nseen < 16)
            snprintf(seen[nseen++], sizeof(seen[0]), "%s", d->section);
        for (int j = i; j < count; j++) {
            WsSettingId other = pending[j];
            if (other == (WsSettingId)-1) continue;
            const WsSettingDesc *od = &kSettings[other];
            if (!WsCiEq(od->section, d->section)) continue;
            fprintf(out, "%s = %s\n", od->key, s_values[other].value);
            pending[j] = (WsSettingId)-1;
        }
    }

    fclose(out);
    free(data);

    if (rename("config.ini.tmp", "config.ini") != 0) {
#ifdef _WIN32
        remove("config.ini");
        if (rename("config.ini.tmp", "config.ini") == 0) goto renamed;
#endif
        remove("config.ini.tmp");
        return 0;
    }
#ifdef _WIN32
renamed:
#endif
    for (int i = 0; i < WS_SET_COUNT; i++) {
        const WsSettingDesc *d = &kSettings[i];
        if (!d->host_owned && !WsSettingLocked(d->id))
            s_values[i].source = WS_SOURCE_CONFIG;
        if (!d->host_owned) s_changed[i] = 0;
    }
    return count > 0 ? count : 1;
}
