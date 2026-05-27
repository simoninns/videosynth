/*
 * File:        frame_source.cpp
 * Module:      frame_source
 * Purpose:     Generates fixed-format 10-bit 4:4:4 BT.601 frame data for frame-based sources.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/frame_source.h"

#include <algorithm>
#include <cmath>

namespace videosynth {
namespace {

constexpr int kActiveWidthPixels = 720;

int ClampCode(int code, int lo, int hi) {
  return std::max(lo, std::min(code, hi));
}

int ActiveHeightForStandard(Standard standard) {
  if (standard == Standard::kPal) {
    return 576;
  }
  if (standard == Standard::kNtsc) {
    return 480;
  }
  return 0;
}

FrameSourceImage MakeImage(Standard standard, std::string* error) {
  FrameSourceImage image;
  image.width = kActiveWidthPixels;
  image.height = ActiveHeightForStandard(standard);
  if (image.height <= 0) {
    if (error != nullptr) {
      *error = "Unsupported video standard for frame-based source generation.";
    }
    return image;
  }

  image.pixels.resize(static_cast<std::size_t>(image.width * image.height));
  return image;
}

}  // namespace

const YCbCr444Pixel& FrameSourceImage::PixelAt(int x, int y) const {
  return pixels[static_cast<std::size_t>((y * width) + x)];
}

bool TestPatternFrameSource::SupportsPattern(const std::string& pattern) const {
  return pattern == "ebu_colour_bars" || pattern == "colour_bars_75" ||
         pattern == "grayscale_ramp_horizontal" || pattern == "grayscale_ramp" ||
         pattern == "pluge" || pattern == "pluge_basic";
}

bool TestPatternFrameSource::GenerateFrame(const std::string& pattern,
                                           Standard standard,
                                           FrameSourceImage* out_image,
                                           std::string* error) const {
  if (out_image == nullptr) {
    if (error != nullptr) {
      *error = "Frame source output image pointer must not be null.";
    }
    return false;
  }

  if (pattern == "ebu_colour_bars" || pattern == "colour_bars_75") {
    return GenerateColourBars75(standard, out_image, error);
  }
  if (pattern == "grayscale_ramp_horizontal" || pattern == "grayscale_ramp") {
    return GenerateGrayscaleRamp(standard, out_image, error);
  }
  if (pattern == "pluge" || pattern == "pluge_basic") {
    return GeneratePluge(standard, out_image, error);
  }

  if (error != nullptr) {
    *error = "Unsupported software-generated pattern for frame source generation.";
  }
  return false;
}

bool TestPatternFrameSource::GenerateColourBars75(Standard standard,
                                                  FrameSourceImage* out_image,
                                                  std::string* error) const {
  static constexpr int kBarCount = 8;
  static constexpr std::int16_t kY[kBarCount] = {720, 648, 524, 448, 336, 260, 140, 64};
  static constexpr std::int16_t kCb[kBarCount] = {512, 176, 624, 288, 736, 400, 848, 512};
  static constexpr std::int16_t kCr[kBarCount] = {512, 568, 176, 232, 792, 848, 456, 512};

  *out_image = MakeImage(standard, error);
  if (out_image->height <= 0) {
    return false;
  }

  const int bar_width = std::max(1, out_image->width / kBarCount);
  for (int y = 0; y < out_image->height; ++y) {
    for (int x = 0; x < out_image->width; ++x) {
      const int bar = std::min(kBarCount - 1, x / bar_width);
      out_image->pixels[static_cast<std::size_t>((y * out_image->width) + x)] =
          YCbCr444Pixel{.y = kY[bar], .cb = kCb[bar], .cr = kCr[bar]};
    }
  }

  return true;
}

bool TestPatternFrameSource::GenerateGrayscaleRamp(Standard standard,
                                                   FrameSourceImage* out_image,
                                                   std::string* error) const {
  *out_image = MakeImage(standard, error);
  if (out_image->height <= 0) {
    return false;
  }

  const int span = std::max(1, out_image->width - 1);
  for (int y = 0; y < out_image->height; ++y) {
    for (int x = 0; x < out_image->width; ++x) {
      const double t = static_cast<double>(x) / static_cast<double>(span);
      const int y_code = static_cast<int>(std::lround(64.0 + (876.0 * t)));
      out_image->pixels[static_cast<std::size_t>((y * out_image->width) + x)] =
          YCbCr444Pixel{
              .y = static_cast<std::int16_t>(ClampCode(y_code, 64, 940)),
              .cb = 512,
              .cr = 512,
          };
    }
  }

  return true;
}

bool TestPatternFrameSource::GeneratePluge(Standard standard,
                                           FrameSourceImage* out_image,
                                           std::string* error) const {
  *out_image = MakeImage(standard, error);
  if (out_image->height <= 0) {
    return false;
  }

  const int pluge_left = (3 * out_image->width) / 16;
  const int pluge_right = (7 * out_image->width) / 16;
  for (int y = 0; y < out_image->height; ++y) {
    for (int x = 0; x < out_image->width; ++x) {
      int y_code = 96;
      if (x >= pluge_left && x < pluge_right && y > 80) {
        const int segment = std::max(1, (pluge_right - pluge_left) / 4);
        const int local_x = x - pluge_left;
        const int band = std::min(3, local_x / segment);
        if (band == 0) {
          y_code = 68;
        } else if (band == 1) {
          y_code = 72;
        } else if (band == 2) {
          y_code = 80;
        } else {
          y_code = 100;
        }
      }

      out_image->pixels[static_cast<std::size_t>((y * out_image->width) + x)] =
          YCbCr444Pixel{.y = static_cast<std::int16_t>(y_code), .cb = 512, .cr = 512};
    }
  }

  return true;
}

}  // namespace videosynth