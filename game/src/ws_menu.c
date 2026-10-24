/* In-game options menu (F10 / Select+L). See ws_menu.h.
 *
 * The host freezes the guest while this menu is open and routes keys and pad
 * navigation here. The panel is 512x448 (2x the SNES field), drawn with the
 * runner's shared overlay primitives, and every value is applied through
 * ws_settings.[ch]. */
#include "ws_menu.h"

#include "config.h"
#include "desktop/sdl_compat.h"
#include "snes_overlay_draw.h"
#include "ws_settings.h"

#include <stdio.h>
#include <string.h>

#define WS_MENU_PANEL_W 512
#define WS_MENU_PANEL_H 448
#define WS_MENU_ROWS (WS_SET_COUNT + 2)
#define WS_MENU_SAVE (WS_SET_COUNT)
#define WS_MENU_CLOSE (WS_SET_COUNT + 1)

#define WS_MENU_ROW_Y 42
#define WS_MENU_ROW_H 27
#define WS_MENU_ROW_X 16
#define WS_MENU_ROW_W 480
#define WS_MENU_LABEL_X 36
#define WS_MENU_VALUE_X 292

static int s_open;
static int s_selected;
static int s_dirty;
static int s_open_prev_held;

static uint32_t s_prev_pad;
static uint32_t s_held_dir;
static uint32_t s_held_since;
static uint32_t s_last_repeat;

static uint32_t s_panel[WS_MENU_PANEL_W * WS_MENU_PANEL_H];

/* ── Drawing ─────────────────────────────────────────────────────────────── */

static void Fill(int x, int y, int w, int h, uint32_t color)
{
    snes_ovl_fill_rect(s_panel, WS_MENU_PANEL_W, WS_MENU_PANEL_H, x, y, w, h, color);
}

static void Stroke(int x, int y, int w, int h, uint32_t color)
{
    snes_ovl_stroke_rect(s_panel, WS_MENU_PANEL_W, WS_MENU_PANEL_H, x, y, w, h, color);
}

/* Option rows use 12 px glyphs: the host's 8x8 font scales only by whole
 * steps, so each glyph is drawn at 1x into a cell and resampled 1.5x with
 * nearest neighbour. */
static void TextRow(int x, int y, const char *s, uint32_t color)
{
    uint32_t cell[8 * 8];
    for (; *s; s++, x += 12) {
        memset(cell, 0, sizeof(cell));
        snes_ovl_draw_char(cell, 8, 8, 0, 0, *s, 1u, 1);
        for (int dy = 0; dy < 12; dy++) {
            for (int dx = 0; dx < 12; dx++) {
                const int px = x + dx, py = y + dy;
                if (!cell[(dy * 2 / 3) * 8 + dx * 2 / 3]) continue;
                if (px >= 0 && px < WS_MENU_PANEL_W && py >= 0 &&
                    py < WS_MENU_PANEL_H)
                    s_panel[py * WS_MENU_PANEL_W + px] = color;
            }
        }
    }
}

static void Text(int x, int y, const char *s, uint32_t color, int scale)
{
    snes_ovl_draw_text(s_panel, WS_MENU_PANEL_W, WS_MENU_PANEL_H, x, y, s, color, scale);
}

static const char *RowHelp(WsSettingId id)
{
    switch (id) {
    case WS_SET_WIDESCREEN: return "ACTIVA EL CAMPO ANCHO DE CARRERA";
    case WS_SET_ASPECT: return "PROPORCION DEL CAMPO: AUTO SIGUE LA VENTANA";
    case WS_SET_MAX_WIDTH: return "ANCHO MAXIMO DEL CAMPO EN PIXELES";
    case WS_SET_MARGIN: return "COMO SE RELLENAN LOS MARGENES DE PISTA";
    case WS_SET_HUD: return "VUELTAS Y TIEMPO EN LAS ESQUINAS DEL CUADRO";
    case WS_SET_CULL: return "LOS PILOTOS SIGUEN VISIBLES EN LOS MARGENES";
    case WS_SET_FULLSCREEN: return "MODO DE VENTANA; SE APLICA AL INSTANTE";
    case WS_SET_PIXEL_ASPECT: return "FORMA DE LOS PIXELES SNES, NO DEL CAMPO";
    case WS_SET_VOLUME: return "VOLUMEN DEL AUDIO, EN PORCENTAJE";
    case WS_SET_MINIMAP: return "RADAR LOCAL EN LA ESQUINA INFERIOR";
    case WS_SET_UNLOCK_TOURS: return "DESBLOQUEA LOS NUEVE TOURS";
    default: return "";
    }
}

static void DrawPanel(void)
{
    const uint32_t bg = 0xFF0F1118u;
    const uint32_t header = 0xFF171B25u;
    const uint32_t fg = 0xFFE2E5EBu;
    const uint32_t dim = 0xFF7F8796u;
    const uint32_t accent = 0xFFFFD24Du;
    const uint32_t sel_bg = 0xFF2B2830u;
    const uint32_t line = 0xFF303746u;

    for (int i = 0; i < WS_MENU_PANEL_W * WS_MENU_PANEL_H; i++) s_panel[i] = bg;
    Fill(0, 0, WS_MENU_PANEL_W, 40, header);
    Text(20, 12, "OPCIONES", accent, 2);
    Text(360, 16, "F10/SELECT+L", dim, 1);
    if (s_dirty) Text(400, 28, "* SIN GUARDAR", accent, 1);

    for (int row = 0; row < WS_MENU_ROWS; row++) {
        const int y = WS_MENU_ROW_Y + row * WS_MENU_ROW_H;
        const int selected = row == s_selected;
        if (selected) {
            Fill(WS_MENU_ROW_X, y - 2, WS_MENU_ROW_W, WS_MENU_ROW_H - 2, sel_bg);
            Stroke(WS_MENU_ROW_X, y - 2, WS_MENU_ROW_W, WS_MENU_ROW_H - 2, accent);
        }
        if (row < WS_SET_COUNT) {
            const WsSettingId id = (WsSettingId)row;
            const WsSettingDesc *d = WsSetting(id);
            const int locked = WsSettingLocked(id);
            char value[96];

            TextRow(20, y + 8, selected ? ">" : " ", accent);
            TextRow(WS_MENU_LABEL_X, y + 8, d->label, selected ? accent : fg);
            if (locked)
                snprintf(value, sizeof(value), "%s", WsSettingDisplayValue(id));
            else
                snprintf(value, sizeof(value), "<%s>", WsSettingDisplayValue(id));
            TextRow(WS_MENU_VALUE_X, y + 8, value, selected ? accent : fg);
            if (locked) {
                char mark[16];
                snprintf(mark, sizeof(mark), "[%s]",
                         WsSettingSourceName(WsSettingSourceOf(id)));
                Text(464, y + 10, mark, 0xFFFF6B6Bu, 1);
            }
        } else if (row == WS_MENU_SAVE) {
            TextRow(WS_MENU_LABEL_X, y + 8, "GUARDAR EN CONFIG.INI",
                    selected ? accent : fg);
        } else {
            TextRow(WS_MENU_LABEL_X, y + 8, "CERRAR",
                    selected ? accent : fg);
        }
    }

    Fill(0, 400, WS_MENU_PANEL_W, WS_MENU_PANEL_H - 400, header);
    Stroke(0, 400, WS_MENU_PANEL_W, 1, line);

    char help[96] = "";
    char note[96] = "";
    if (s_selected < WS_SET_COUNT) {
        const WsSettingId id = (WsSettingId)s_selected;
        const WsSettingDesc *d = WsSetting(id);
        snprintf(help, sizeof(help), "%s", RowHelp(id));
        if (WsSettingLocked(id)) {
            const char *by = WsSettingLockedBy(id);
            snprintf(note, sizeof(note), "BLOQUEADO POR %s", by ? by : "ENTORNO");
        } else if (d->applies == WS_APPLY_PERMANENT_NOTE) {
            snprintf(note, sizeof(note),
                     "PERMANENTE PARA LA PARTIDA: APAGARLO NO OCULTA LOS TOURS");
        } else if (d->applies == WS_APPLY_NEXT_RACE) {
            snprintf(note, sizeof(note), "SE APLICA EN LA PROXIMA CARRERA");
        } else if (d->applies == WS_APPLY_RESTART) {
            snprintf(note, sizeof(note), "REQUIERE REINICIAR LA APLICACION");
        }
    } else if (s_selected == WS_MENU_SAVE) {
        snprintf(help, sizeof(help), "ESCRIBE LAS OPCIONES EN CONFIG.INI");
        snprintf(note, sizeof(note), "LAS TECLAS DE ENTORNO Y CONFIG.LOCAL.INI NO SE TOCAN");
    } else {
        snprintf(help, sizeof(help),
                 "CIERRA SIN GUARDAR; LOS CAMBIOS QUEDAN EN ESTA SESION");
    }
    Text(16, 406, help, fg, 1);
    if (note[0]) Text(16, 422, note, accent, 1);
    Text(16, 437, "F10/ESC CERRAR   FLECHAS CAMBIAR   A/ENTER ACEPTAR",
         dim, 1);
}

/* ── Applying and saving ─────────────────────────────────────────────────── */

static void ChangeSelected(int direction)
{
    if (s_selected < 0 || s_selected >= WS_SET_COUNT) return;
    const WsSettingId id = (WsSettingId)s_selected;
    char old_value[64];
    snprintf(old_value, sizeof(old_value), "%s", WsSettingValue(id));
    if (!WsSettingCycle(id, direction)) return;

    if (id == WS_SET_WIDESCREEN) {
        g_config.widescreen = WsSettingBool(id) != 0;
    } else if (id == WS_SET_FULLSCREEN || id == WS_SET_PIXEL_ASPECT) {
        snesrecomp_desktop_set_display(WsSettingInt(WS_SET_FULLSCREEN),
                                       WsSettingInt(WS_SET_PIXEL_ASPECT), 0);
    } else if (id == WS_SET_VOLUME) {
        snesrecomp_desktop_set_volume(WsSettingInt(id), 0);
    } else {
        WsSettingsOnChange(id, old_value);
    }
    s_dirty = 1;
}

static void SaveAll(void)
{
    WsSettingsSave();
    snesrecomp_desktop_set_widescreen(WsSettingBool(WS_SET_WIDESCREEN));
    snesrecomp_desktop_set_display(WsSettingInt(WS_SET_FULLSCREEN),
                                   WsSettingInt(WS_SET_PIXEL_ASPECT), 1);
    snesrecomp_desktop_set_volume(WsSettingInt(WS_SET_VOLUME), 1);
    s_dirty = WsSettingsHasUnsaved();
}

static void ActivateSelected(void)
{
    if (s_selected == WS_MENU_SAVE)
        SaveAll();
    else if (s_selected == WS_MENU_CLOSE)
        s_open = 0;
}

static void MoveSelected(int delta)
{
    s_selected += delta;
    if (s_selected < 0) s_selected = WS_MENU_ROWS - 1;
    if (s_selected >= WS_MENU_ROWS) s_selected = 0;
}

/* ── Input ───────────────────────────────────────────────────────────────── */

static void Open(void)
{
    if (s_open) return;
    WsSettingsSyncHost();
    s_open = 1;
    s_selected = 0;
    s_prev_pad = 0;
    s_held_dir = 0;
    s_open_prev_held = 1;   /* Select+L is held: do not treat it as a nav edge */
}

static void Close(void)
{
    s_open = 0;
}

static int PollOpen(uint32_t pad)
{
    const uint32_t gesture = SNES_PAD_SELECT | SNES_PAD_L;
    const int held = (pad & gesture) == gesture;
    const int opened = held && !s_open_prev_held;
    s_open_prev_held = held;
    if (opened && !s_open) {
        Open();
        return 1;
    }
    return 0;
}

static int IsOpen(void) { return s_open; }

static void HandleKey(int key, int repeat)
{
    (void)repeat;
    switch (key) {
    case SDLK_UP:     MoveSelected(-1); break;
    case SDLK_DOWN:   MoveSelected(1); break;
    case SDLK_LEFT:   ChangeSelected(-1); break;
    case SDLK_RIGHT:  ChangeSelected(1); break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: ActivateSelected(); break;
    case SDLK_ESCAPE:
    case SDLK_F10:    Close(); break;
    default: break;
    }
}

static void PollNav(uint32_t pad, uint32_t ticks_ms)
{
    const uint32_t pressed = pad & ~s_prev_pad;
    const uint32_t dir_mask = SNES_PAD_UP | SNES_PAD_DOWN |
                              SNES_PAD_LEFT | SNES_PAD_RIGHT;
    const uint32_t dir = pad & dir_mask;

    if (pressed & SNES_PAD_B) {
        Close();
        s_prev_pad = pad;
        return;
    }
    if (pressed & SNES_PAD_A)
        ActivateSelected();

    if ((pressed & SNES_PAD_UP) && !(pressed & SNES_PAD_DOWN))
        MoveSelected(-1);
    if ((pressed & SNES_PAD_DOWN) && !(pressed & SNES_PAD_UP))
        MoveSelected(1);
    if ((pressed & SNES_PAD_LEFT) && !(pressed & SNES_PAD_RIGHT))
        ChangeSelected(-1);
    if ((pressed & SNES_PAD_RIGHT) && !(pressed & SNES_PAD_LEFT))
        ChangeSelected(1);

    const int single = dir && !(dir & (dir - 1)) &&
                       !(dir == SNES_PAD_UP ? (pad & SNES_PAD_DOWN) : 0) &&
                       !(dir == SNES_PAD_DOWN ? (pad & SNES_PAD_UP) : 0) &&
                       !(dir == SNES_PAD_LEFT ? (pad & SNES_PAD_RIGHT) : 0) &&
                       !(dir == SNES_PAD_RIGHT ? (pad & SNES_PAD_LEFT) : 0);
    if (!single) {
        s_held_dir = 0;
    } else if (dir != s_held_dir) {
        s_held_dir = dir;
        s_held_since = ticks_ms;
        s_last_repeat = ticks_ms;
    } else if (ticks_ms - s_held_since >= SNES_OVL_REPEAT_DELAY &&
               ticks_ms - s_last_repeat >= SNES_OVL_REPEAT_RATE) {
        s_last_repeat = ticks_ms;
        if (dir == SNES_PAD_UP) MoveSelected(-1);
        else if (dir == SNES_PAD_DOWN) MoveSelected(1);
        else if (dir == SNES_PAD_LEFT) ChangeSelected(-1);
        else if (dir == SNES_PAD_RIGHT) ChangeSelected(1);
    }
    s_prev_pad = pad;
}

static int OverlayImage(const uint32_t **pixels, int *w, int *h)
{
    if (!s_open) {
        if (pixels) *pixels = NULL;
        if (w) *w = 0;
        if (h) *h = 0;
        return 0;
    }
    DrawPanel();
    if (pixels) *pixels = s_panel;
    if (w) *w = WS_MENU_PANEL_W;
    if (h) *h = WS_MENU_PANEL_H;
    return 1;
}

const SnesTitleMenu kWsTitleMenu = {
    .poll_open = &PollOpen,
    .is_open = &IsOpen,
    .open = &Open,
    .handle_key = &HandleKey,
    .poll_nav = &PollNav,
    .overlay_image = &OverlayImage,
    .close = &Close,
    .open_key = SDLK_F10,
};
