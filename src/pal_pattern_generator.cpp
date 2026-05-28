/*
 * File:        pal_pattern_generator.cpp
 * Module:      pal_pattern_generator
 * Purpose:     Generates PAL software-generated frame patterns in BT.601 10-bit 4:4:4.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/pal_pattern_generator.h"

#include <algorithm>
#include <cmath>

namespace videosynth {
namespace {

constexpr int kPalWidth = 720;
constexpr int kPalHeight = 576;
// ITU-R BT.1700 Table 1 item 1a defines 576 active PAL lines.
// ITU-R BT.1700 Table 2 gives a 52.0 us analogue active line (64.0 us line
// period minus 12.0 us line blanking). Under the shared ITU-R BT.601 13.5 MHz
// digital sampling model, 52.0 us corresponds to 702 active picture pixels.
// The 720-sample frame-source raster therefore leaves 18 horizontal margin
// samples, placed as 9 samples left and 9 samples right.
constexpr int kPalActiveX = 9;
constexpr int kPalActiveY = 0;
constexpr int kPalActiveWidth = 702;
constexpr int kPalActiveHeight = 576;
constexpr int kPalCrosshatchStepX = 78;
constexpr int kPalCrosshatchStepY = 72;
constexpr std::int16_t kYMin = 48;
constexpr std::int16_t kYMax = 940;
constexpr std::int16_t kCMin = 64;
constexpr std::int16_t kCMax = 960;
constexpr std::int16_t kNeutralChroma = 512;

int ClampCode(int code, int lo, int hi) {
  return std::max(lo, std::min(code, hi));
}

std::int16_t ClampYCode(int code) {
  return static_cast<std::int16_t>(ClampCode(code, kYMin, kYMax));
}

std::int16_t ClampChromaCode(int code) {
  return static_cast<std::int16_t>(ClampCode(code, kCMin, kCMax));
}

YCbCr444Pixel MakePixel(int y_code, int cb_code, int cr_code) {
  return YCbCr444Pixel{
      .y = ClampYCode(y_code),
      .cb = ClampChromaCode(cb_code),
      .cr = ClampChromaCode(cr_code),
  };
}

void SetPixel(FrameSourceImage* image, int x, int y, const YCbCr444Pixel& pixel) {
  image->pixels[static_cast<std::size_t>((y * image->width) + x)] = pixel;
}

void FillImage(FrameSourceImage* image, const YCbCr444Pixel& pixel) {
  std::fill(image->pixels.begin(), image->pixels.end(), pixel);
}

void InitializePalImage(FrameSourceImage* out_image) {
  out_image->width = kPalWidth;
  out_image->height = kPalHeight;
  out_image->active_x = kPalActiveX;
  out_image->active_y = kPalActiveY;
  out_image->active_width = kPalActiveWidth;
  out_image->active_height = kPalActiveHeight;
  out_image->pixels.assign(static_cast<std::size_t>(kPalWidth * kPalHeight),
                           MakePixel(64, kNeutralChroma, kNeutralChroma));
}

void GenerateColourBars(FrameSourceImage* out_image, bool is_100_percent) {
  static constexpr int kBarCount = 8;
  static constexpr int kBarWidth = 90;

  static constexpr std::int16_t kY100[kBarCount] = {940, 840, 678, 578, 426, 326, 164, 64};
  static constexpr std::int16_t kCb100[kBarCount] = {512, 64, 663, 215, 809, 361, 960, 512};
  static constexpr std::int16_t kCr100[kBarCount] = {512, 585, 64, 137, 887, 960, 439, 512};

  static constexpr std::int16_t kY75[kBarCount] = {940, 648, 524, 448, 336, 260, 140, 64};
  static constexpr std::int16_t kCb75[kBarCount] = {512, 176, 624, 288, 736, 400, 848, 512};
  static constexpr std::int16_t kCr75[kBarCount] = {512, 568, 176, 232, 792, 848, 456, 512};

  const std::int16_t* y_codes = is_100_percent ? kY100 : kY75;
  const std::int16_t* cb_codes = is_100_percent ? kCb100 : kCb75;
  const std::int16_t* cr_codes = is_100_percent ? kCr100 : kCr75;

  for (int y = out_image->active_y; y < (out_image->active_y + out_image->active_height); ++y) {
    for (int x = out_image->active_x; x < (out_image->active_x + out_image->active_width); ++x) {
      const int relative_x = x - out_image->active_x;
      const int bar = std::min(kBarCount - 1,
                               (relative_x * kBarCount) / out_image->active_width);
      SetPixel(out_image, x, y, MakePixel(y_codes[bar], cb_codes[bar], cr_codes[bar]));
    }
  }
}

void GenerateLinearGrayscaleRamp(FrameSourceImage* out_image, bool horizontal) {
  const int horizontal_span = std::max(1, out_image->active_width - 1);
  const int vertical_span = std::max(1, out_image->active_height - 1);
  for (int y = out_image->active_y; y < (out_image->active_y + out_image->active_height); ++y) {
    for (int x = out_image->active_x; x < (out_image->active_x + out_image->active_width); ++x) {
      const int relative_x = x - out_image->active_x;
      const int relative_y = y - out_image->active_y;
      const double t = horizontal
                           ? static_cast<double>(relative_x) / static_cast<double>(horizontal_span)
                           : static_cast<double>(relative_y) / static_cast<double>(vertical_span);
      const double y_code = 64.0 + (876.0 * t);
      SetPixel(out_image,
               x,
               y,
               MakePixel(static_cast<int>(std::round(y_code)), kNeutralChroma, kNeutralChroma));
    }
  }
}

void GenerateCheckerboard(FrameSourceImage* out_image, int tile_size) {
  for (int y = out_image->active_y; y < (out_image->active_y + out_image->active_height); ++y) {
    for (int x = out_image->active_x; x < (out_image->active_x + out_image->active_width); ++x) {
      const int relative_x = x - out_image->active_x;
      const int relative_y = y - out_image->active_y;
      const int tile = ((relative_x / tile_size) + (relative_y / tile_size)) % 2;
      SetPixel(out_image,
               x,
               y,
               MakePixel(tile == 0 ? 940 : 64, kNeutralChroma, kNeutralChroma));
    }
  }
}

void GeneratePluge5Patch(FrameSourceImage* out_image) {
  FillImage(out_image, MakePixel(64, kNeutralChroma, kNeutralChroma));

  const int x0 = out_image->active_x +
                 static_cast<int>(std::round(0.20 * static_cast<double>(out_image->active_width)));
  const int x1 = out_image->active_x +
                 static_cast<int>(std::round(0.80 * static_cast<double>(out_image->active_width))) - 1;
  const int y0 = out_image->active_y +
                 static_cast<int>(std::round(0.75 * static_cast<double>(out_image->active_height)));
  const int y1 = out_image->active_y +
                 static_cast<int>(std::round(0.875 * static_cast<double>(out_image->active_height))) - 1;

  const int window_width = std::max(0, x1 - x0 + 1);
  if (window_width <= 0 || y1 < y0) {
    return;
  }

  const int base_width = window_width / 5;
  const int remainder = window_width % 5;
  int patch_widths[5] = {base_width, base_width, base_width, base_width, base_width};
  for (int i = 0; i < remainder; ++i) {
    ++patch_widths[i];
  }

  const int patch_y[5] = {64, 48, 64, 80, 64};

  int x_start = x0;
  for (int patch = 0; patch < 5; ++patch) {
    const int x_end = x_start + patch_widths[patch] - 1;
    for (int y = y0; y <= y1; ++y) {
      for (int x = x_start; x <= x_end; ++x) {
        SetPixel(out_image, x, y, MakePixel(patch_y[patch], kNeutralChroma, kNeutralChroma));
      }
    }
    x_start = x_end + 1;
  }
}

void GenerateCrosshatch(FrameSourceImage* out_image) {
  FillImage(out_image, MakePixel(64, kNeutralChroma, kNeutralChroma));

  const int active_x_end = out_image->active_x + out_image->active_width - 1;
  const int active_y_end = out_image->active_y + out_image->active_height - 1;
  for (int y = out_image->active_y; y <= active_y_end; ++y) {
    for (int x = out_image->active_x; x <= active_x_end; ++x) {
      const int relative_x = x - out_image->active_x;
      const int relative_y = y - out_image->active_y;
      if ((relative_x % kPalCrosshatchStepX) == 0 || (relative_y % kPalCrosshatchStepY) == 0 ||
          x == active_x_end || y == active_y_end) {
        SetPixel(out_image, x, y, MakePixel(940, kNeutralChroma, kNeutralChroma));
      }
    }
  }
}

}  // namespace

bool IsSupportedPalPattern(const std::string& pattern) {
  return pattern == "pal_ebu_colour_bars_100" || pattern == "pal_ebu_colour_bars_75" ||
         pattern == "pal_linear_grayscale_ramp_horizontal" ||
         pattern == "pal_linear_grayscale_ramp_vertical" ||
         pattern == "pal_luma_checkerboard_8x8" ||
         pattern == "pal_luma_checkerboard_16x16" ||
         pattern == "pal_full_field_black" || pattern == "pal_full_field_white" ||
         pattern == "pal_pluge_5patch_near_black" || pattern == "pal_crosshatch_visible_area_grid";
}

bool GeneratePalPatternFrame(const std::string& pattern,
                             FrameSourceImage* out_image,
                             std::string* error) {
  if (out_image == nullptr) {
    if (error != nullptr) {
      *error = "Frame source output image pointer must not be null.";
    }
    return false;
  }

  if (!IsSupportedPalPattern(pattern)) {
    if (error != nullptr) {
      *error = "Unsupported PAL software-generated pattern.";
    }
    return false;
  }

  InitializePalImage(out_image);

  if (pattern == "pal_ebu_colour_bars_100") {
    GenerateColourBars(out_image, true);
    return true;
  }
  if (pattern == "pal_ebu_colour_bars_75") {
    GenerateColourBars(out_image, false);
    return true;
  }
  if (pattern == "pal_linear_grayscale_ramp_horizontal") {
    GenerateLinearGrayscaleRamp(out_image, true);
    return true;
  }
  if (pattern == "pal_linear_grayscale_ramp_vertical") {
    GenerateLinearGrayscaleRamp(out_image, false);
    return true;
  }
  if (pattern == "pal_luma_checkerboard_8x8") {
    GenerateCheckerboard(out_image, 8);
    return true;
  }
  if (pattern == "pal_luma_checkerboard_16x16") {
    GenerateCheckerboard(out_image, 16);
    return true;
  }
  if (pattern == "pal_full_field_black") {
    FillImage(out_image, MakePixel(64, kNeutralChroma, kNeutralChroma));
    return true;
  }
  if (pattern == "pal_full_field_white") {
    FillImage(out_image, MakePixel(940, kNeutralChroma, kNeutralChroma));
    return true;
  }
  if (pattern == "pal_pluge_5patch_near_black") {
    GeneratePluge5Patch(out_image);
    return true;
  }

  GenerateCrosshatch(out_image);
  return true;
}

}  // namespace videosynth
