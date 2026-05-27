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
#include <vector>

#include <gtest/gtest.h>

#include "videosynth/chroma_encoder.h"
#include "videosynth/frame_source.h"
#include "videosynth/generation_stage.h"
#include "videosynth/signal_timing_model.h"
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

std::vector<double> CarrierPhasesForActiveLine(int line_1based,
                                               const TimingConstants& timing,
                                               int active_window_samples,
                                               int active_window_start) {
  constexpr double kPi = 3.14159265358979323846;
  std::vector<double> phases(static_cast<std::size_t>(active_window_samples), 0.0);
  const double subcarrier_hz = timing.sample_rate_4fsc_hz / 4.0;
  const Standard standard = timing.lines_per_frame == 625 ? Standard::kPal : Standard::kNtsc;
  const LineTimingPrimitive line = BuildLineTimingPrimitive(standard, line_1based);
  for (int x_sample = 0; x_sample < active_window_samples; ++x_sample) {
    const int sample_offset = active_window_start + x_sample;
    const double t = static_cast<double>(sample_offset) / timing.sample_rate_4fsc_hz;
    double carrier_phase = (2.0 * M_PI * subcarrier_hz * t) + line.burst_phase_rad;
    if (standard == Standard::kNtsc) {
      carrier_phase += kPi;
    }
    phases[static_cast<std::size_t>(x_sample)] = carrier_phase;
  }
  return phases;
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
  EXPECT_EQ(y_pal.size(), static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal)));
  EXPECT_EQ(c_pal.size(), y_pal.size());

  std::vector<double> y_ntsc;
  std::vector<double> c_ntsc;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y_ntsc, &c_ntsc, &errors));
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  EXPECT_EQ(ntsc.samples_per_line_4fsc, 910);
  EXPECT_EQ(y_ntsc.size(), static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kNtsc)));
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

  const int eq_first_half = CountSyncSamplesInHalfLine(y, 4, 0, pal, levels.sync_tip_mv);
  const int eq_second_half = CountSyncSamplesInHalfLine(y, 4, 1, pal, levels.sync_tip_mv);
  EXPECT_GT(eq_first_half, 0);
  EXPECT_GT(eq_second_half, 0);

  const int vs_first_half = CountSyncSamplesInHalfLine(y, 1, 0, pal, levels.sync_tip_mv);
  const int vs_second_half = CountSyncSamplesInHalfLine(y, 1, 1, pal, levels.sync_tip_mv);
  EXPECT_GT(vs_first_half, 0);
  EXPECT_GT(vs_second_half, 0);

  const int h_second_half = CountSyncSamplesInHalfLine(y, 23, 1, pal, levels.sync_tip_mv);
  EXPECT_EQ(h_second_half, 0);
}

TEST(GenerationStageTimingTest, KeepsEndOfFrameNtscLinesAsHorizontalSync) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const SignalLevels levels = GetSignalLevels(Standard::kNtsc);

  const int line520 = CountSyncSamplesOnLine(y, 520, ntsc, levels.sync_tip_mv);
  const int line523 = CountSyncSamplesOnLine(y, 523, ntsc, levels.sync_tip_mv);
  const int line525 = CountSyncSamplesOnLine(y, 525, ntsc, levels.sync_tip_mv);

  // Frame-tail lines should remain horizontal-like in this line-granular model;
  // the vertical interval wraps across frame boundaries at sub-line positions.
  EXPECT_NEAR(line523, line520, 2);
  EXPECT_NEAR(line525, line520, 2);
}

TEST(GenerationStageTimingTest, IncludesSecondFieldNtscVerticalTransitionBlock) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const SignalLevels levels = GetSignalLevels(Standard::kNtsc);

  const int line263 = CountSyncSamplesOnLine(y, 263, ntsc, levels.sync_tip_mv);
  const int line264 = CountSyncSamplesOnLine(y, 264, ntsc, levels.sync_tip_mv);
  const int line522 = CountSyncSamplesOnLine(y, 522, ntsc, levels.sync_tip_mv);
  const int line267 = CountSyncSamplesOnLine(y, 267, ntsc, levels.sync_tip_mv);

  EXPECT_GT(line263, 0);
  EXPECT_GT(line264, 0);
  EXPECT_GT(line267, line264);
  EXPECT_GT(line522, 0);
}

TEST(GenerationStageTimingTest, NtscBroadSyncKeepsIntervalWithinEachHalfLine) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const SignalLevels levels = GetSignalLevels(Standard::kNtsc);
  const int half_line_samples = ntsc.samples_per_line_4fsc / 2;

  const int vs_first_half = CountSyncSamplesInHalfLine(y, 5, 0, ntsc, levels.sync_tip_mv);
  const int vs_second_half = CountSyncSamplesInHalfLine(y, 5, 1, ntsc, levels.sync_tip_mv);

  EXPECT_GT(vs_first_half, 0);
  EXPECT_GT(vs_second_half, 0);
  EXPECT_LT(vs_first_half, half_line_samples);
  EXPECT_LT(vs_second_half, half_line_samples);
}

TEST(GenerationStageTimingTest, PalBroadSyncKeepsIntervalWithinEachHalfLine) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal), &y, &c, &errors));

  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const SignalLevels levels = GetSignalLevels(Standard::kPal);
  const int half_line_samples = pal.samples_per_line_4fsc / 2;

  const int vs_first_half = CountSyncSamplesInHalfLine(y, 1, 0, pal, levels.sync_tip_mv);
  const int vs_second_half = CountSyncSamplesInHalfLine(y, 1, 1, pal, levels.sync_tip_mv);

  EXPECT_GT(vs_first_half, 0);
  EXPECT_GT(vs_second_half, 0);
  EXPECT_LT(vs_first_half, half_line_samples);
  EXPECT_LT(vs_second_half, half_line_samples);
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

TEST(GenerationStageTimingTest, UsesContinuousSubcarrierAlternatingBurstPhaseForNtscAndPal) {
  GenerationStage generation;
  std::vector<std::string> errors;

  // NTSC: 910 samples/line × π/2 rad/sample = π rad/line. The burst window mean
  // flips sign on successive lines, confirming the continuous-subcarrier SC-H phase.
  std::vector<double> y_ntsc;
  std::vector<double> c_ntsc;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y_ntsc, &c_ntsc, &errors));
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);

  const double ntsc_line20 = BurstWindowMean(c_ntsc, 20, ntsc);
  const double ntsc_line21 = BurstWindowMean(c_ntsc, 21, ntsc);
  EXPECT_LT(ntsc_line20 * ntsc_line21, 0.0);

  std::vector<double> y_pal;
  std::vector<double> c_pal;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal), &y_pal, &c_pal, &errors));
  const TimingConstants pal = GetTimingConstants(Standard::kPal);

  const double pal_line20 = BurstWindowMean(c_pal, 20, pal);
  const double pal_line21 = BurstWindowMean(c_pal, 21, pal);
  EXPECT_LT(pal_line20 * pal_line21, 0.0);
}

TEST(GenerationStageTimingTest, ShapesSyncEdgesInsteadOfHardSteps) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const SignalLevels levels = GetSignalLevels(Standard::kNtsc);
  const int line_1based = 20;
  const int line_start = (line_1based - 1) * ntsc.samples_per_line_4fsc;

  ASSERT_LT(line_start + 3, static_cast<int>(y.size()));
  EXPECT_DOUBLE_EQ(y[line_start - 1], levels.blanking_mv);

  // SMPTE 170M-2004 specifies finite sync rise/fall times, so the pulse edge
  // should move through intermediate levels rather than a single hard step.
  const double first_pulse_sample = y[line_start];
  const double second_pulse_sample = y[line_start + 1];
  const double third_pulse_sample = y[line_start + 2];
  EXPECT_DOUBLE_EQ(first_pulse_sample, levels.blanking_mv);
  EXPECT_LT(second_pulse_sample, levels.blanking_mv);
  EXPECT_GT(second_pulse_sample, levels.sync_tip_mv);
  EXPECT_LT(third_pulse_sample, second_pulse_sample);
}

TEST(GenerationStageTimingTest, AppliesBurstEnvelopeRampAtBurstWindowEdges) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const int line_1based = 20;
  const int line_start = (line_1based - 1) * ntsc.samples_per_line_4fsc;
  const int burst_start = line_start + BurstStartSamples(ntsc.sample_rate_4fsc_hz);
  const int burst_end = line_start + BurstEndSamples(ntsc.sample_rate_4fsc_hz);
  auto MaxAbsInRange = [&](int start_sample, int end_sample) {
    double max_abs = 0.0;
    for (int i = start_sample; i < end_sample; ++i) {
      max_abs = std::max(max_abs, std::abs(c[i]));
    }
    return max_abs;
  };

  const int burst_width = burst_end - burst_start;
  ASSERT_GT(burst_width, 6);
  const int center_start = burst_start + (burst_width / 3);
  const int center_end = burst_start + ((2 * burst_width) / 3);

  const double edge_start_max = MaxAbsInRange(burst_start, burst_start + 3);
  const double center_max = MaxAbsInRange(center_start, center_end);
  const double edge_end_max = MaxAbsInRange(burst_end - 3, burst_end);

  EXPECT_DOUBLE_EQ(c[burst_start], 0.0);
  EXPECT_DOUBLE_EQ(c[burst_end - 1], 0.0);

  // SMPTE 170M-2004 Table 2 defines a finite burst envelope rise time.
  EXPECT_LT(edge_start_max, center_max);
  EXPECT_LT(edge_end_max, center_max);
  EXPECT_GT(center_max, 100.0);
}

TEST(GenerationStageTimingTest, ShapesBothPositiveAndNegativeBurstLobesAtEdges) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const int line_1based = 20;
  const int line_start = (line_1based - 1) * ntsc.samples_per_line_4fsc;
  const int burst_start = line_start + BurstStartSamples(ntsc.sample_rate_4fsc_hz);
  const int burst_end = line_start + BurstEndSamples(ntsc.sample_rate_4fsc_hz);
  const int burst_width = burst_end - burst_start;
  ASSERT_GT(burst_width, 8);

  const int center_start = burst_start + (burst_width / 3);
  const int center_end = burst_start + ((2 * burst_width) / 3);

  auto PeakPositive = [&](int start_sample, int end_sample) {
    double peak = 0.0;
    for (int i = start_sample; i < end_sample; ++i) {
      peak = std::max(peak, c[i]);
    }
    return peak;
  };

  auto PeakNegativeMagnitude = [&](int start_sample, int end_sample) {
    double peak = 0.0;
    for (int i = start_sample; i < end_sample; ++i) {
      peak = std::max(peak, -c[i]);
    }
    return peak;
  };

  const double edge_pos = PeakPositive(burst_start, burst_start + 4);
  const double edge_neg = PeakNegativeMagnitude(burst_start, burst_start + 4);
  const double center_pos = PeakPositive(center_start, center_end);
  const double center_neg = PeakNegativeMagnitude(center_start, center_end);

  EXPECT_LT(edge_pos, center_pos);
  EXPECT_LT(edge_neg, center_neg);
  EXPECT_GT(center_pos, 70.0);
  EXPECT_GT(center_neg, 70.0);
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

TEST(GenerationStagePatternTest, PalColourBarsFirstTransitionMatchesEbuDigitalActivePlacement) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal, "ebu_colour_bars"), &y, &c, &errors));

  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const int line_start = (23 - 1) * pal.samples_per_line_4fsc;

  // EBU Tech. 3280-E Section 1.2: PAL digital active starts at +177 samples,
  // with 948 active samples. For 8 equal bars rendered from 720 pixels, the
  // first bar transition lands at sample 296 on line 23.
  int first_transition_sample = -1;
  for (int sample = line_start + 240; sample < line_start + 360; ++sample) {
    if ((y[sample - 1] - y[sample]) > 50.0) {
      first_transition_sample = sample;
      break;
    }
  }

  ASSERT_NE(first_transition_sample, -1);
  EXPECT_EQ(first_transition_sample - line_start, 296);
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
    for (const std::string& pattern : patterns) {
      std::vector<double> y;
      std::vector<double> c;
      ASSERT_TRUE(generation.Generate(MakeProject(standard, pattern), &y, &c, &errors));
      EXPECT_EQ(y.size(), static_cast<std::size_t>(SamplesPerFrame4fsc(standard)));
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
  EXPECT_EQ(y.size(), static_cast<std::size_t>(3 * SamplesPerFrame4fsc(Standard::kPal)));
  EXPECT_EQ(c.size(), y.size());
}

TEST(GenerationStageChromaTest, ActiveChromaUsesNtscBurstPlus180ReferenceModel) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<double> y;
  std::vector<double> c;

  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc, "ebu_colour_bars"), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const int line_1based = 60;
  const int line_start = (line_1based - 1) * ntsc.samples_per_line_4fsc;
  const int burst_start = line_start + BurstStartSamples(ntsc.sample_rate_4fsc_hz);
  const int burst_mid = burst_start + 8;
  const double subcarrier_hz = ntsc.sample_rate_4fsc_hz / 4.0;
  const LineTimingPrimitive line = BuildLineTimingPrimitive(Standard::kNtsc, line_1based);
  const double burst_t = static_cast<double>(burst_mid - line_start) / ntsc.sample_rate_4fsc_hz;
  const double expected_burst = 150.0 *
                                std::sin((2.0 * M_PI * subcarrier_hz * burst_t) + line.burst_phase_rad);
  EXPECT_NEAR(c[burst_mid], expected_burst, 1e-9);

  const int active_start = ActiveWindowStartSamples(ntsc.sample_rate_4fsc_hz);
  const int active_end = ActiveWindowEndSamples(ntsc.sample_rate_4fsc_hz);
  const int active_window_samples = active_end - active_start;
  TestPatternFrameSource frame_source;
  FrameSourceImage source_frame;
  std::string frame_error;
  ASSERT_TRUE(frame_source.GenerateFrame("ebu_colour_bars", Standard::kNtsc, &source_frame, &frame_error));

  std::vector<YCbCr444Pixel> source_line(static_cast<std::size_t>(active_window_samples));
  for (int x_sample = 0; x_sample < active_window_samples; ++x_sample) {
    int pixel_x = (x_sample * source_frame.width) / active_window_samples;
    pixel_x = std::min(source_frame.width - 1, std::max(0, pixel_x));
    source_line[static_cast<std::size_t>(x_sample)] = source_frame.PixelAt(pixel_x, 60 - 22);
  }

  const auto chroma_encoder = CreateChromaEncoder(Standard::kNtsc, ntsc.sample_rate_4fsc_hz);
  ASSERT_NE(chroma_encoder, nullptr);
  std::vector<double> expected_active_line;
  chroma_encoder->EncodeLine(source_line,
                             CarrierPhasesForActiveLine(line_1based,
                                                        ntsc,
                                                        active_window_samples,
                                                        active_start),
                             &expected_active_line);

  const int active_sample = active_window_samples / 3;
  const int generated_sample_index = line_start + active_start + active_sample;
  EXPECT_NEAR(c[generated_sample_index], expected_active_line[static_cast<std::size_t>(active_sample)], 1e-9);
}

}  // namespace
}  // namespace videosynth
