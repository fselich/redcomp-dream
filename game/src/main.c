/*
 * Uniracers — desktop host shim.
 *
 * Identity and hooks, nothing else. The host itself is the framework's
 * (snesrecomp/runner/src/desktop/host_main.h): the pre-boot launcher, ROM
 * resolution and digest checks, config.ini and keybinds.ini, the window and
 * the SDL / OpenGL presenters, audio, gamepads, the in-game save-state
 * browser and rewind filmstrip, the OSD, the pacing clock, crash handlers and
 * the post-mortem report, mod packages, Generate & rebuild, and the netplay
 * barrier when the project is built with it. A fix there reaches this
 * project on a submodule pull; nothing in this file needs to change for it.
 *
 * This file used to BE the host -- a ~700-line copy made at scaffold time --
 * so every project scaffolded before a fix kept the bug. Two ports of the
 * same game, a week apart, had different hosts.
 *
 * Nothing here ever ships a ROM. The player supplies one; the host makes
 * that easy (launcher, then positional argument, then a copy beside the
 * executable, then the rom.cfg cache, then a file picker) and checks what it
 * is handed against the digests in rom_identity.txt.
 *
 * Grow this file only with what is specific to THIS title: a custom
 * presenter (prepare_frame / draw_frame), an SPC player, a pacing rule
 * (keep_pacing_debt), a Mods provider. See SnesDesktopHostGame for the
 * complete list of hooks and what each is for.
 */

#include "host_main.h"
#include "game_rtl.h"
#include "snesrecomp_rom_identity.h"  /* generated from rom_identity.txt */
#include "config.h"
#include "common_rtl.h"
#include "ws_frame.h"

#include <stdlib.h>
#include <string.h>

#ifndef __ANDROID__
#define SDL_MAIN_HANDLED 1
#endif
#include "desktop/sdl_compat.h"
#ifdef __ANDROID__
/* SDLActivity loads libmain.so and dlsyms its SDL_main. */
#define main SDL_main
#endif

#ifndef SNES_GAME_VERSION
#define SNES_GAME_VERSION "dev"
#endif

/* Race frame width (openspec/changes/uniracers-ws-ultrawide). The target
 * aspect is SNESRECOMP_WS_ASPECT ("W:H", a decimal, or "auto") or, by
 * default, the window aspect; never narrower than 16:9. The width is capped
 * at 512 px: the renderer decodes the 9-bit OAM X as [E-256, 256+E), so left-
 * margin sprites alias into the right margin once the margin E exceeds 128. */
enum { kWsRaceMaxWidth = 512 };

static double WsTargetAspect(int drawable_w, int drawable_h)
{
    const double wide = 16.0 / 9.0;
    const char *e = WsEnvStr("SNESRECOMP_WS_ASPECT");
    if (e && *e && strcmp(e, "auto") != 0) {
        double a = 0, b = 0;
        const char *colon = strchr(e, ':');
        if (colon) {
            a = atof(e);
            b = atof(colon + 1);
            if (a > 0 && b > 0) return a / b;
        } else if ((a = atof(e)) > 0) {
            return a;
        }
    }
    if (drawable_w > 0 && drawable_h > 0) {
        double window = (double)drawable_w / drawable_h;
        return window > wide ? window : wide;
    }
    return wide;
}

static void PrepareFrame(int drawable_w, int drawable_h,
                         int *frame_w, int *frame_h)
{
    *frame_h = 224;
    if (!(WsWideEnabled() && WsInLiveRace())) {
        *frame_w = 256;
        return;
    }
    SnesDisplayFrame f = SnesDisplayAspect_ComputeAdaptiveFrame(
        256, 224, kWsRaceMaxWidth, WsTargetAspect(drawable_w, drawable_h),
        SnesDisplayAspect_Clamp(g_config.display_aspect));
    *frame_w = f.width;
}

static const SnesDesktopHostGame kGameHost = {
    .display_name        = "Uniracers",
    .window_title        = "Uniracers",
    .region              = SNESRECOMP_ROM_REGION,
    .rom_file            = SNESRECOMP_ROM_FILE,
    .expected_sha256_hex = SNESRECOMP_ROM_EXPECTED_SHA256,
    .expected_crc32_hex  = SNESRECOMP_ROM_EXPECTED_CRC32,
    .game_id             = SNESRECOMP_ROM_GAME_ID,
    .build_version       = SNES_GAME_VERSION,
    .game_info           = &kGameInfo,
    .num_players         = 2,
    .native_widescreen   = 1,
    .widescreen_supported = 1,
    .prepare_frame       = &PrepareFrame,
    .after_config        = &GameAfterConfig,
    .after_run_frame     = &GameAfterRunFrame,
    /* Battery-backed SRAM shows the launcher's SAVES panel. Leave NULL for a
     * title without one. The path is exe-relative. */
    .sram_path           = "saves/save.srm",
};

#ifndef __ANDROID__
#undef main   /* desktop: keep plain main() even if SDL_main.h remapped it */
#endif
int main(int argc, char **argv)
{
    return snesrecomp_desktop_main(&kGameHost, argc, argv);
}
