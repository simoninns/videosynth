/*
 * File:        test_frame_source.cpp
 * Module:      frame_source_tests
 * Purpose:     Validates fixed-format frame generation for software patterns.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <string>

#include <filesystem>

#include <gtest/gtest.h>

#include "videosynth/frame_source.h"

namespace videosynth {
namespace {

TEST(FrameSourceTest, GeneratesFixedFormatPalFrameDimensions) {
  TestPatternFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  ASSERT_TRUE(frame_source.GenerateFrame(
      "pal_ebu_colour_bars_100", Standard::kPal, &image, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(image.width, 720);
  EXPECT_EQ(image.height, 576);
  EXPECT_EQ(image.active_x, 9);
  EXPECT_EQ(image.active_y, 0);
  EXPECT_EQ(image.active_width, 702);
  EXPECT_EQ(image.active_height, 576);
  ASSERT_EQ(image.pixels.size(), static_cast<std::size_t>(720 * 576));
}

TEST(FrameSourceTest, GeneratesEbuColourBarsWithFullWhiteReference) {
  TestPatternFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  ASSERT_TRUE(frame_source.GenerateFrame(
      "pal_ebu_colour_bars_100", Standard::kPal, &image, &error));
  EXPECT_TRUE(error.empty());

  const YCbCr444Pixel& margin = image.PixelAt(0, 0);
  EXPECT_EQ(margin.y, 64);
  const YCbCr444Pixel& white_bar = image.PixelAt(image.active_x, 0);
  EXPECT_EQ(white_bar.y, 940);
  EXPECT_EQ(white_bar.cb, 512);
  EXPECT_EQ(white_bar.cr, 512);
}

TEST(FrameSourceTest, GeneratesSmpteNtscBars100And75Variants) {
  TestPatternFrameSource frame_source;
  FrameSourceImage bars_100;
  FrameSourceImage bars_75;
  std::string error;

  ASSERT_TRUE(frame_source.GenerateFrame(
      "ntsc_smpte_170m_colour_bars_100", Standard::kNtsc, &bars_100, &error));
  ASSERT_TRUE(frame_source.GenerateFrame(
      "ntsc_smpte_170m_colour_bars_75", Standard::kNtsc, &bars_75, &error));
  EXPECT_TRUE(error.empty());

  const YCbCr444Pixel& top_yellow = bars_100.PixelAt(150, 0);
  EXPECT_EQ(top_yellow.y, 840);
  EXPECT_EQ(top_yellow.cb, 64);
  EXPECT_EQ(top_yellow.cr, 585);

  const YCbCr444Pixel& top_yellow_75 = bars_75.PixelAt(150, 0);
  EXPECT_EQ(top_yellow_75.y, 648);
  EXPECT_EQ(top_yellow_75.cb, 176);
  EXPECT_EQ(top_yellow_75.cr, 568);
}

TEST(FrameSourceTest, KeepsPixelsInsideBt601StudioSwingContract) {
  TestPatternFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  ASSERT_TRUE(frame_source.GenerateFrame(
      "ntsc_linear_grayscale_ramp_horizontal", Standard::kNtsc, &image, &error));

  for (const YCbCr444Pixel& pixel : image.pixels) {
    EXPECT_GE(pixel.y, 64);
    EXPECT_LE(pixel.y, 940);
    EXPECT_GE(pixel.cb, 64);
    EXPECT_LE(pixel.cb, 960);
    EXPECT_GE(pixel.cr, 64);
    EXPECT_LE(pixel.cr, 960);
  }
}

TEST(FrameSourceTest, GeneratesVerticalGrayscaleRampEndpoints) {
  TestPatternFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  ASSERT_TRUE(frame_source.GenerateFrame(
      "ntsc_linear_grayscale_ramp_vertical", Standard::kNtsc, &image, &error));
  EXPECT_EQ(image.PixelAt(image.active_x, 0).y, 64);
  EXPECT_EQ(image.PixelAt(image.active_x, image.height - 1).y, 940);
}

TEST(FrameSourceTest, GeneratesLumaCheckerboard8x8) {
  TestPatternFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  ASSERT_TRUE(frame_source.GenerateFrame("pal_luma_checkerboard_8x8", Standard::kPal, &image, &error));
  EXPECT_EQ(image.PixelAt(image.active_x, 0).y, 940);
  EXPECT_EQ(image.PixelAt(image.active_x + 8, 0).y, 64);
  EXPECT_EQ(image.PixelAt(image.active_x, 8).y, 64);
}

TEST(FrameSourceTest, GeneratesLumaCheckerboard16x16) {
  TestPatternFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  ASSERT_TRUE(frame_source.GenerateFrame("ntsc_luma_checkerboard_16x16", Standard::kNtsc, &image, &error));
  EXPECT_EQ(image.PixelAt(image.active_x, 0).y, 940);
  EXPECT_EQ(image.PixelAt(image.active_x + 16, 0).y, 64);
  EXPECT_EQ(image.PixelAt(image.active_x, 16).y, 64);
}

TEST(FrameSourceTest, GeneratesFullFieldLevels) {
  TestPatternFrameSource frame_source;
  FrameSourceImage black_image;
  FrameSourceImage white_image;
  std::string error;

  ASSERT_TRUE(frame_source.GenerateFrame("pal_full_field_black", Standard::kPal, &black_image, &error));
  ASSERT_TRUE(frame_source.GenerateFrame("pal_full_field_white", Standard::kPal, &white_image, &error));
  EXPECT_EQ(black_image.PixelAt(100, 100).y, 64);
  EXPECT_EQ(white_image.PixelAt(100, 100).y, 940);
}

TEST(FrameSourceTest, GeneratesPlugeNearBlackWindowLevels) {
  TestPatternFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  ASSERT_TRUE(frame_source.GenerateFrame("pal_pluge_5patch_near_black", Standard::kPal, &image, &error));

  const int x0 = 149;
  const int y0 = 432;
  const int x1 = 570;
  const int width = x1 - x0 + 1;
  const int base_width = width / 5;
  const int remainder = width % 5;
  const int first_width = base_width + (remainder > 0 ? 1 : 0);
  const int second_width = base_width + (remainder > 1 ? 1 : 0);
  const int first_end = x0 + first_width - 1;
  const int second_end = first_end + second_width;

  EXPECT_EQ(image.PixelAt(x0, y0).y, 64);
  EXPECT_EQ(image.PixelAt(first_end + 1, y0).y, 48);
  EXPECT_EQ(image.PixelAt(second_end + 1, y0).y, 64);
}

TEST(FrameSourceTest, GeneratesCrosshatchWithinVisibleArea) {
  TestPatternFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  ASSERT_TRUE(frame_source.GenerateFrame("pal_crosshatch_visible_area_grid", Standard::kPal, &image, &error));
  EXPECT_EQ(image.PixelAt(0, 10).y, 64);
  EXPECT_EQ(image.PixelAt(9, 10).y, 940);
  EXPECT_EQ(image.PixelAt(87, 10).y, 940);
  EXPECT_EQ(image.PixelAt(9, 72).y, 940);
  EXPECT_EQ(image.PixelAt(321, 20).y, 940);
  EXPECT_EQ(image.PixelAt(20, 288).y, 940);
  EXPECT_EQ(image.PixelAt(710, 20).y, 940);
  EXPECT_EQ(image.PixelAt(719, 20).y, 64);
  EXPECT_EQ(image.PixelAt(20, 575).y, 940);
}

TEST(FrameSourceTest, RejectsNtscBarsPatternForPal) {
  TestPatternFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  EXPECT_FALSE(frame_source.GenerateFrame(
      "ntsc_smpte_170m_colour_bars_100", Standard::kPal, &image, &error));
  EXPECT_FALSE(error.empty());
}

TEST(FrameSourceTest, RejectsPalBarsPatternForNtsc) {
  TestPatternFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  EXPECT_FALSE(frame_source.GenerateFrame(
      "pal_ebu_colour_bars_100", Standard::kNtsc, &image, &error));
  EXPECT_FALSE(error.empty());
}

TEST(FrameSourceTest, DecodesProgressivePalPngSource) {
  ProgressiveFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/720x576/stills/png/Color-Bars-Hori-Cont.png")
          .string();

  ASSERT_TRUE(frame_source.GenerateFrame(section, 0, Standard::kPal, &image, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(image.width, 720);
  EXPECT_EQ(image.height, 576);
  EXPECT_EQ(image.active_x, 9);
  EXPECT_EQ(image.active_width, 702);
  EXPECT_NE(image.PixelAt(image.active_x, image.active_y).y, 64);
}

TEST(FrameSourceTest, DecodesProgressiveNtscPngSource) {
  ProgressiveFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/720x480/stills/png/Color-Bars-Hori-Cont.png")
          .string();

  ASSERT_TRUE(frame_source.GenerateFrame(section, 0, Standard::kNtsc, &image, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(image.width, 720);
  EXPECT_EQ(image.height, 480);
  EXPECT_EQ(image.active_x, 4);
  EXPECT_EQ(image.active_width, 711);
  EXPECT_NE(image.PixelAt(image.active_x, image.active_y).y, 64);
}

TEST(FrameSourceTest, RejectsProgressivePngWithInvalidRaster) {
  ProgressiveFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/doc-diagrams/DependencyInversionPattern.png")
          .string();

  EXPECT_FALSE(frame_source.GenerateFrame(section, 0, Standard::kPal, &image, &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace
}  // namespace videosynth