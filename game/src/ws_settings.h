/* One model for every user-facing widescreen option. See ws_settings.c.
 *
 * Every option has a config.ini section/key, an optional environment
 * override, a default, a set of values and an apply class. The value's source
 * is remembered so the in-game menu can lock what the environment or
 * config.local.ini owns, and the writer can leave those keys alone.
 */
#ifndef WS_SETTINGS_H
#define WS_SETTINGS_H

#include <stddef.h>

typedef enum {
    WS_SET_WIDESCREEN = 0,
    WS_SET_ASPECT,
    WS_SET_MAX_WIDTH,
    WS_SET_MARGIN,
    WS_SET_HUD,
    WS_SET_CULL,
    WS_SET_FULLSCREEN,
    WS_SET_PIXEL_ASPECT,
    WS_SET_VOLUME,
    WS_SET_MINIMAP,
    WS_SET_UNLOCK_TOURS,
    WS_SET_COUNT,
} WsSettingId;

typedef enum {
    WS_SOURCE_DEFAULT = 0,
    WS_SOURCE_CONFIG,
    WS_SOURCE_LOCAL,
    WS_SOURCE_ENV,
} WsSettingSource;

typedef enum {
    WS_APPLY_LIVE = 0,
    WS_APPLY_NEXT_RACE,
    WS_APPLY_RESTART,
    WS_APPLY_PERMANENT_NOTE,
} WsApplyClass;

typedef enum {
    WS_KIND_BOOL = 0,
    WS_KIND_ENUM,
    WS_KIND_INT,
} WsSettingKind;

typedef struct {
    WsSettingId id;
    const char *label;
    const char *section;       /* "Widescreen" (without brackets) */
    const char *key;
    const char *env;           /* primary environment override, or NULL */
    const char *legacy_env;    /* legacy environment override, or NULL */
    WsSettingKind kind;
    const char *const *values; /* enum values, canonical spelling */
    const char *const *labels; /* enum display labels */
    const int *presets;        /* int presets, or NULL */
    int num_choices;
    int min_value;
    int max_value;
    const char *default_value;
    WsApplyClass applies;
    int host_owned;            /* [Graphics]: read/set through the host only */
} WsSettingDesc;

const WsSettingDesc *WsSetting(WsSettingId id);
int WsSettingsCount(void);

/* Resolve every option: env > config.local.ini > config.ini > default. */
void WsSettingsLoad(void);
/* Refresh the host-owned values from g_config (volume changed by a hotkey,
 * fullscreen/aspect by the host). Locked environment values are left alone. */
void WsSettingsSyncHost(void);

const char *WsSettingValue(WsSettingId id);
const char *WsSettingDisplayValue(WsSettingId id);
int WsSettingInt(WsSettingId id);
int WsSettingBool(WsSettingId id);

WsSettingSource WsSettingSourceOf(WsSettingId id);
/* 1 when env or config.local.ini supplied the value. */
int WsSettingLocked(WsSettingId id);
/* Name of the environment variable or file that locks the option, else NULL. */
const char *WsSettingLockedBy(WsSettingId id);

/* Normalise and store a value. Returns 1 when it changed, 0 when locked,
 * invalid or unchanged. */
int WsSettingsSetValue(WsSettingId id, const char *value);
/* Move through the option's list/range. Returns 1 when it changed. */
int WsSettingCycle(WsSettingId id, int direction);
/* Follow-up after a changed value (the margin ring reset). */
void WsSettingsOnChange(WsSettingId id, const char *old_value);

/* In-place update of config.ini. Returns the number of keys written. */
int WsSettingsSave(void);
/* 1 when a writable option differs from config.ini. */
int WsSettingsHasUnsaved(void);

const char *WsSettingSourceName(WsSettingSource source);

#endif /* WS_SETTINGS_H */
