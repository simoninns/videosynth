/*
 * File:        ntsc_pattern_generator.cpp
 * Module:      ntsc_pattern_generator
 * Purpose:     Generates NTSC software-generated frame patterns in BT.601 10-bit 4:4:4.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/ntsc_pattern_generator.h"

#include <algorithm>
#include <cmath>

namespace videosynth {
namespace {

constexpr int kNtscWidth = 720;
constexpr int kNtscHeight = 480;
// SMPTE 170M-2004 defines an NTSC analogue active line of approximately
// 52.666 us. Under the shared ITU-R BT.601 13.5 MHz digital sampling model,
// that corresponds to 711 active picture pixels. The 720-sample frame-source
// raster therefore leaves 9 horizontal margin samples; the nearest centered
// integer placement is 4 samples left and 5 samples right. The frame-source
// raster carries only the 480 full active lines used by VideoSynth's
// frame-based contract.
constexpr int kNtscActiveX = 4;
constexpr int kNtscActiveY = 0;
constexpr int kNtscActiveWidth = 711;
constexpr int kNtscActiveHeight = 480;
constexpr int kNtscCrosshatchStepX = 79;
constexpr int kNtscCrosshatchStepY = 60;
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

void InitializeNtscImage(FrameSourceImage* out_image) {
  out_image->width = kNtscWidth;
  out_image->height = kNtscHeight;
  out_image->active_x = kNtscActiveX;
  out_image->active_y = kNtscActiveY;
  out_image->active_width = kNtscActiveWidth;
  out_image->active_height = kNtscActiveHeight;
  out_image->pixels.assign(static_cast<std::size_t>(kNtscWidth * kNtscHeight),
                           MakePixel(64, kNeutralChroma, kNeutralChroma));
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

void GenerateNtscCrosshatch(FrameSourceImage* out_image) {
  FillImage(out_image, MakePixel(64, kNeutralChroma, kNeutralChroma));

  const int active_x_end = out_image->active_x + out_image->active_width - 1;
  const int active_y_end = out_image->active_y + out_image->active_height - 1;
  for (int y = out_image->active_y; y <= active_y_end; ++y) {
    for (int x = out_image->active_x; x <= active_x_end; ++x) {
      const int relative_x = x - out_image->active_x;
      const int relative_y = y - out_image->active_y;
      if ((relative_x % kNtscCrosshatchStepX) == 0 || (relative_y % kNtscCrosshatchStepY) == 0 ||
          x == active_x_end || y == active_y_end) {
        SetPixel(out_image, x, y, MakePixel(940, kNeutralChroma, kNeutralChroma));
      }
    }
  }
}

void FillSmpteBarsBand(FrameSourceImage* out_image,
                       int y0,
                       int y1,
                       const std::int16_t* y_codes,
                       const std::int16_t* cb_codes,
                       const std::int16_t* cr_codes) {
  const int bar_width = out_image->width / 7;
  for (int y = y0; y <= y1; ++y) {
    for (int x = 0; x < out_image->width; ++x) {
      const int bar = std::min(6, x / bar_width);
      SetPixel(out_image, x, y, MakePixel(y_codes[bar], cb_codes[bar], cr_codes[bar]));
    }
  }
}

void FillSmpteCastellationsBand(FrameSourceImage* out_image,
                                int y0,
                                int y1,
                                const YCbCr444Pixel cells[7]) {
  const int cell_width = out_image->width / 7;
  for (int y = y0; y <= y1; ++y) {
    for (int x = 0; x < out_image->width; ++x) {
      const int cell = std::min(6, x / cell_width);
      SetPixel(out_image, x, y, cells[cell]);
    }
  }
}

void FillRect(FrameSourceImage* out_image,
              int x0,
              int x1,
              int y0,
              int y1,
              const YCbCr444Pixel& pixel) {
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      SetPixel(out_image, x, y, pixel);
    }
  }
}

void GenerateNtscSmpteColourBars(FrameSourceImage* out_image, bool is_100_percent) {
  static constexpr std::int16_t kY100Top[7] = {940, 840, 678, 578, 426, 326, 164};
  static constexpr std::int16_t kCb100Top[7] = {512, 64, 663, 215, 809, 361, 960};
  static constexpr std::int16_t kCr100Top[7] = {512, 585, 64, 137, 887, 960, 439};

  static constexpr std::int16_t kY75Top[7] = {940, 648, 524, 448, 336, 260, 140};
  static constexpr std::int16_t kCb75Top[7] = {512, 176, 624, 288, 736, 400, 848};
  static constexpr std::int16_t kCr75Top[7] = {512, 568, 176, 232, 792, 848, 456};

  const std::int16_t* y_top = is_100_percent ? kY100Top : kY75Top;
  const std::int16_t* cb_top = is_100_percent ? kCb100Top : kCb75Top;
  const std::int16_t* cr_top = is_100_percent ? kCr100Top : kCr75Top;

  const int y_a0 = out_image->active_y;
  const int y_a1 = out_image->active_y + static_cast<int>(std::round((2.0 / 3.0) * out_image->active_height)) - 1;
  const int y_b0 = y_a1 + 1;
  const int y_b1 = out_image->active_y + static_cast<int>(std::round((3.0 / 4.0) * out_image->active_height)) - 1;
  const int y_c0 = y_b1 + 1;
  const int y_c1 = out_image->active_y + out_image->active_height - 1;

  const int bar_x0 = out_image->active_x;
  const int bar_x1 = out_image->active_x + out_image->active_width - 1;
  for (int y = y_a0; y <= y_a1; ++y) {
    for (int x = bar_x0; x <= bar_x1; ++x) {
      const int relative_x = x - bar_x0;
      const int bar = std::min(6, (relative_x * 7) / out_image->active_width);
      SetPixel(out_image, x, y, MakePixel(y_top[bar], cb_top[bar], cr_top[bar]));
    }
  }

  YCbCr444Pixel cells[7] = {
      MakePixel(y_top[6], cb_top[6], cr_top[6]),
      MakePixel(64, 512, 512),
      MakePixel(y_top[4], cb_top[4], cr_top[4]),
      MakePixel(64, 512, 512),
      MakePixel(y_top[2], cb_top[2], cr_top[2]),
      MakePixel(64, 512, 512),
      MakePixel(y_top[0], cb_top[0], cr_top[0]),
  };
  for (int y = y_b0; y <= y_b1; ++y) {
    for (int x = bar_x0; x <= bar_x1; ++x) {
      const int relative_x = x - bar_x0;
      const int cell = std::min(6, (relative_x * 7) / out_image->active_width);
      SetPixel(out_image, x, y, cells[cell]);
    }
  }

  FillRect(out_image, bar_x0, bar_x1, y_c0, y_c1, MakePixel(64, 512, 512));

  const int base_block_width = out_image->active_width / 4;
  const int block_remainder = out_image->active_width % 4;
  int block_widths[4] = {base_block_width, base_block_width, base_block_width, base_block_width};
  for (int i = 0; i < block_remainder; ++i) {
    ++block_widths[i];
  }
  int block_x0[4] = {bar_x0, 0, 0, 0};
  for (int i = 1; i < 4; ++i) {
    block_x0[i] = block_x0[i - 1] + block_widths[i - 1];
  }
  FillRect(out_image,
           block_x0[0],
           block_x0[0] + block_widths[0] - 1,
           y_c0,
           y_c1,
           MakePixel(244, 612, 395));
  FillRect(out_image,
           block_x0[1],
           block_x0[1] + block_widths[1] - 1,
           y_c0,
           y_c1,
           MakePixel(940, 512, 512));
  FillRect(out_image,
           block_x0[2],
           block_x0[2] + block_widths[2] - 1,
           y_c0,
           y_c1,
           MakePixel(141, 697, 606));

  const int black_x0 = block_x0[3];
  const int black_x1 = bar_x1;
  FillRect(out_image, black_x0, black_x1, y_c0, y_c1, MakePixel(64, 512, 512));

  const int pluge_width = std::max(1, black_x1 - black_x0 + 1);
  int patch_widths[5] = {pluge_width / 5, pluge_width / 5, pluge_width / 5, pluge_width / 5, pluge_width / 5};
  for (int i = 0; i < (pluge_width % 5); ++i) {
    ++patch_widths[i];
  }
  const int patch_y[5] = {64, 48, 64, 80, 64};
  int x_start = black_x0;
  for (int patch = 0; patch < 5; ++patch) {
    const int x_end = x_start + patch_widths[patch] - 1;
    for (int y = y_c0; y <= y_c1; ++y) {
      for (int x = x_start; x <= x_end; ++x) {
        SetPixel(out_image, x, y, MakePixel(patch_y[patch], 512, 512));
      }
    }
    x_start = x_end + 1;
  }
}

}  // namespace

bool IsSupportedNtscPattern(const std::string& pattern) {
  return pattern == "ntsc_smpte_170m_colour_bars_100" ||
         pattern == "ntsc_smpte_170m_colour_bars_75" ||
         pattern == "ntsc_linear_grayscale_ramp_horizontal" ||
         pattern == "ntsc_linear_grayscale_ramp_vertical" ||
         pattern == "ntsc_luma_checkerboard_8x8" ||
         pattern == "ntsc_luma_checkerboard_16x16" ||
         pattern == "ntsc_full_field_black" || pattern == "ntsc_full_field_white" ||
         pattern == "ntsc_pluge_5patch_near_black" || pattern == "ntsc_crosshatch_visible_area_grid";
}

bool GenerateNtscPatternFrame(const std::string& pattern,
                              FrameSourceImage* out_image,
                              std::string* error) {
  if (out_image == nullptr) {
    if (error != nullptr) {
      *error = "Frame source output image pointer must not be null.";
    }
    return false;
  }

  if (!IsSupportedNtscPattern(pattern)) {
    if (error != nullptr) {
      *error = "Unsupported NTSC software-generated pattern.";
    }
    return false;
  }

  InitializeNtscImage(out_image);

  if (pattern == "ntsc_smpte_170m_colour_bars_100") {
    GenerateNtscSmpteColourBars(out_image, true);
    return true;
  }
  if (pattern == "ntsc_smpte_170m_colour_bars_75") {
    GenerateNtscSmpteColourBars(out_image, false);
    return true;
  }
  if (pattern == "ntsc_linear_grayscale_ramp_horizontal") {
    GenerateLinearGrayscaleRamp(out_image, true);
    return true;
  }
  if (pattern == "ntsc_linear_grayscale_ramp_vertical") {
    GenerateLinearGrayscaleRamp(out_image, false);
    return true;
  }
  if (pattern == "ntsc_luma_checkerboard_8x8") {
    GenerateCheckerboard(out_image, 8);
    return true;
  }
  if (pattern == "ntsc_luma_checkerboard_16x16") {
    GenerateCheckerboard(out_image, 16);
    return true;
  }
  if (pattern == "ntsc_full_field_black") {
    FillImage(out_image, MakePixel(64, kNeutralChroma, kNeutralChroma));
    return true;
  }
  if (pattern == "ntsc_full_field_white") {
    FillImage(out_image, MakePixel(940, kNeutralChroma, kNeutralChroma));
    return true;
  }
  if (pattern == "ntsc_pluge_5patch_near_black") {
    GeneratePluge5Patch(out_image);
    return true;
  }
  GenerateNtscCrosshatch(out_image);
  return true;
}

}  // namespace videosynth
