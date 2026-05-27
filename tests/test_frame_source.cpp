/*
 * File:        test_frame_source.cpp
 * Module:      frame_source_tests
 * Purpose:     Validates fixed-format frame generation for software patterns.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <string>

#include <gtest/gtest.h>

#include "videosynth/frame_source.h"

namespace videosynth {
namespace {

TEST(FrameSourceTest, GeneratesFixedFormatPalFrameDimensions) {
  TestPatternFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  ASSERT_TRUE(frame_source.GenerateFrame("ebu_colour_bars", Standard::kPal, &image, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(image.width, 720);
  EXPECT_EQ(image.height, 576);
  ASSERT_EQ(image.pixels.size(), static_cast<std::size_t>(720 * 576));
}

TEST(FrameSourceTest, KeepsPixelsInsideBt601StudioSwingContract) {
  TestPatternFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  ASSERT_TRUE(frame_source.GenerateFrame(
      "grayscale_ramp_horizontal", Standard::kNtsc, &image, &error));

  for (const YCbCr444Pixel& pixel : image.pixels) {
    EXPECT_GE(pixel.y, 64);
    EXPECT_LE(pixel.y, 940);
    EXPECT_GE(pixel.cb, 64);
    EXPECT_LE(pixel.cb, 960);
    EXPECT_GE(pixel.cr, 64);
    EXPECT_LE(pixel.cr, 960);
  }
}

}  // namespace
}  // namespace videosynth