#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

/* Host-presentation choices for a 256x224 SNES frame. These do not alter the
 * emulated PPU; they only define the horizontal pixel aspect used at present. */
typedef enum SnesDisplayAspect {
  kSnesDisplayAspect_Crt4x3 = 0,
  kSnesDisplayAspect_SquarePixels8x7 = 1,
  kSnesDisplayAspect_SquareFrame1x1 = 2,
  kSnesDisplayAspect_Count,
} SnesDisplayAspect;

typedef struct SnesDisplayViewport {
  int x;
  int y;
  int width;
  int height;
} SnesDisplayViewport;

static inline SnesDisplayAspect SnesDisplayAspect_Clamp(int value) {
  return value >= 0 && value < kSnesDisplayAspect_Count
      ? (SnesDisplayAspect)value : kSnesDisplayAspect_Crt4x3;
}

static inline const char *SnesDisplayAspect_Name(int value) {
  static const char *const names[] = {"4:3", "8:7", "1:1"};
  return names[SnesDisplayAspect_Clamp(value)];
}

/* Shared by the standard desktop config reader and custom game hosts. */
static inline bool SnesDisplayAspect_Parse(const char *text, uint8_t *value) {
  static const char *const aliases[][3] = {
    {"4:3", "CRT", "0"}, {"8:7", "SquarePixels", "1"},
    {"1:1", "SquareFrame", "2"}
  };
  if (!text || !value) return false;
  for (int i = 0; i < kSnesDisplayAspect_Count; ++i) {
    for (int j = 0; j < 3; ++j) {
      const char *a = text, *b = aliases[i][j];
      while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        ++a; ++b;
      }
      if (*a == *b) { *value = (uint8_t)i; return true; }
    }
  }
  return false;
}
/* Horizontal:vertical pixel aspect. A 256x224 frame therefore presents as
 * 4:3, 8:7, or 1:1 respectively. */
static inline void SnesDisplayAspect_GetPixelAspect(
    SnesDisplayAspect aspect, int *numerator, int *denominator) {
  static const uint8_t kNumerators[kSnesDisplayAspect_Count] = {7, 1, 7};
  static const uint8_t kDenominators[kSnesDisplayAspect_Count] = {6, 1, 8};
  aspect = SnesDisplayAspect_Clamp((int)aspect);
  if (numerator) *numerator = kNumerators[aspect];
  if (denominator) *denominator = kDenominators[aspect];
}

/* Adaptive/fixed widescreen controls the visible field; DisplayAspect
 * independently controls pixel shape. Even widths keep the native field
 * centered. Native/capacity bounds letterbox instead of stretching pixels. */
typedef struct SnesDisplayFrame {
  int width, extra;
  double aspect;
} SnesDisplayFrame;

static inline SnesDisplayFrame SnesDisplayAspect_ComputeAdaptiveFrame(
    int native_width, int frame_height, int max_width, double target_aspect,
    SnesDisplayAspect display_aspect) {
  if (native_width <= 0) native_width = 256;
  if (frame_height <= 0) frame_height = 224;
  if (max_width < native_width) max_width = native_width;
  max_width -= (max_width - native_width) & 1;
  int par_num, par_den;
  SnesDisplayAspect_GetPixelAspect(display_aspect, &par_num, &par_den);
  double pixels_per_aspect = (double)frame_height * par_den / par_num;
  double minimum = native_width / pixels_per_aspect;
  double maximum = max_width / pixels_per_aspect;
  if (!(target_aspect >= minimum)) target_aspect = minimum; /* also NaN */
  if (target_aspect > maximum) target_aspect = maximum;
  int extra = (int)((target_aspect * pixels_per_aspect - native_width) / 2 + 0.5);
  int width = native_width + 2 * extra;
  return (SnesDisplayFrame){width, extra, target_aspect};
}

static inline SnesDisplayViewport SnesDisplayAspect_FitViewport(
    double aspect, int width, int height) {
  if (width <= 0 || height <= 0 || !(aspect > 0))
    return (SnesDisplayViewport){0};
  int w, h;
  if ((double)width / height > aspect) {
    h = height; w = (int)(height * aspect + 0.5);
  } else {
    w = width; h = (int)(width / aspect + 0.5);
  }
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  return (SnesDisplayViewport){(width - w) / 2, (height - h) / 2, w, h};
}

/* Widescreen is a horizontal 4/3 expansion of the authentic 256-pixel field.
 * Keeping the selected pixel aspect produces 16:9 from 4:3, 32:21 from 8:7,
 * and 4:3 from a 1:1 frame. The PPU requires a centered even width. */
static inline int SnesDisplayAspect_ComputeWideFrameWidth(int native_width) {
  if (native_width <= 0) native_width = 256;
  int64_t width = ((int64_t)native_width * 4 + 1) / 3;
  if (width & 1) width++;
  return width > INT32_MAX ? INT32_MAX - 1 : (int)width;
}

static inline void SnesDisplayAspect_ComputePresentationSize(
    int frame_width, int frame_height, SnesDisplayAspect aspect,
    int *width, int *height) {
  if (!width || !height) return;
  if (frame_width <= 0) frame_width = 256;
  if (frame_height <= 0) frame_height = 224;
  int par_num, par_den;
  SnesDisplayAspect_GetPixelAspect(aspect, &par_num, &par_den);
  *width = (int)(((int64_t)frame_width * par_num + par_den / 2) / par_den);
  *height = frame_height;
}

static inline int SnesDisplayAspect_ComputeWindowWidth(
    int frame_width, int frame_height, int window_height,
    SnesDisplayAspect aspect) {
  if (frame_width <= 0) frame_width = 256;
  if (frame_height <= 0) frame_height = 224;
  if (window_height <= 0) window_height = frame_height;
  int par_num, par_den;
  SnesDisplayAspect_GetPixelAspect(aspect, &par_num, &par_den);
  int64_t numerator = (int64_t)frame_width * par_num * window_height;
  int64_t denominator = (int64_t)par_den * frame_height;
  return (int)((numerator + denominator / 2) / denominator);
}

static inline void SnesDisplayAspect_ComputeViewport(
    int source_width, int source_height, int drawable_width,
    int drawable_height, SnesDisplayAspect aspect, bool ignore_aspect,
    bool integer_scale, SnesDisplayViewport *viewport) {
  if (!viewport) return;
  viewport->x = viewport->y = 0;
  viewport->width = drawable_width > 0 ? drawable_width : 1;
  viewport->height = drawable_height > 0 ? drawable_height : 1;
  if (ignore_aspect || source_width <= 0 || source_height <= 0 ||
      drawable_width <= 0 || drawable_height <= 0)
    return;

  int par_num, par_den;
  SnesDisplayAspect_GetPixelAspect(aspect, &par_num, &par_den);
  double source_display_width =
      (double)source_width * (double)par_num / (double)par_den;
  double scale_x = drawable_width / source_display_width;
  double scale_y = (double)drawable_height / source_height;
  double scale = scale_x < scale_y ? scale_x : scale_y;
  if (integer_scale && scale >= 1.0)
    scale = (double)(int)scale;
  if (scale <= 0.0) scale = scale_x < scale_y ? scale_x : scale_y;

  viewport->width = (int)(source_display_width * scale + 0.5);
  viewport->height = (int)(source_height * scale + 0.5);

  /* Even PPU widths cannot represent every target ratio exactly. Suppress a
   * sub-source-pixel seam while retaining real pillar/letterboxing. */
  if (!integer_scale) {
    int width_gap = drawable_width - viewport->width;
    int height_gap = drawable_height - viewport->height;
    int native_x = (drawable_width + source_width - 1) / source_width;
    int native_y = (drawable_height + source_height - 1) / source_height;
    if (width_gap > 0 && width_gap <= native_x)
      viewport->width = drawable_width;
    if (height_gap > 0 && height_gap <= native_y)
      viewport->height = drawable_height;
  }
  viewport->x = (drawable_width - viewport->width) / 2;
  viewport->y = (drawable_height - viewport->height) / 2;
}
