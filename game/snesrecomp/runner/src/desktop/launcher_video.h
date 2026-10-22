#pragma once

#include "display_aspect.h"
#include "recomp_launcher.h"
#include <stdio.h>

/* Custom hosts opt in explicitly, just like SnesDesktopHostGame's
 * display_aspect_supported / shader_supported. Widescreen mods and
 * game-defined view/camera choices remain independent. */
static inline void SnesLauncherVideo_Configure(
    RecompLauncherCSettings *settings, RecompLauncherCGameInfo *game,
    int display_aspect_supported, int shader_supported,
    int display_aspect, const char *shader) {
  if (display_aspect_supported) {
    static const char *const labels[] = {
      "4:3 (CRT)", "8:7 (Square pixels)", "1:1 (Square frame)"
    };
    settings->aspect_index = SnesDisplayAspect_Clamp(display_aspect);
#ifdef RECOMP_LAUNCHER_HAS_SNES_DISPLAY_ASPECT
    game->has_snes_display_aspect = 1;
#endif
    game->aspect_labels = labels;
    game->num_aspect_labels = kSnesDisplayAspect_Count;
    game->aspect_setting_label = "Display aspect";
    game->aspect_setting_help =
        "4:3 recreates a traditional TV. 8:7 uses square pixels. "
        "1:1 presents the native picture in a square. "
        "Also controls pixel proportions in adaptive widescreen.";
  }
  game->has_shader = shader_supported;
  if (shader_supported)
    snprintf(settings->shader_path, sizeof(settings->shader_path), "%s",
             shader ? shader : "");
}
