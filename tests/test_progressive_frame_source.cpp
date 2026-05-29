/*
 * File:        test_progressive_frame_source.cpp
 * Module:      progressive_frame_source_tests
 * Purpose:     Validates progressive frame-source decoding and normalization.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <string>

#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include "videosynth/progressive_frame_source.h"

namespace videosynth {
namespace {

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
  const std::size_t center_x = image.active_x + (image.active_width / 2);
  const std::size_t center_y = image.active_y + (image.active_height / 2);
  EXPECT_NE(image.PixelAt(center_x, center_y).y, 64);
}

TEST(FrameSourceTest, DecodesProgressivePalExrSource) {
  ProgressiveFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/720x576/stills/exr/100_BARS.exr")
          .string();

  ASSERT_TRUE(frame_source.GenerateFrame(section, 0, Standard::kPal, &image, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(image.width, 720);
  EXPECT_EQ(image.height, 576);
  EXPECT_EQ(image.active_x, 9);
  EXPECT_EQ(image.active_width, 702);
  EXPECT_NE(image.PixelAt(image.active_x, image.active_y).y, 64);
}

TEST(FrameSourceTest, DecodesProgressiveNtscExrSource) {
  ProgressiveFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/720x480/stills/exr/100_BARS.exr")
          .string();

  ASSERT_TRUE(frame_source.GenerateFrame(section, 0, Standard::kNtsc, &image, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(image.width, 720);
  EXPECT_EQ(image.height, 480);
  EXPECT_EQ(image.active_x, 4);
  EXPECT_EQ(image.active_width, 711);
  const std::size_t center_x = image.active_x + (image.active_width / 2);
  const std::size_t center_y = image.active_y + (image.active_height / 2);
  EXPECT_NE(image.PixelAt(center_x, center_y).y, 64);
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

TEST(FrameSourceTest, RejectsProgressiveRawSourceFamily) {
  ProgressiveFrameSource frame_source;
  FrameSourceImage image;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/720x576/stills/exr/100_BARS.exr")
          .string();
  section.source.replace(section.source.size() - 4, 4, ".raw");

  EXPECT_FALSE(frame_source.GenerateFrame(section, 0, Standard::kPal, &image, &error));
  EXPECT_FALSE(error.empty());
}

TEST(FrameSourceTest, DecodesProgressivePalMp4SourceFrames) {
  ProgressiveFrameSource frame_source;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/720x576/video/mp4_25_00/nynashamn.mp4")
          .string();

  int frame_count = 0;
  ASSERT_TRUE(frame_source.ResolveFrameCount(section, Standard::kPal, &frame_count, &error));
  ASSERT_GT(frame_count, 0);

  FrameSourceImage first_frame;
  ASSERT_TRUE(frame_source.GenerateFrame(section, 0, Standard::kPal, &first_frame, &error));
  EXPECT_EQ(first_frame.width, 720);
  EXPECT_EQ(first_frame.height, 576);
  EXPECT_EQ(first_frame.active_x, 9);
  EXPECT_EQ(first_frame.active_width, 702);

  FrameSourceImage last_frame;
  ASSERT_TRUE(frame_source.GenerateFrame(section,
                                         frame_count - 1,
                                         Standard::kPal,
                                         &last_frame,
                                         &error));
  EXPECT_EQ(last_frame.width, 720);
  EXPECT_EQ(last_frame.height, 576);
}

TEST(FrameSourceTest, RejectsProgressiveMp4FrameIndexOutOfRange) {
  ProgressiveFrameSource frame_source;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/720x480/video/mp4_29_97/nynashamn.mp4")
          .string();

  int frame_count = 0;
  ASSERT_TRUE(frame_source.ResolveFrameCount(section, Standard::kNtsc, &frame_count, &error));
  ASSERT_GT(frame_count, 0);

  FrameSourceImage out_of_range;
  EXPECT_FALSE(frame_source.GenerateFrame(section,
                                          frame_count,
                                          Standard::kNtsc,
                                          &out_of_range,
                                          &error));
  EXPECT_FALSE(error.empty());
}

TEST(FrameSourceTest, DecodesProgressivePalMovSourceFrames) {
  ProgressiveFrameSource frame_source;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/720x576/video/mov_25_00/Moving-Zone-2H.mov")
          .string();

  int frame_count = 0;
  ASSERT_TRUE(frame_source.ResolveFrameCount(section, Standard::kPal, &frame_count, &error));
  ASSERT_GT(frame_count, 0);

  FrameSourceImage first_frame;
  ASSERT_TRUE(frame_source.GenerateFrame(section, 0, Standard::kPal, &first_frame, &error));
  EXPECT_EQ(first_frame.width, 720);
  EXPECT_EQ(first_frame.height, 576);
  EXPECT_EQ(first_frame.active_x, 9);
  EXPECT_EQ(first_frame.active_width, 702);
}

TEST(FrameSourceTest, DecodesProgressiveNtscMovSourceFrames) {
  ProgressiveFrameSource frame_source;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/704x480/video/mov_29_97/MOVING_ZONE_2H.mov")
          .string();

  int frame_count = 0;
  ASSERT_TRUE(frame_source.ResolveFrameCount(section, Standard::kNtsc, &frame_count, &error));
  ASSERT_GT(frame_count, 0);

  FrameSourceImage first_frame;
  ASSERT_TRUE(frame_source.GenerateFrame(section, 0, Standard::kNtsc, &first_frame, &error));
  EXPECT_EQ(first_frame.width, 720);
  EXPECT_EQ(first_frame.height, 480);
  EXPECT_EQ(first_frame.active_x, 4);
  EXPECT_EQ(first_frame.active_width, 711);
  EXPECT_NE(first_frame.PixelAt(0, 0).y, first_frame.PixelAt(8, 0).y);
}

TEST(FrameSourceTest, RejectsProgressiveMovFrameIndexOutOfRange) {
  ProgressiveFrameSource frame_source;
  std::string error;

  Section section;
  section.type = "progressive";
  section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/704x480/video/mov_29_97/MOVING_ZONE_2H.mov")
          .string();

  int frame_count = 0;
  ASSERT_TRUE(frame_source.ResolveFrameCount(section, Standard::kNtsc, &frame_count, &error));
  ASSERT_GT(frame_count, 0);

  FrameSourceImage out_of_range;
  EXPECT_FALSE(frame_source.GenerateFrame(section,
                                          frame_count,
                                          Standard::kNtsc,
                                          &out_of_range,
                                          &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace
}  // namespace videosynth