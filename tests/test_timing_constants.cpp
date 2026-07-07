/*
 * File:        test_timing_constants.cpp
 * Module:      timing_constants_tests
 * Purpose:     Validates PAL and NTSC timing and signal level constants.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include "videosynth/timing_constants.h"

namespace videosynth {
namespace {

TEST(TimingConstantsTest, ProvidesPal4fscConstants) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);

  EXPECT_EQ(pal.lines_per_frame, 625);
  EXPECT_EQ(pal.samples_per_line_4fsc, 1135);
  EXPECT_DOUBLE_EQ(pal.frame_rate_hz, 25.0);
  EXPECT_DOUBLE_EQ(pal.sample_rate_4fsc_hz, 17734475.0);
}

TEST(TimingConstantsTest, ProvidesNtsc4fscConstants) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);

  EXPECT_EQ(ntsc.lines_per_frame, 525);
  EXPECT_EQ(ntsc.samples_per_line_4fsc, 910);
  EXPECT_DOUBLE_EQ(ntsc.frame_rate_hz, 30000.0 / 1001.0);
  EXPECT_DOUBLE_EQ(ntsc.sample_rate_4fsc_hz, 14318180.0);
}

TEST(SignalLevelsTest, ProvidesPalLevels) {
  const SignalLevels levels = GetSignalLevels(Standard::kPal);

  EXPECT_DOUBLE_EQ(levels.sync_tip_mv, -300.0);
  EXPECT_DOUBLE_EQ(levels.blanking_mv, 0.0);
  EXPECT_DOUBLE_EQ(levels.black_mv, 0.0);
  EXPECT_DOUBLE_EQ(levels.white_mv, 700.0);
}

TEST(SignalLevelsTest, ProvidesNtscLevels) {
  const SignalLevels levels = GetSignalLevels(Standard::kNtsc);

  EXPECT_DOUBLE_EQ(levels.sync_tip_mv, -285.7);
  EXPECT_DOUBLE_EQ(levels.blanking_mv, 0.0);
  EXPECT_DOUBLE_EQ(levels.black_mv, 53.6);
  EXPECT_DOUBLE_EQ(levels.white_mv, 714.3);
}

TEST(SignalLevelsTest, SupportsZeroIreNtscBlackSetup) {
  CvbsPresets presets;
  presets.video_standard_preset = Standard::kNtsc;
  presets.ntsc_black_setup_ire = 0.0;
  presets.ntsc_black_setup_ire_specified = true;

  const SignalLevels levels = GetSignalLevels(presets);

  EXPECT_DOUBLE_EQ(levels.sync_tip_mv, -285.7);
  EXPECT_DOUBLE_EQ(levels.blanking_mv, 0.0);
  EXPECT_DOUBLE_EQ(levels.black_mv, 0.0);
  EXPECT_DOUBLE_EQ(levels.white_mv, 714.3);
}

TEST(TimingConstantsTest,
     ResolvesSampleFrameCountsForSupportedOutputEncodings) {
  EXPECT_EQ(SamplesPerFrameForEncodingPreset(Standard::kPal, "CVBS_U16_4FSC"),
            static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal)));
  EXPECT_EQ(SamplesPerFrameForEncodingPreset(Standard::kPal, "RAW_S16_28M"),
            1120000U);
  EXPECT_EQ(SamplesPerFrameForEncodingPreset(Standard::kPal, "RAW_S16_40M"),
            1600000U);

  EXPECT_EQ(SamplesPerFrameForEncodingPreset(Standard::kNtsc, "RAW_S16_28M"),
            934267U);
  EXPECT_EQ(SamplesPerFrameForEncodingPreset(Standard::kNtsc, "RAW_S16_40M"),
            1334667U);
}

TEST(TimingConstantsTest, ProvidesPalAudioRateAndSamplesPerFrame) {
  EXPECT_DOUBLE_EQ(AudioSampleRateHz(Standard::kPal), 44100.0);
  EXPECT_EQ(AudioHeaderSampleRateHz(Standard::kPal), 44100);
  EXPECT_EQ(AudioSamplesPerFrame(Standard::kPal), 1764);
}

TEST(TimingConstantsTest, ProvidesNtscAudioRateAndSamplesPerFrame) {
  EXPECT_DOUBLE_EQ(AudioSampleRateHz(Standard::kNtsc), 44100000.0 / 1001.0);
  EXPECT_EQ(AudioHeaderSampleRateHz(Standard::kNtsc), 44056);
  EXPECT_EQ(AudioSamplesPerFrame(Standard::kNtsc), 1470);
}

TEST(TimingConstantsTest, ProvidesPalMAudioRateAndSamplesPerFrame) {
  EXPECT_DOUBLE_EQ(AudioSampleRateHz(Standard::kPalM), 44100000.0 / 1001.0);
  EXPECT_EQ(AudioHeaderSampleRateHz(Standard::kPalM), 44056);
  EXPECT_EQ(AudioSamplesPerFrame(Standard::kPalM), 1470);
}

}  // namespace
}  // namespace videosynth
