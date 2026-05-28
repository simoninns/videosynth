/*
 * File:        frame_source.h
 * Module:      frame_source
 * Purpose:     Defines fixed-format frame-source buffers for frame-based content.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "videosynth/model.h"

namespace videosynth {

struct YCbCr444Pixel {
  std::int16_t y = 64;
  std::int16_t cb = 512;
  std::int16_t cr = 512;
};

struct FrameSourceImage {
  int width = 0;
  int height = 0;
  int active_x = 0;
  int active_y = 0;
  int active_width = 0;
  int active_height = 0;
  std::vector<YCbCr444Pixel> pixels;

  const YCbCr444Pixel& PixelAt(int x, int y) const;
};

class TestPatternFrameSource {
 public:
  bool SupportsPattern(const std::string& pattern) const;

  bool GenerateFrame(const std::string& pattern,
                     Standard standard,
                     FrameSourceImage* out_image,
                     std::string* error) const;
};

}  // namespace videosynth