/*
 * File:        test_progressive_frame_source_probe.cpp
 * Module:      progressive_frame_source_probe_tests
 * Purpose:     Validates progressive source profile probing and frame counting.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "videosynth/progressive_frame_source.h"
#include "videosynth/progressive_frame_source_probe.h"

namespace videosynth {
namespace {

Section MakeProgressiveSection(const std::string& relative_source) {
  Section section;
  section.type = "progressive";
  section.source =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) / relative_source).string();
  return section;
}

TEST(ProgressiveFrameSourceProbeTest, MkvProbeReportsProfileAndFrameCount) {
  ProgressiveFrameSourceProbe probe;
  const Section section = MakeProgressiveSection(
      "videosynth-assets/assets/mkv/720x576/"
      "MOVING_ZONE_2H.mkv");

  ProgressiveFrameSourceProfile profile;
  std::string error;
  ASSERT_TRUE(probe.Probe(section, &profile, &error)) << error;

  EXPECT_EQ(profile.codec, "ffv1");
  EXPECT_EQ(profile.pixel_format, "yuv422p10le");
  EXPECT_EQ(profile.width, 720);
  EXPECT_EQ(profile.height, 576);
  EXPECT_NEAR(profile.frame_rate_hz, 25.0, 1e-6);

  // The probe derives its frame count from container metadata rather than a
  // counting decode pass; it must still agree with the decoded frame count.
  ProgressiveFrameSource frame_source;
  int decoded_frame_count = 0;
  ASSERT_TRUE(frame_source.ResolveFrameCount(section, Standard::kPal,
                                             &decoded_frame_count, &error))
      << error;
  EXPECT_EQ(profile.frame_count, decoded_frame_count);
}

TEST(ProgressiveFrameSourceProbeTest, ExrProbeReportsSingleFrameProfile) {
  ProgressiveFrameSourceProbe probe;
  const Section section = MakeProgressiveSection(
      "videosynth-assets/assets/exr/720x486/100_BARS.exr");

  ProgressiveFrameSourceProfile profile;
  std::string error;
  ASSERT_TRUE(probe.Probe(section, &profile, &error)) << error;

  EXPECT_EQ(profile.container, "exr");
  EXPECT_EQ(profile.width, 720);
  EXPECT_EQ(profile.height, 486);
  EXPECT_EQ(profile.frame_count, 1);
}

TEST(ProgressiveFrameSourceProbeTest, RepeatedProbesOfOneSourceAgree) {
  ProgressiveFrameSourceProbe probe;
  const Section section = MakeProgressiveSection(
      "videosynth-assets/assets/mkv/720x486/"
      "SMPTE_BARS_001.mkv");

  ProgressiveFrameSourceProfile first;
  ProgressiveFrameSourceProfile second;
  std::string error;
  ASSERT_TRUE(probe.Probe(section, &first, &error)) << error;
  // The second probe is served from the memo; it must report the same profile.
  ASSERT_TRUE(probe.Probe(section, &second, &error)) << error;

  EXPECT_EQ(first.container, second.container);
  EXPECT_EQ(first.codec, second.codec);
  EXPECT_EQ(first.width, second.width);
  EXPECT_EQ(first.height, second.height);
  EXPECT_EQ(first.frame_count, second.frame_count);
  EXPECT_NEAR(first.frame_rate_hz, second.frame_rate_hz, 1e-9);
}

TEST(ProgressiveFrameSourceProbeTest, MissingSourceIsReported) {
  ProgressiveFrameSourceProbe probe;
  Section section;
  section.type = "progressive";
  section.source = "/nonexistent/videosynth-probe-target.mkv";

  ProgressiveFrameSourceProfile profile;
  std::string error;
  EXPECT_FALSE(probe.Probe(section, &profile, &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace
}  // namespace videosynth
