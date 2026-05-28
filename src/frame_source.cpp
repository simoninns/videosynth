/*
 * File:        frame_source.cpp
 * Module:      frame_source
 * Purpose:     Generates fixed-format 10-bit 4:4:4 BT.601 frame data for frame-based sources.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/frame_source.h"

#include "videosynth/ntsc_pattern_generator.h"
#include "videosynth/pal_pattern_generator.h"

namespace videosynth {

const YCbCr444Pixel& FrameSourceImage::PixelAt(int x, int y) const {
  return pixels[static_cast<std::size_t>((y * width) + x)];
}

bool TestPatternFrameSource::SupportsPattern(const std::string& pattern) const {
  return IsSupportedPalPattern(pattern) || IsSupportedNtscPattern(pattern);
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

  if (standard == Standard::kPal) {
    if (!IsSupportedPalPattern(pattern)) {
      if (error != nullptr) {
        *error = "Pattern is not valid for PAL projects.";
      }
      return false;
    }
    return GeneratePalPatternFrame(pattern, out_image, error);
  }

  if (standard == Standard::kNtsc) {
    if (!IsSupportedNtscPattern(pattern)) {
      if (error != nullptr) {
        *error = "Pattern is not valid for NTSC projects.";
      }
      return false;
    }
    return GenerateNtscPatternFrame(pattern, out_image, error);
  }

  if (error != nullptr) {
    *error = "Unsupported video standard for frame source generation.";
  }
  return false;
}

}  // namespace videosynth