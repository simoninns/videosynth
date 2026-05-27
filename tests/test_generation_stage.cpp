/*
 * File:        test_generation_stage.cpp
 * Module:      generation_stage_tests
 * Purpose:     Validates generated sync, burst, and picture timing behavior.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "videosynth/generation_stage.h"
#include "videosynth/timing_constants.h"

namespace videosynth {
namespace {

int BurstStartSamples(double sample_rate_hz) {
  return static_cast<int>(std::lround(sample_rate_hz * 5.6e-6));
}

int BurstEndSamples(double sample_rate_hz) {
  return static_cast<int>(std::lround(sample_rate_hz * 8.0e-6));
}

Project MakeProject(Standard standard,
                    const std::string& pattern = "ebu_colour_bars",
                    int duration_frames = 1) {
  Project project;
  project.cvbs_presets.video_standard_preset = standard;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  project.sections.push_back(
      Section{.name = "SignalTiming",
              .type = "software_generated",
              .pattern = pattern,
              .duration_frames = duration_frames});
  return project;
}

int ActiveWindowStartSamples(double sample_rate_hz) {
  return static_cast<int>(std::lround(sample_rate_hz * 10.5e-6));
}

int ActiveWindowEndSamples(double sample_rate_hz) {
  return static_cast<int>(std::lround(sample_rate_hz * 62.5e-6));
}

std::set<int> UniqueRoundedLumaLevelsInActiveWindow(const std::vector<double>& y_mv,
                                                    int line_1based,
                                                    const TimingConstants& timing) {
  const int line_start = (line_1based - 1) * timing.samples_per_line_4fsc;
  const int start = line_start + ActiveWindowStartSamples(timing.sample_rate_4fsc_hz);
  const int end = std::min(line_start + ActiveWindowEndSamples(timing.sample_rate_4fsc_hz),
                           line_start + timing.samples_per_line_4fsc);

  std::set<int> levels;
  for (int i = start; i < end; ++i) {
    levels.insert(static_cast<int>(std::lround(y_mv[i])));
  }
  return levels;
}

int CountSyncSamplesOnLine(const std::vector<double>& y_mv,
                           int line_1based,
                           const TimingConstants& timing,
                           double sync_tip_mv) {
  const int line_start = (line_1based - 1) * timing.samples_per_line_4fsc;
  const int line_end = line_start + timing.samples_per_line_4fsc;

  int count = 0;
  for (int i = line_start; i < line_end; ++i) {
    if (y_mv[i] == sync_tip_mv) {
      ++count;
    }
  }
  return count;
}

int CountSyncSamplesInHalfLine(const std::vector<double>& y_mv,
                               int line_1based,
                               int half_index,
                               const TimingConstants& timing,
                               double sync_tip_mv) {
  const int half_size = timing.samples_per_line_4fsc / 2;
  const int line_start = (line_1based - 1) * timing.samples_per_line_4fsc;
  const int start = line_start + (half_index * half_size);
  const int end = std::min(start + half_size, line_start + timing.samples_per_line_4fsc);

  int count = 0;
  for (int i = start; i < end; ++i) {
    if (y_mv[i] == sync_tip_mv) {
      ++count;
    }
  }
  return count;
}

double BurstWindowMean(const std::vector<double>& c_mv,
                       int line_1based,
                       const TimingConstants& timing) {
  const int line_start = (line_1based - 1) * timing.samples_per_line_4fsc;
  const int start = line_start + BurstStartSamples(timing.sample_rate_4fsc_hz);
  const int end = line_start + BurstEndSamples(timing.sample_rate_4fsc_hz);

  double sum = 0.0;
  int count = 0;
  for (int i = start; i < end; ++i) {
    sum += c_mv[i];
    ++count;
  }
  return count > 0 ? (sum / static_cast<double>(count)) : 0.0;
}

TEST(GenerationStageTimingTest, ProducesDeterministicSampleCounts) {
  GenerationStage generation;
  std::vector<std::string> errors;

  std::vector<double> y_pal;
  std::vector<double> c_pal;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal), &y_pal, &c_pal, &errors));
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  EXPECT_EQ(y_pal.size(), static_cast<std::size_t>(pal.lines_per_frame * pal.samples_per_line_4fsc));
  EXPECT_EQ(c_pal.size(), y_pal.size());

  std::vector<double> y_ntsc;
  std::vector<double> c_ntsc;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y_ntsc, &c_ntsc, &errors));
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  EXPECT_EQ(ntsc.samples_per_line_4fsc, 910);
  EXPECT_EQ(y_ntsc.size(),
            static_cast<std::size_t>(ntsc.lines_per_frame * ntsc.samples_per_line_4fsc));
  EXPECT_EQ(c_ntsc.size(), y_ntsc.size());
}

TEST(GenerationStageTimingTest, BuildsDifferentPulseWidthsForEqualizingAndBroadPulses) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const SignalLevels levels = GetSignalLevels(Standard::kNtsc);

  const int horizontal_sync = CountSyncSamplesOnLine(y, 20, ntsc, levels.sync_tip_mv);
  const int equalizing_sync = CountSyncSamplesOnLine(y, 2, ntsc, levels.sync_tip_mv);
  const int broad_sync = CountSyncSamplesOnLine(y, 5, ntsc, levels.sync_tip_mv);

  EXPECT_GT(horizontal_sync, equalizing_sync);
  EXPECT_GT(broad_sync, horizontal_sync);
}

TEST(GenerationStageTimingTest, AppliesTwoHalfLinePulsesToEqualizingAndVerticalSyncLines) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal), &y, &c, &errors));

  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const SignalLevels levels = GetSignalLevels(Standard::kPal);

  const int eq_first_half = CountSyncSamplesInHalfLine(y, 1, 0, pal, levels.sync_tip_mv);
  const int eq_second_half = CountSyncSamplesInHalfLine(y, 1, 1, pal, levels.sync_tip_mv);
  EXPECT_GT(eq_first_half, 0);
  EXPECT_GT(eq_second_half, 0);

  const int vs_first_half = CountSyncSamplesInHalfLine(y, 6, 0, pal, levels.sync_tip_mv);
  const int vs_second_half = CountSyncSamplesInHalfLine(y, 6, 1, pal, levels.sync_tip_mv);
  EXPECT_GT(vs_first_half, 0);
  EXPECT_GT(vs_second_half, 0);

  const int h_second_half = CountSyncSamplesInHalfLine(y, 23, 1, pal, levels.sync_tip_mv);
  EXPECT_EQ(h_second_half, 0);
}

TEST(GenerationStageTimingTest, IncludesEndOfFrameNtscVerticalTransitionBlock) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const SignalLevels levels = GetSignalLevels(Standard::kNtsc);

  const int line522 = CountSyncSamplesOnLine(y, 522, ntsc, levels.sync_tip_mv);
  const int line523 = CountSyncSamplesOnLine(y, 523, ntsc, levels.sync_tip_mv);
  const int line520 = CountSyncSamplesOnLine(y, 520, ntsc, levels.sync_tip_mv);

  EXPECT_GT(line522, 0);
  EXPECT_GT(line520, 0);
  EXPECT_GT(line523, line522);
}

TEST(GenerationStageTimingTest, EmitsBurstOnHorizontalButNotBroadSyncLines) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);

  const double normal_line_mean = std::abs(BurstWindowMean(c, 20, ntsc));
  const double broad_line_mean = std::abs(BurstWindowMean(c, 5, ntsc));

  EXPECT_GT(normal_line_mean, 0.1);
  EXPECT_LT(broad_line_mean, 1e-9);
}

TEST(GenerationStageTimingTest, UsesFixedNtscBurstPhaseAndAlternatingPalBurstPhase) {
  GenerationStage generation;
  std::vector<std::string> errors;

  std::vector<double> y_ntsc;
  std::vector<double> c_ntsc;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y_ntsc, &c_ntsc, &errors));
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);

  const double ntsc_line20 = BurstWindowMean(c_ntsc, 20, ntsc);
  const double ntsc_line21 = BurstWindowMean(c_ntsc, 21, ntsc);
  EXPECT_GT(ntsc_line20 * ntsc_line21, 0.0);

  std::vector<double> y_pal;
  std::vector<double> c_pal;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal), &y_pal, &c_pal, &errors));
  const TimingConstants pal = GetTimingConstants(Standard::kPal);

  const double pal_line20 = BurstWindowMean(c_pal, 20, pal);
  const double pal_line21 = BurstWindowMean(c_pal, 21, pal);
  EXPECT_LT(pal_line20 * pal_line21, 0.0);
}

TEST(GenerationStagePatternTest, ColourBarsProduceMultipleDiscreteLumaLevels) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal, "ebu_colour_bars"), &y, &c, &errors));

  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const std::set<int> levels = UniqueRoundedLumaLevelsInActiveWindow(y, 100, pal);

  EXPECT_GE(levels.size(), 6U);
}

TEST(GenerationStagePatternTest, GrayscaleRampRisesAcrossActiveRegion) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(
      generation.Generate(MakeProject(Standard::kNtsc, "grayscale_ramp_horizontal"), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const int line_start = (60 - 1) * ntsc.samples_per_line_4fsc;
  const int start = line_start + ActiveWindowStartSamples(ntsc.sample_rate_4fsc_hz);
  const int end = std::min(line_start + ActiveWindowEndSamples(ntsc.sample_rate_4fsc_hz),
                           line_start + ntsc.samples_per_line_4fsc);

  ASSERT_LT(start + 2, end);
  const double left = y[start + 1];
  const double right = y[end - 1];
  EXPECT_LT(left, right);
}

TEST(GenerationStagePatternTest, PlugePatternStaysNearBlackRange) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal, "pluge"), &y, &c, &errors));

  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const int line_start = (140 - 1) * pal.samples_per_line_4fsc;
  const int start = line_start + ActiveWindowStartSamples(pal.sample_rate_4fsc_hz);
  const int end = std::min(line_start + ActiveWindowEndSamples(pal.sample_rate_4fsc_hz),
                           line_start + pal.samples_per_line_4fsc);

  double min_y = y[start];
  double max_y = y[start];
  for (int i = start; i < end; ++i) {
    min_y = std::min(min_y, y[i]);
    max_y = std::max(max_y, y[i]);
  }

  EXPECT_GT(max_y - min_y, 10.0);
  EXPECT_LT(max_y, 120.0);
}

TEST(GenerationStagePatternTest, EachPatternRendersForPalAndNtsc) {
  GenerationStage generation;
  std::vector<std::string> errors;

  const std::vector<std::string> patterns = {
      "ebu_colour_bars", "grayscale_ramp_horizontal", "pluge"};
  const std::vector<Standard> standards = {Standard::kPal, Standard::kNtsc};

  for (Standard standard : standards) {
    const TimingConstants timing = GetTimingConstants(standard);
    for (const std::string& pattern : patterns) {
      std::vector<double> y;
      std::vector<double> c;
      ASSERT_TRUE(generation.Generate(MakeProject(standard, pattern), &y, &c, &errors));
      EXPECT_EQ(y.size(),
                static_cast<std::size_t>(timing.lines_per_frame * timing.samples_per_line_4fsc));
      EXPECT_EQ(c.size(), y.size());
    }
  }
}

TEST(GenerationStagePatternTest, DurationFramesScalesOutputSampleCount) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;

  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal, "ebu_colour_bars", 3),
                                  &y,
                                  &c,
                                  &errors));
  const TimingConstants pal = GetTimingConstants(Standard::kPal);

  EXPECT_EQ(y.size(),
            static_cast<std::size_t>(3 * pal.lines_per_frame * pal.samples_per_line_4fsc));
  EXPECT_EQ(c.size(), y.size());
}

}  // namespace
}  // namespace videosynth
