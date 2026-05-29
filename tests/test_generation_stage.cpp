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
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "videosynth/chroma_encoder.h"
#include "videosynth/fixed_point.h"
#include "videosynth/progressive_frame_source.h"
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
          const std::string& pattern = "",
                    int duration_frames = 1) {
  const std::string selected_pattern =
    pattern.empty()
      ? (standard == Standard::kPal
         ? "pal_ebu_colour_bars_100"
         : "ntsc_smpte_170m_colour_bars_100")
      : pattern;
  Project project;
  project.cvbs_presets.video_standard_preset = standard;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  project.sections.push_back(
      Section{.name = "SignalTiming",
              .type = "software_generated",
        .pattern = selected_pattern,
              .duration_frames = duration_frames});
  return project;
}

Project MakeProjectWithNtscBlackSetup(double ntsc_black_setup_ire,
                                      const std::string& pattern = "ntsc_full_field_black",
                                      int duration_frames = 1) {
  Project project = MakeProject(Standard::kNtsc, pattern, duration_frames);
  project.cvbs_presets.ntsc_black_setup_ire = ntsc_black_setup_ire;
  project.cvbs_presets.ntsc_black_setup_ire_specified = true;
  return project;
}

Project MakeProgressivePngProject(Standard standard, const std::string& source_path) {
  Project project;
  project.cvbs_presets.video_standard_preset = standard;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  project.sections.push_back(
      Section{.name = "ProgressiveImport",
              .type = "progressive",
              .source = source_path,
              .duration_frames = 1});
  return project;
}

Project MakeProgressiveMp4Project(Standard standard, const std::string& source_path) {
  Project project;
  project.cvbs_presets.video_standard_preset = standard;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  project.sections.push_back(
      Section{.name = "ProgressiveVideo",
              .type = "progressive",
              .source = source_path,
              .duration_frames_all = true,
              .duration_frames = 0});
  return project;
}

double LumaMillivoltsFromCodeForTest(int y_code, const SignalLevels& levels) {
  const int clamped = std::max(48, std::min(940, y_code));
  const double y_norm = static_cast<double>(clamped - 64) / 876.0;
  return levels.black_mv + (y_norm * (levels.white_mv - levels.black_mv));
}

int ActiveWindowStartSamples(Standard standard, double sample_rate_hz) {
  if (standard == Standard::kPal) {
    return 177;
  }
  return static_cast<int>(std::lround(sample_rate_hz * 10.5e-6));
}

int ActiveWindowEndSamples(Standard standard, double sample_rate_hz) {
  if (standard == Standard::kPal) {
    return ActiveWindowStartSamples(standard, sample_rate_hz) +
           static_cast<int>(std::lround(sample_rate_hz * 52.0e-6));
  }
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
  const int line_start = (line_1based - 1) * timing.samples_per_line_4fsc;
  for (int x_sample = 0; x_sample < active_window_samples; ++x_sample) {
    const int sample_offset = active_window_start + x_sample;
    const double t = static_cast<double>(line_start + sample_offset) / timing.sample_rate_4fsc_hz;
    double carrier_phase = (2.0 * M_PI * subcarrier_hz * t) + line.burst_phase_rad;
    if (standard == Standard::kNtsc) {
      carrier_phase += kPi;
    }
    phases[static_cast<std::size_t>(x_sample)] = carrier_phase;
  }
  return phases;
}

std::set<int> UniqueRoundedLumaLevelsInActiveWindow(const std::vector<SampleFixed>& y_mv,
                                                    int line_1based,
                                                    Standard standard,
                                                    const TimingConstants& timing) {
  const int line_start = (line_1based - 1) * timing.samples_per_line_4fsc;
  const int start = line_start + ActiveWindowStartSamples(standard, timing.sample_rate_4fsc_hz);
  const int end = std::min(line_start + ActiveWindowEndSamples(standard, timing.sample_rate_4fsc_hz),
                           line_start + timing.samples_per_line_4fsc);

  std::set<int> levels;
  for (int i = start; i < end; ++i) {
    levels.insert(static_cast<int>(std::lround(SampleFixedToMillivolts(y_mv[i]))));
  }
  return levels;
}

int CountSyncSamplesOnLine(const std::vector<SampleFixed>& y_mv,
                           int line_1based,
                           const TimingConstants& timing,
                           double sync_tip_mv) {
  const int line_start = (line_1based - 1) * timing.samples_per_line_4fsc;
  const int line_end = line_start + timing.samples_per_line_4fsc;

  int count = 0;
  const SampleFixed sync_tip_fixed = MillivoltsToSampleFixed(sync_tip_mv);
  for (int i = line_start; i < line_end; ++i) {
    if (y_mv[i] == sync_tip_fixed) {
      ++count;
    }
  }
  return count;
}

int CountSyncSamplesInHalfLine(const std::vector<SampleFixed>& y_mv,
                               int line_1based,
                               int half_index,
                               const TimingConstants& timing,
                               double sync_tip_mv) {
  const int half_size = timing.samples_per_line_4fsc / 2;
  const int line_start = (line_1based - 1) * timing.samples_per_line_4fsc;
  const int start = line_start + (half_index * half_size);
  const int end = std::min(start + half_size, line_start + timing.samples_per_line_4fsc);

  int count = 0;
  const SampleFixed sync_tip_fixed = MillivoltsToSampleFixed(sync_tip_mv);
  for (int i = start; i < end; ++i) {
    if (y_mv[i] == sync_tip_fixed) {
      ++count;
    }
  }
  return count;
}

int QuantizeCompositeCodeForStandard(double composite_mv, Standard standard) {
  if (standard == Standard::kPal) {
    constexpr double kMillivoltsPerCode = 1.1905;
    constexpr int kBlankingCode = 256;
    constexpr int kMinCode = 4;
    constexpr int kMaxCode = 1019;
    const int mapped = static_cast<int>(std::lround(composite_mv / kMillivoltsPerCode)) +
                       kBlankingCode;
    return std::max(kMinCode, std::min(kMaxCode, mapped));
  }

  constexpr double kMillivoltsPerCode = 1.2755;
  constexpr int kBlankingCode = 240;
  constexpr int kMinCode = 16;
  constexpr int kMaxCode = 1019;
  const int mapped = static_cast<int>(std::lround(composite_mv / kMillivoltsPerCode)) +
                     kBlankingCode;
  return std::max(kMinCode, std::min(kMaxCode, mapped));
}

double BurstWindowMean(const std::vector<SampleFixed>& c_mv,
                       int line_1based,
                       const TimingConstants& timing) {
  const int line_start = (line_1based - 1) * timing.samples_per_line_4fsc;
  const int start = line_start + BurstStartSamples(timing.sample_rate_4fsc_hz);
  const int end = line_start + BurstEndSamples(timing.sample_rate_4fsc_hz);

  double sum = 0.0;
  int count = 0;
  for (int i = start; i < end; ++i) {
    sum += SampleFixedToMillivolts(c_mv[i]);
    ++count;
  }
  return count > 0 ? (sum / static_cast<double>(count)) : 0.0;
}

double EstimateBurstPhaseRad(const std::vector<SampleFixed>& c_mv,
                             int line_1based,
                             const TimingConstants& timing) {
  constexpr double kPi = 3.14159265358979323846;
  const int line_start = (line_1based - 1) * timing.samples_per_line_4fsc;
  const int start = line_start + BurstStartSamples(timing.sample_rate_4fsc_hz);
  const int end = line_start + BurstEndSamples(timing.sample_rate_4fsc_hz);
  const double subcarrier_hz = timing.sample_rate_4fsc_hz / 4.0;

  double sum_sin = 0.0;
  double sum_cos = 0.0;
  for (int i = start; i < end; ++i) {
    const double wt = 2.0 * kPi * subcarrier_hz *
                      (static_cast<double>(i) / timing.sample_rate_4fsc_hz);
    const double c_mv_double = SampleFixedToMillivolts(c_mv[static_cast<std::size_t>(i)]);
    sum_sin += c_mv_double * std::sin(wt);
    sum_cos += c_mv_double * std::cos(wt);
  }
  return std::atan2(sum_cos, sum_sin);
}

double WrappedPhaseDeltaAbs(double a_rad, double b_rad) {
  constexpr double kPi = 3.14159265358979323846;
  const double two_pi = 2.0 * kPi;
  double delta = std::fmod((b_rad - a_rad) + kPi, two_pi);
  if (delta < 0.0) {
    delta += two_pi;
  }
  delta -= kPi;
  return std::abs(delta);
}

struct DecodedPalChromaSample {
  double u = 0.0;
  double v_switched = 0.0;
  double burst_phase_rad = 0.0;
};

DecodedPalChromaSample DecodePalChromaWindowBurstLocked(const std::vector<SampleFixed>& c_mv,
                                                         int line_1based,
                                                         int sample_start,
                                                         int sample_end,
                                                         const TimingConstants& timing) {
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kCompositeChromaScaleMillivolts = 350.0;

  const double burst_phase_rad = EstimateBurstPhaseRad(c_mv, line_1based, timing);
  const double burst_nominal = burst_phase_rad >= 0.0 ? (3.0 * kPi / 4.0) : (-3.0 * kPi / 4.0);
  const double phase_correction = burst_phase_rad - burst_nominal;
  const double subcarrier_hz = timing.sample_rate_4fsc_hz / 4.0;

  double sum_u = 0.0;
  double sum_v = 0.0;
  int count = 0;
  for (int sample_index = sample_start; sample_index < sample_end; ++sample_index) {
    const double t = static_cast<double>(sample_index) / timing.sample_rate_4fsc_hz;
    const double wt = (2.0 * kPi * subcarrier_hz * t) + phase_correction;
    const double chroma_norm = SampleFixedToMillivolts(c_mv[static_cast<std::size_t>(sample_index)]) /
                               kCompositeChromaScaleMillivolts;
    sum_u += chroma_norm * std::sin(wt);
    sum_v += chroma_norm * std::cos(wt);
    ++count;
  }

  return DecodedPalChromaSample{
      .u = count > 0 ? (2.0 * sum_u / static_cast<double>(count)) : 0.0,
      .v_switched = count > 0 ? (2.0 * sum_v / static_cast<double>(count)) : 0.0,
      .burst_phase_rad = burst_phase_rad,
  };
}

TEST(GenerationStageTimingTest, ProducesDeterministicSampleCounts) {
  GenerationStage generation;
  std::vector<std::string> errors;

  std::vector<SampleFixed> y_pal;
  std::vector<SampleFixed> c_pal;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal), &y_pal, &c_pal, &errors));
  EXPECT_EQ(y_pal.size(), static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal)));
  EXPECT_EQ(c_pal.size(), y_pal.size());

  std::vector<SampleFixed> y_ntsc;
  std::vector<SampleFixed> c_ntsc;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y_ntsc, &c_ntsc, &errors));
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  EXPECT_EQ(ntsc.samples_per_line_4fsc, 910);
  EXPECT_EQ(y_ntsc.size(), static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kNtsc)));
  EXPECT_EQ(c_ntsc.size(), y_ntsc.size());
}

TEST(GenerationStageTimingTest, FixedPointModeMatchesFloatingReferenceAtCodeLevel) {
  GenerationStage generation;
  std::vector<std::string> errors;
  const Project project = MakeProject(Standard::kPal, "pal_ebu_colour_bars_100", 1);

  std::vector<SampleFixed> y_float;
  std::vector<SampleFixed> c_float;
  unsetenv("VIDEOSYNTH_GENERATION_FIXED");
  ASSERT_TRUE(generation.Generate(project, &y_float, &c_float, &errors));

  std::vector<SampleFixed> y_fixed;
  std::vector<SampleFixed> c_fixed;
  ASSERT_EQ(setenv("VIDEOSYNTH_GENERATION_FIXED", "1", 1), 0);
  ASSERT_TRUE(generation.Generate(project, &y_fixed, &c_fixed, &errors));
  unsetenv("VIDEOSYNTH_GENERATION_FIXED");

  ASSERT_EQ(y_float.size(), y_fixed.size());
  ASSERT_EQ(c_float.size(), c_fixed.size());

  int max_abs_code_delta = 0;
  double code_error_power = 0.0;
  int float_clipped = 0;
  int fixed_clipped = 0;

  for (std::size_t i = 0; i < y_float.size(); ++i) {
    const int code_float =
        QuantizeCompositeCodeForStandard(y_float[i] + c_float[i], Standard::kPal);
    const int code_fixed =
        QuantizeCompositeCodeForStandard(y_fixed[i] + c_fixed[i], Standard::kPal);
    const int delta = code_fixed - code_float;

    max_abs_code_delta = std::max(max_abs_code_delta, std::abs(delta));
    code_error_power += static_cast<double>(delta * delta);

    if (code_float == 4 || code_float == 1019) {
      ++float_clipped;
    }
    if (code_fixed == 4 || code_fixed == 1019) {
      ++fixed_clipped;
    }
  }

  const double rms_code_delta = std::sqrt(code_error_power / static_cast<double>(y_float.size()));
  EXPECT_LE(max_abs_code_delta, 1);
  EXPECT_LT(rms_code_delta, 0.5);
  EXPECT_EQ(float_clipped, fixed_clipped);
}

TEST(GenerationStageTimingTest, ReportsProgressiveSourceReadError) {
  Project project = MakeProject(Standard::kPal);
  project.sections[0].type = "progressive";
  project.sections[0].pattern.clear();
  project.sections[0].source = "fixture.png";
  project.sections[0].duration_frames = 1;

  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;

  EXPECT_FALSE(generation.Generate(project, &y, &c, &errors));
  ASSERT_FALSE(errors.empty());
}

TEST(GenerationStageTimingTest, BuildsDifferentPulseWidthsForEqualizingAndBroadPulses) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
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
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
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
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
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
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
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
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
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
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
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
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);

  const double normal_line_mean = std::abs(BurstWindowMean(c, 20, ntsc));
  const double broad_line_mean = std::abs(BurstWindowMean(c, 5, ntsc));

  EXPECT_GT(normal_line_mean, 0.1);
  EXPECT_LT(broad_line_mean, 1e-9);
}

TEST(GenerationStageTimingTest, UsesContinuousSubcarrierBurstPhaseProgressionForNtscAndPal) {
  constexpr double kPi = 3.14159265358979323846;
  GenerationStage generation;
  std::vector<std::string> errors;

  // NTSC: 910 samples/line × π/2 rad/sample = π rad/line, so adjacent lines
  // maintain a strong burst with stable magnitude under continuous subcarrier timing.
  std::vector<SampleFixed> y_ntsc;
  std::vector<SampleFixed> c_ntsc;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y_ntsc, &c_ntsc, &errors));
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);

  const double ntsc_line20 = BurstWindowMean(c_ntsc, 20, ntsc);
  const double ntsc_line21 = BurstWindowMean(c_ntsc, 21, ntsc);
  EXPECT_GT(std::abs(ntsc_line20), 0.1);
  EXPECT_GT(std::abs(ntsc_line21), 0.1);

  std::vector<SampleFixed> y_pal;
  std::vector<SampleFixed> c_pal;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal), &y_pal, &c_pal, &errors));
  const TimingConstants pal = GetTimingConstants(Standard::kPal);

  // PAL 625 has 283.75 subcarrier cycles/line and V-switching, so adjacent
  // burst-bearing lines carry a quarter-cycle phase delta in this model.
  const double pal_line20_phase = EstimateBurstPhaseRad(c_pal, 20, pal);
  const double pal_line21_phase = EstimateBurstPhaseRad(c_pal, 21, pal);
  const double pal_line22_phase = EstimateBurstPhaseRad(c_pal, 22, pal);
  EXPECT_NEAR(WrappedPhaseDeltaAbs(pal_line20_phase, pal_line21_phase), kPi / 2.0, 0.25);
  EXPECT_NEAR(WrappedPhaseDeltaAbs(pal_line21_phase, pal_line22_phase), kPi / 2.0, 0.25);
}

TEST(GenerationStageTimingTest, PalBurstPhaseFollowsFourFrameSequence) {
  constexpr double kPi = 3.14159265358979323846;
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y_pal;
  std::vector<SampleFixed> c_pal;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal, "pal_ebu_colour_bars_100", 4),
                                  &y_pal,
                                  &c_pal,
                                  &errors));

  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const int frame_samples = SamplesPerFrame4fsc(Standard::kPal);
  const int line_1based = 23;
  const int burst_offset_start = BurstStartSamples(pal.sample_rate_4fsc_hz);
  const int burst_offset_end = BurstEndSamples(pal.sample_rate_4fsc_hz);
  const double subcarrier_hz = pal.sample_rate_4fsc_hz / 4.0;

  auto phase_for_frame = [&](int frame_index) {
    const int frame_base = frame_index * frame_samples;
    const int line_start = frame_base + ((line_1based - 1) * pal.samples_per_line_4fsc);
    const int start = line_start + burst_offset_start;
    const int end = line_start + burst_offset_end;
    double sum_sin = 0.0;
    double sum_cos = 0.0;
    for (int i = start; i < end; ++i) {
      const double wt = 2.0 * kPi * subcarrier_hz *
                        (static_cast<double>(i) / pal.sample_rate_4fsc_hz);
      sum_sin += c_pal[static_cast<std::size_t>(i)] * std::sin(wt);
      sum_cos += c_pal[static_cast<std::size_t>(i)] * std::cos(wt);
    }
    return std::atan2(sum_cos, sum_sin);
  };

  const double p1 = phase_for_frame(0);
  const double p2 = phase_for_frame(1);
  const double p3 = phase_for_frame(2);
  const double p4 = phase_for_frame(3);

  // PAL colour framing is a 4-frame sequence. The same frame line two frames
  // later is half a cycle away, and the per-frame step is quarter-cycle.
  EXPECT_NEAR(WrappedPhaseDeltaAbs(p1, p2), kPi / 2.0, 0.25);
  EXPECT_NEAR(WrappedPhaseDeltaAbs(p2, p3), kPi / 2.0, 0.25);
  EXPECT_NEAR(WrappedPhaseDeltaAbs(p3, p4), kPi / 2.0, 0.25);
}

TEST(GenerationStageTimingTest, ShapesSyncEdgesInsteadOfHardSteps) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const SignalLevels levels = GetSignalLevels(Standard::kNtsc);
  const int line_1based = 20;
  const int line_start = (line_1based - 1) * ntsc.samples_per_line_4fsc;

  ASSERT_LT(line_start + 3, static_cast<int>(y.size()));
  EXPECT_DOUBLE_EQ(y[line_start - 1], levels.blanking_mv);

  // SMPTE 170M-2004 specifies finite sync rise/fall times, so the pulse edge
  // should move through intermediate levels rather than a single hard step.
  const double first_pulse_sample = SampleFixedToMillivolts(y[line_start]);
  const double second_pulse_sample = SampleFixedToMillivolts(y[line_start + 1]);
  const double third_pulse_sample = SampleFixedToMillivolts(y[line_start + 2]);
  EXPECT_DOUBLE_EQ(first_pulse_sample, levels.blanking_mv);
  EXPECT_LT(second_pulse_sample, levels.blanking_mv);
  EXPECT_GT(second_pulse_sample, levels.sync_tip_mv);
  EXPECT_LT(third_pulse_sample, second_pulse_sample);
}

TEST(GenerationStageTimingTest, AppliesBurstEnvelopeRampAtBurstWindowEdges) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const int line_1based = 20;
  const int line_start = (line_1based - 1) * ntsc.samples_per_line_4fsc;
  const int burst_start = line_start + BurstStartSamples(ntsc.sample_rate_4fsc_hz);
  const int burst_end = line_start + BurstEndSamples(ntsc.sample_rate_4fsc_hz);
  auto MaxAbsInRange = [&](int start_sample, int end_sample) {
    double max_abs = 0.0;
    for (int i = start_sample; i < end_sample; ++i) {
      max_abs = std::max(max_abs, std::abs(SampleFixedToMillivolts(c[i])));
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
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
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
      peak = std::max(peak, SampleFixedToMillivolts(c[i]));
    }
    return peak;
  };

  auto PeakNegativeMagnitude = [&](int start_sample, int end_sample) {
    double peak = 0.0;
    for (int i = start_sample; i < end_sample; ++i) {
      peak = std::max(peak, -SampleFixedToMillivolts(c[i]));
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
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(generation.Generate(
      MakeProject(Standard::kPal, "pal_ebu_colour_bars_100"), &y, &c, &errors));

  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const std::set<int> levels = UniqueRoundedLumaLevelsInActiveWindow(y, 100, Standard::kPal, pal);

  EXPECT_GE(levels.size(), 6U);
}

TEST(GenerationStagePatternTest, PalColourBarsFirstTransitionMatchesVisibleAperturePlacement) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(generation.Generate(
      MakeProject(Standard::kPal, "pal_ebu_colour_bars_100"), &y, &c, &errors));

  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const int line_start = (23 - 1) * pal.samples_per_line_4fsc;

  const int active_start = ActiveWindowStartSamples(Standard::kPal, pal.sample_rate_4fsc_hz);
  const int active_window_samples =
      ActiveWindowEndSamples(Standard::kPal, pal.sample_rate_4fsc_hz) - active_start;
  const int expected_transition =
      active_start + static_cast<int>(std::ceil((88.0 * active_window_samples) / 702.0));

  // PAL visible content starts at +177 samples and spans only the 52.0 us
  // visible aperture, not the older 948-sample 4fsc digital-active window.
  int first_transition_sample = -1;
  for (int sample = line_start + 240; sample < line_start + 360; ++sample) {
    if ((y[sample - 1] - y[sample]) > 50.0) {
      first_transition_sample = sample;
      break;
    }
  }

  ASSERT_NE(first_transition_sample, -1);
  EXPECT_EQ(first_transition_sample - line_start, expected_transition);
}

TEST(GenerationStagePatternTest, GrayscaleRampRisesAcrossActiveRegion) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(
      generation.Generate(MakeProject(Standard::kNtsc, "ntsc_linear_grayscale_ramp_horizontal"), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const int line_start = (60 - 1) * ntsc.samples_per_line_4fsc;
  const int start = line_start + ActiveWindowStartSamples(Standard::kNtsc, ntsc.sample_rate_4fsc_hz);
  const int end = std::min(line_start + ActiveWindowEndSamples(Standard::kNtsc, ntsc.sample_rate_4fsc_hz),
                           line_start + ntsc.samples_per_line_4fsc);

  ASSERT_LT(start + 2, end);
  const double left = y[start + 1];
  const double right = y[end - 1];
  EXPECT_LT(left, right);
}

TEST(GenerationStagePatternTest, PlugePatternStaysNearBlackRange) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal, "pal_pluge_5patch_near_black"), &y, &c, &errors));

  const SignalLevels levels = GetSignalLevels(Standard::kPal);

  bool initialized = false;
  double min_y = 0.0;
  double max_y = 0.0;
  for (SampleFixed sample : y) {
    const double sample_mv = SampleFixedToMillivolts(sample);
    if (sample_mv < levels.blanking_mv) {
      continue;
    }
    if (!initialized) {
      min_y = sample_mv;
      max_y = sample_mv;
      initialized = true;
      continue;
    }
    min_y = std::min(min_y, sample_mv);
    max_y = std::max(max_y, sample_mv);
  }

  ASSERT_TRUE(initialized);
  EXPECT_GT(max_y - min_y, 10.0);
  EXPECT_LT(max_y, 120.0);
}

TEST(GenerationStagePatternTest, PlugePatternContainsBelowBlackSamples) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc, "ntsc_pluge_5patch_near_black"), &y, &c, &errors));

  const SignalLevels levels = GetSignalLevels(Standard::kNtsc);

  bool initialized = false;
  double min_y = 0.0;
  for (SampleFixed sample : y) {
    const double sample_mv = SampleFixedToMillivolts(sample);
    if (sample_mv < levels.blanking_mv) {
      continue;
    }
    if (!initialized) {
      min_y = sample_mv;
      initialized = true;
      continue;
    }
    min_y = std::min(min_y, sample_mv);
  }

  ASSERT_TRUE(initialized);
  EXPECT_LT(min_y, levels.black_mv);
}

TEST(GenerationStagePatternTest, ZeroIreNtscBlackSetupMovesBlackPatternToBlanking) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;

  ASSERT_TRUE(generation.Generate(MakeProjectWithNtscBlackSetup(0.0), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const int line_start = (30 - 1) * ntsc.samples_per_line_4fsc;
  const int start = line_start + ActiveWindowStartSamples(Standard::kNtsc, ntsc.sample_rate_4fsc_hz);
  const int end = std::min(line_start + ActiveWindowEndSamples(Standard::kNtsc, ntsc.sample_rate_4fsc_hz),
                           line_start + ntsc.samples_per_line_4fsc);

  for (int i = start; i < end; ++i) {
    EXPECT_NEAR(SampleFixedToMillivolts(y[static_cast<std::size_t>(i)]), 0.0, 1e-6);
  }
}

TEST(GenerationStagePatternTest, CrosshatchProducesMultipleVerticalTransitionsOnActiveLine) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kNtsc, "ntsc_crosshatch_visible_area_grid"), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const int line_start = (100 - 1) * ntsc.samples_per_line_4fsc;
  const int start = line_start + ActiveWindowStartSamples(Standard::kNtsc, ntsc.sample_rate_4fsc_hz);
  const int end = std::min(line_start + ActiveWindowEndSamples(Standard::kNtsc, ntsc.sample_rate_4fsc_hz),
                           line_start + ntsc.samples_per_line_4fsc);

  int transitions = 0;
  for (int i = start + 1; i < end; ++i) {
    if (std::abs(SampleFixedToMillivolts(y[i]) - SampleFixedToMillivolts(y[i - 1])) > 200.0) {
      ++transitions;
    }
  }

  EXPECT_GE(transitions, 8);
}

TEST(GenerationStagePatternTest, PalCrosshatchIncludesBottomActiveLine) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(generation.Generate(MakeProject(Standard::kPal, "pal_crosshatch_visible_area_grid"),
                                &y,
                                &c,
                                &errors));

  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const SignalLevels levels = GetSignalLevels(Standard::kPal);
  const int line_start = (622 - 1) * pal.samples_per_line_4fsc;
  const int active_start = ActiveWindowStartSamples(Standard::kPal, pal.sample_rate_4fsc_hz);
  const int active_end = ActiveWindowEndSamples(Standard::kPal, pal.sample_rate_4fsc_hz);
  const int center_sample = line_start + active_start + ((active_end - active_start) / 2);

  ASSERT_LT(center_sample, static_cast<int>(y.size()));
  EXPECT_NEAR(SampleFixedToMillivolts(y[static_cast<std::size_t>(center_sample)]),
              levels.white_mv,
              5.0);
}

TEST(GenerationStagePatternTest, EachPatternRendersForPalAndNtsc) {
  GenerationStage generation;
  std::vector<std::string> errors;

  for (const auto& pair : std::vector<std::pair<Standard, std::string>>{
           {Standard::kPal, "pal_ebu_colour_bars_100"},
           {Standard::kPal, "pal_ebu_colour_bars_75"},
           {Standard::kPal, "pal_linear_grayscale_ramp_horizontal"},
           {Standard::kPal, "pal_linear_grayscale_ramp_vertical"},
           {Standard::kPal, "pal_luma_checkerboard_8x8"},
           {Standard::kPal, "pal_luma_checkerboard_16x16"},
           {Standard::kPal, "pal_full_field_black"},
           {Standard::kPal, "pal_full_field_white"},
           {Standard::kPal, "pal_pluge_5patch_near_black"},
           {Standard::kPal, "pal_crosshatch_visible_area_grid"},
           {Standard::kNtsc, "ntsc_smpte_170m_colour_bars_100"},
           {Standard::kNtsc, "ntsc_smpte_170m_colour_bars_75"},
           {Standard::kNtsc, "ntsc_linear_grayscale_ramp_horizontal"},
           {Standard::kNtsc, "ntsc_linear_grayscale_ramp_vertical"},
           {Standard::kNtsc, "ntsc_luma_checkerboard_8x8"},
           {Standard::kNtsc, "ntsc_luma_checkerboard_16x16"},
           {Standard::kNtsc, "ntsc_full_field_black"},
           {Standard::kNtsc, "ntsc_full_field_white"},
           {Standard::kNtsc, "ntsc_pluge_5patch_near_black"},
           {Standard::kNtsc, "ntsc_crosshatch_visible_area_grid"}}) {
    std::vector<SampleFixed> y;
    std::vector<SampleFixed> c;
    ASSERT_TRUE(generation.Generate(MakeProject(pair.first, pair.second), &y, &c, &errors));
    EXPECT_EQ(y.size(), static_cast<std::size_t>(SamplesPerFrame4fsc(pair.first)));
    EXPECT_EQ(c.size(), y.size());
  }
}

TEST(GenerationStagePatternTest, DurationFramesScalesOutputSampleCount) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;

  ASSERT_TRUE(generation.Generate(
                                  MakeProject(Standard::kPal,
                                              "pal_ebu_colour_bars_100",
                                              3),
                                  &y,
                                  &c,
                                  &errors));
  EXPECT_EQ(y.size(), static_cast<std::size_t>(3 * SamplesPerFrame4fsc(Standard::kPal)));
  EXPECT_EQ(c.size(), y.size());
}

TEST(GenerationStageChromaTest, ActiveChromaUsesNtscBurstPlus180ReferenceModel) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;

  ASSERT_TRUE(generation.Generate(
      MakeProject(Standard::kNtsc, "ntsc_smpte_170m_colour_bars_100"), &y, &c, &errors));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const int line_1based = 60;
  const int line_start = (line_1based - 1) * ntsc.samples_per_line_4fsc;
  const int burst_start = line_start + BurstStartSamples(ntsc.sample_rate_4fsc_hz);
  const int burst_mid = burst_start + 8;
  const double subcarrier_hz = ntsc.sample_rate_4fsc_hz / 4.0;
  const LineTimingPrimitive line = BuildLineTimingPrimitive(Standard::kNtsc, line_1based);
  const double burst_t = static_cast<double>(burst_mid) / ntsc.sample_rate_4fsc_hz;
  const double expected_burst = 150.0 *
                                std::sin((2.0 * M_PI * subcarrier_hz * burst_t) + line.burst_phase_rad);
  EXPECT_NEAR(SampleFixedToMillivolts(c[burst_mid]), expected_burst, 1e-6);

  const int active_start = ActiveWindowStartSamples(Standard::kNtsc, ntsc.sample_rate_4fsc_hz);
  const int active_end = ActiveWindowEndSamples(Standard::kNtsc, ntsc.sample_rate_4fsc_hz);
  const int active_window_samples = active_end - active_start;
  TestPatternFrameSource frame_source;
  FrameSourceImage source_frame;
  std::string frame_error;
  ASSERT_TRUE(frame_source.GenerateFrame(
      "ntsc_smpte_170m_colour_bars_100", Standard::kNtsc, &source_frame, &frame_error));

  std::vector<YCbCr444Pixel> source_line(static_cast<std::size_t>(active_window_samples));
  for (int x_sample = 0; x_sample < active_window_samples; ++x_sample) {
    int pixel_x = source_frame.active_x + ((x_sample * source_frame.active_width) / active_window_samples);
    pixel_x = std::min(source_frame.active_x + source_frame.active_width - 1,
                       std::max(source_frame.active_x, pixel_x));
    source_line[static_cast<std::size_t>(x_sample)] = source_frame.PixelAt(pixel_x, 60 - 22);
  }

  const auto chroma_encoder = CreateChromaEncoder(Standard::kNtsc, ntsc.sample_rate_4fsc_hz);
  ASSERT_NE(chroma_encoder, nullptr);
  std::vector<SampleFixed> expected_active_line;
  chroma_encoder->EncodeLine(source_line,
                             CarrierPhasesForActiveLine(line_1based,
                                                        ntsc,
                                                        active_window_samples,
                                                        active_start),
                             &expected_active_line);

  const int active_sample = active_window_samples / 3;
  const int generated_sample_index = line_start + active_start + active_sample;
  EXPECT_NEAR(SampleFixedToMillivolts(c[generated_sample_index]),
              SampleFixedToMillivolts(expected_active_line[static_cast<std::size_t>(active_sample)]),
              1e-6);
}

TEST(GenerationStageChromaTest, PalBurstLockedDecodeRecoversStableHueAcrossLines) {
  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;

  ASSERT_TRUE(generation.Generate(
      MakeProject(Standard::kPal, "pal_ebu_colour_bars_100"), &y, &c, &errors));

  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  TestPatternFrameSource frame_source;
  FrameSourceImage source_frame;
  std::string frame_error;
  ASSERT_TRUE(frame_source.GenerateFrame(
      "pal_ebu_colour_bars_100", Standard::kPal, &source_frame, &frame_error));

  // Use a window centered in the second EBU colour bar, away from transitions.
  const int active_start = ActiveWindowStartSamples(Standard::kPal, pal.sample_rate_4fsc_hz);
  const int active_window_samples =
      ActiveWindowEndSamples(Standard::kPal, pal.sample_rate_4fsc_hz) - active_start;
  const int x_sample = 180;
  const int sample_window = 64;
  const int sample_window_start = x_sample - (sample_window / 2);
  const int sample_window_end = sample_window_start + sample_window;

  const int pixel_x = std::min(source_frame.active_x + source_frame.active_width - 1,
                               std::max(source_frame.active_x,
                      source_frame.active_x +
                        ((x_sample * source_frame.active_width) / active_window_samples)));
  const YCbCr444Pixel source_pixel = source_frame.PixelAt(pixel_x, 0);
  const double expected_u = static_cast<double>(source_pixel.cb - 512) / 448.0;
  const double expected_v = static_cast<double>(source_pixel.cr - 512) / 448.0;
  const double expected_hue = std::atan2(expected_v, expected_u);

  std::vector<double> decoded_hues;
  decoded_hues.reserve(4);

  for (int line_1based = 23; line_1based <= 26; ++line_1based) {
    const int line_start = (line_1based - 1) * pal.samples_per_line_4fsc;
    const DecodedPalChromaSample decoded = DecodePalChromaWindowBurstLocked(
        c,
        line_1based,
        line_start + active_start + sample_window_start,
        line_start + active_start + sample_window_end,
        pal);

    const double v_unswitched = decoded.burst_phase_rad < 0.0 ? -decoded.v_switched : decoded.v_switched;
    const double decoded_hue = std::atan2(v_unswitched, decoded.u);
    const double decoded_magnitude = std::sqrt((decoded.u * decoded.u) + (v_unswitched * v_unswitched));

    decoded_hues.push_back(decoded_hue);
    EXPECT_GT(decoded_magnitude, 0.08);
    EXPECT_NEAR(WrappedPhaseDeltaAbs(decoded_hue, expected_hue), 0.0, 0.25);
  }

  EXPECT_NEAR(WrappedPhaseDeltaAbs(decoded_hues[0], decoded_hues[1]), 0.0, 0.15);
  EXPECT_NEAR(WrappedPhaseDeltaAbs(decoded_hues[1], decoded_hues[2]), 0.0, 0.15);
  EXPECT_NEAR(WrappedPhaseDeltaAbs(decoded_hues[2], decoded_hues[3]), 0.0, 0.15);
}

TEST(GenerationStageProgressiveTest, NtscPngUsesField2DominantRowPairing) {
  const std::string source_path =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/720x480/stills/png/Check-Gamma-Checker.png")
          .string();

  ProgressiveFrameSource progressive_source;
  FrameSourceImage source_frame;
  std::string source_error;
  Section section;
  section.type = "progressive";
  section.source = source_path;
  ASSERT_TRUE(
      progressive_source.GenerateFrame(section, 0, Standard::kNtsc, &source_frame, &source_error));

  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const int active_start = ActiveWindowStartSamples(Standard::kNtsc, ntsc.sample_rate_4fsc_hz);
  const int active_window_samples =
      ActiveWindowEndSamples(Standard::kNtsc, ntsc.sample_rate_4fsc_hz) - active_start;

  const int active_lines_per_field = 240;
  int selected_field_line = -1;
  int selected_x_sample = -1;
  for (int field_line = 0; field_line < active_lines_per_field && selected_x_sample < 0; ++field_line) {
    for (int x_sample = 0; x_sample < active_window_samples; ++x_sample) {
      int pixel_x = source_frame.active_x + ((x_sample * source_frame.active_width) / active_window_samples);
      pixel_x = std::min(source_frame.active_x + source_frame.active_width - 1,
                         std::max(source_frame.active_x, pixel_x));
      if (source_frame.PixelAt(pixel_x, source_frame.active_y + (2 * field_line)).y !=
          source_frame.PixelAt(pixel_x, source_frame.active_y + (2 * field_line + 1)).y) {
        selected_field_line = field_line;
        selected_x_sample = x_sample;
        break;
      }
    }
  }
  ASSERT_NE(selected_field_line, -1);
  ASSERT_NE(selected_x_sample, -1);

  int pixel_x = source_frame.active_x +
                ((selected_x_sample * source_frame.active_width) / active_window_samples);
  pixel_x = std::min(source_frame.active_x + source_frame.active_width - 1,
                     std::max(source_frame.active_x, pixel_x));

  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(generation.Generate(MakeProgressivePngProject(Standard::kNtsc, source_path),
                                  &y,
                                  &c,
                                  &errors));

  const SignalLevels levels = GetSignalLevels(Standard::kNtsc);
    const int field1_line_start = ((22 + selected_field_line) - 1) * ntsc.samples_per_line_4fsc;
    const int field2_line_start = ((284 + selected_field_line) - 1) * ntsc.samples_per_line_4fsc;
  const int sample_offset = active_start + selected_x_sample;

  const double expected_field1 = LumaMillivoltsFromCodeForTest(
      source_frame.PixelAt(pixel_x, source_frame.active_y + (2 * selected_field_line + 1)).y,
      levels);
  const double expected_field2 = LumaMillivoltsFromCodeForTest(
      source_frame.PixelAt(pixel_x, source_frame.active_y + (2 * selected_field_line)).y,
      levels);

  EXPECT_NEAR(SampleFixedToMillivolts(y[static_cast<std::size_t>(field1_line_start + sample_offset)]),
              expected_field1,
              1.0);
  EXPECT_NEAR(SampleFixedToMillivolts(y[static_cast<std::size_t>(field2_line_start + sample_offset)]),
              expected_field2,
              1.0);
}

TEST(GenerationStageProgressiveTest, PalPngUsesField2DominantRowPairing) {
  const std::string source_path =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/720x576/stills/png/Check-Gamma-Checker.png")
          .string();

  ProgressiveFrameSource progressive_source;
  FrameSourceImage source_frame;
  std::string source_error;
  Section section;
  section.type = "progressive";
  section.source = source_path;
  ASSERT_TRUE(
      progressive_source.GenerateFrame(section, 0, Standard::kPal, &source_frame, &source_error));

  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const int active_start = ActiveWindowStartSamples(Standard::kPal, pal.sample_rate_4fsc_hz);
  const int active_window_samples =
      ActiveWindowEndSamples(Standard::kPal, pal.sample_rate_4fsc_hz) - active_start;

  const int active_lines_per_field = 288;
  int selected_field_line = -1;
  int selected_x_sample = -1;
  for (int field_line = 0; field_line < active_lines_per_field && selected_x_sample < 0; ++field_line) {
    for (int x_sample = 0; x_sample < active_window_samples; ++x_sample) {
      int pixel_x = source_frame.active_x + ((x_sample * source_frame.active_width) / active_window_samples);
      pixel_x = std::min(source_frame.active_x + source_frame.active_width - 1,
                         std::max(source_frame.active_x, pixel_x));
      if (source_frame.PixelAt(pixel_x, source_frame.active_y + (2 * field_line)).y !=
          source_frame.PixelAt(pixel_x, source_frame.active_y + (2 * field_line + 1)).y) {
        selected_field_line = field_line;
        selected_x_sample = x_sample;
        break;
      }
    }
  }
  ASSERT_NE(selected_field_line, -1);
  ASSERT_NE(selected_x_sample, -1);

  int pixel_x = source_frame.active_x +
                ((selected_x_sample * source_frame.active_width) / active_window_samples);
  pixel_x = std::min(source_frame.active_x + source_frame.active_width - 1,
                     std::max(source_frame.active_x, pixel_x));

  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(generation.Generate(MakeProgressivePngProject(Standard::kPal, source_path),
                                  &y,
                                  &c,
                                  &errors));

  const SignalLevels levels = GetSignalLevels(Standard::kPal);
    const int field1_line_start = ((23 + selected_field_line) - 1) * pal.samples_per_line_4fsc;
    const int field2_line_start = ((335 + selected_field_line) - 1) * pal.samples_per_line_4fsc;
  const int sample_offset = active_start + selected_x_sample;

  const double expected_field1 = LumaMillivoltsFromCodeForTest(
      source_frame.PixelAt(pixel_x, source_frame.active_y + (2 * selected_field_line + 1)).y,
      levels);
  const double expected_field2 = LumaMillivoltsFromCodeForTest(
      source_frame.PixelAt(pixel_x, source_frame.active_y + (2 * selected_field_line)).y,
      levels);

  EXPECT_NEAR(SampleFixedToMillivolts(y[static_cast<std::size_t>(field1_line_start + sample_offset)]),
              expected_field1,
              1.0);
  EXPECT_NEAR(SampleFixedToMillivolts(y[static_cast<std::size_t>(field2_line_start + sample_offset)]),
              expected_field2,
              1.0);
}

TEST(GenerationStageProgressiveTest, ProgressiveMp4AllDurationGeneratesEntireSource) {
  const std::string source_path =
      (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
       "resources/assets/720x576/video/mp4_25_00/nynashamn.mp4")
          .string();

  ProgressiveFrameSource progressive_source;
  Section section;
  section.type = "progressive";
  section.source = source_path;

  int expected_frame_count = 0;
  std::string count_error;
  ASSERT_TRUE(progressive_source.ResolveFrameCount(
      section, Standard::kPal, &expected_frame_count, &count_error));
  ASSERT_GT(expected_frame_count, 0);

  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y;
  std::vector<SampleFixed> c;
  ASSERT_TRUE(generation.Generate(MakeProgressiveMp4Project(Standard::kPal, source_path),
                                  &y,
                                  &c,
                                  &errors));

  const std::size_t frame_span = static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));
  EXPECT_EQ(y.size(), frame_span * static_cast<std::size_t>(expected_frame_count));
  EXPECT_EQ(c.size(), y.size());
}

}  // namespace
}  // namespace videosynth
