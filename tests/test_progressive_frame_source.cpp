/*
 * File:        test_progressive_frame_source.cpp
 * Module:      progressive_frame_source_tests
 * Purpose:     Validates progressive frame-source decoding and normalization.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "videosynth/progressive_frame_source.h"

namespace videosynth {
namespace {

void ExpectCodesWithinStudioRange(const FrameSourceImage& image) {
  for (int y = image.active_y; y < image.active_y + image.active_height; ++y) {
    for (int x = image.active_x; x < image.active_x + image.active_width; ++x) {
      const YCbCr444Pixel& pixel = image.PixelAt(x, y);
      EXPECT_GE(pixel.y, 64) << "x=" << x << " y=" << y;
      EXPECT_LE(pixel.y, 940) << "x=" << x << " y=" << y;
      EXPECT_GE(pixel.cb, 64) << "x=" << x << " y=" << y;
      EXPECT_LE(pixel.cb, 960) << "x=" << x << " y=" << y;
      EXPECT_GE(pixel.cr, 64) << "x=" << x << " y=" << y;
      EXPECT_LE(pixel.cr, 960) << "x=" << x << " y=" << y;
    }
  }
}

TEST(FrameSourceTest, DecodesProgressivePalExrSource) {
  ProgressiveFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source = (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
                    "videosynth-assets/assets/exr/720x576/100_BARS.exr")
                       .string();

  ASSERT_TRUE(
      frame_source.GenerateFrame(section, 0, Standard::kPal, &image, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(image.width, 720);
  EXPECT_EQ(image.height, 576);
  EXPECT_EQ(image.active_x, 0);
  EXPECT_EQ(image.active_y, 0);
  EXPECT_EQ(image.active_width, 720);
  EXPECT_EQ(image.active_height, 576);
  const std::size_t center_x = image.active_x + (image.active_width / 2);
  const std::size_t center_y = image.active_y + (image.active_height / 2);
  EXPECT_NE(image.PixelAt(center_x, center_y).y, 64);
}

TEST(FrameSourceTest, DecodesProgressiveNtscExrSource) {
  ProgressiveFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source = (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
                    "videosynth-assets/assets/exr/720x486/100_BARS.exr")
                       .string();

  ASSERT_TRUE(
      frame_source.GenerateFrame(section, 0, Standard::kNtsc, &image, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(image.width, 720);
  EXPECT_EQ(image.height, 486);
  EXPECT_EQ(image.active_x, 0);
  EXPECT_EQ(image.active_y, 0);
  EXPECT_EQ(image.active_width, 720);
  EXPECT_EQ(image.active_height, 486);
  const std::size_t center_x = image.active_x + (image.active_width / 2);
  const std::size_t center_y = image.active_y + (image.active_height / 2);
  EXPECT_NE(image.PixelAt(center_x, center_y).y, 64);
}

TEST(FrameSourceTest, RejectsProgressiveRawSourceFamily) {
  ProgressiveFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source = (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
                    "videosynth-assets/assets/exr/720x576/100_BARS.exr")
                       .string();
  section.source.replace(section.source.size() - 4, 4, ".raw");

  EXPECT_FALSE(
      frame_source.GenerateFrame(section, 0, Standard::kPal, &image, &error));
  EXPECT_FALSE(error.empty());
}

TEST(FrameSourceTest, DecodesProgressivePalMkvSourceFrames) {
  ProgressiveFrameSource frame_source;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source = (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
                    "videosynth-assets/assets/mkv/720x576/MOVING_ZONE_2H.mkv")
                       .string();

  int frame_count = 0;
  ASSERT_TRUE(frame_source.ResolveFrameCount(section, Standard::kPal,
                                             &frame_count, &error));
  ASSERT_GT(frame_count, 0);

  FrameSourceImage first_frame;
  ASSERT_TRUE(frame_source.GenerateFrame(section, 0, Standard::kPal,
                                         &first_frame, &error));
  EXPECT_EQ(first_frame.width, 720);
  EXPECT_EQ(first_frame.height, 576);
  EXPECT_EQ(first_frame.active_x, 0);
  EXPECT_EQ(first_frame.active_y, 0);
  EXPECT_EQ(first_frame.active_width, 720);
  EXPECT_EQ(first_frame.active_height, 576);
}

TEST(FrameSourceTest, DecodesProgressiveNtscMkvSourceFrames) {
  ProgressiveFrameSource frame_source;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source = (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
                    "videosynth-assets/assets/mkv/720x486/MOVING_ZONE_2H.mkv")
                       .string();

  int frame_count = 0;
  ASSERT_TRUE(frame_source.ResolveFrameCount(section, Standard::kNtsc,
                                             &frame_count, &error));
  ASSERT_GT(frame_count, 0);

  FrameSourceImage first_frame;
  ASSERT_TRUE(frame_source.GenerateFrame(section, 0, Standard::kNtsc,
                                         &first_frame, &error));
  EXPECT_EQ(first_frame.width, 720);
  EXPECT_EQ(first_frame.height, 486);
  EXPECT_EQ(first_frame.active_x, 0);
  EXPECT_EQ(first_frame.active_y, 0);
  EXPECT_EQ(first_frame.active_width, 720);
  EXPECT_EQ(first_frame.active_height, 486);
  EXPECT_NE(first_frame.PixelAt(0, 0).y, first_frame.PixelAt(8, 0).y);
}

TEST(FrameSourceTest, NtscMkvPreservesFullRasterActiveGeometry) {
  ProgressiveFrameSource frame_source;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source = (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
                    "videosynth-assets/assets/mkv/720x486/MOVING_ZONE_2H.mkv")
                       .string();

  FrameSourceImage frame;
  ASSERT_TRUE(
      frame_source.GenerateFrame(section, 0, Standard::kNtsc, &frame, &error));
  EXPECT_TRUE(error.empty());

  EXPECT_EQ(frame.width, 720);
  EXPECT_EQ(frame.height, 486);
  EXPECT_EQ(frame.active_x, 0);
  EXPECT_EQ(frame.active_width, 720);
  EXPECT_EQ(frame.active_y, 0);
  EXPECT_EQ(frame.active_height, 486);
}

TEST(FrameSourceTest, PalAndNtscMkvDecodedPixelsStayWithinStudioCodeRange) {
  ProgressiveFrameSource frame_source;
  std::string error;

  Section pal_section;
  pal_section.type = "progressive";
  pal_section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "videosynth-assets/assets/mkv/720x576/MOVING_ZONE_2H.mkv")
          .string();
  FrameSourceImage pal_frame;
  ASSERT_TRUE(frame_source.GenerateFrame(pal_section, 0, Standard::kPal,
                                         &pal_frame, &error));
  EXPECT_TRUE(error.empty());
  ExpectCodesWithinStudioRange(pal_frame);

  Section ntsc_section;
  ntsc_section.type = "progressive";
  ntsc_section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "videosynth-assets/assets/mkv/720x486/MOVING_ZONE_2H.mkv")
          .string();
  FrameSourceImage ntsc_frame;
  ASSERT_TRUE(frame_source.GenerateFrame(ntsc_section, 0, Standard::kNtsc,
                                         &ntsc_frame, &error));
  EXPECT_TRUE(error.empty());
  ExpectCodesWithinStudioRange(ntsc_frame);
}

TEST(FrameSourceTest, RejectsProgressiveMkvFrameIndexOutOfRange) {
  ProgressiveFrameSource frame_source;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source = (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
                    "videosynth-assets/assets/mkv/720x486/MOVING_ZONE_2H.mkv")
                       .string();

  int frame_count = 0;
  ASSERT_TRUE(frame_source.ResolveFrameCount(section, Standard::kNtsc,
                                             &frame_count, &error));
  ASSERT_GT(frame_count, 0);

  FrameSourceImage out_of_range;
  EXPECT_FALSE(frame_source.GenerateFrame(section, frame_count, Standard::kNtsc,
                                          &out_of_range, &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace
}  // namespace videosynth