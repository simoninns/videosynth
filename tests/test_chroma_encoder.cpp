/*
 * File:        test_chroma_encoder.cpp
 * Module:      chroma_encoder_tests
 * Purpose:     Validates standard-specific chroma encoder dispatch and neutral behavior.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <algorithm>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "videosynth/chroma_encoder.h"
#include "videosynth/timing_constants.h"

namespace videosynth {
namespace {

std::vector<double> ConstantPhaseLine(std::size_t sample_count, double phase_rad) {
  return std::vector<double>(sample_count, phase_rad);
}

double RootMeanSquare(const std::vector<double>& values) {
  double square_sum = 0.0;
  for (double value : values) {
    square_sum += value * value;
  }
  return values.empty() ? 0.0 : std::sqrt(square_sum / static_cast<double>(values.size()));
}

std::vector<YCbCr444Pixel> MakeCbSinusoidLine(std::size_t sample_count,
                                              double amplitude_norm,
                                              double cycles_per_sample) {
  std::vector<YCbCr444Pixel> line(sample_count, YCbCr444Pixel{.y = 512, .cb = 512, .cr = 512});
  for (std::size_t index = 0; index < sample_count; ++index) {
    const double angle = 2.0 * M_PI * cycles_per_sample * static_cast<double>(index);
    const double cb_norm = amplitude_norm * std::sin(angle);
    line[index].cb = static_cast<std::int16_t>(std::lround(512.0 + (448.0 * cb_norm)));
  }
  return line;
}

std::vector<YCbCr444Pixel> MakeNtscAxisLine(std::size_t sample_count,
                                            double amplitude_norm,
                                            double cycles_per_sample,
                                            bool q_only) {
  std::vector<YCbCr444Pixel> line(sample_count, YCbCr444Pixel{.y = 512, .cb = 512, .cr = 512});
  for (std::size_t index = 0; index < sample_count; ++index) {
    const double angle = 2.0 * M_PI * cycles_per_sample * static_cast<double>(index);
    const double carrier = amplitude_norm * std::sin(angle);
    const double cb_norm = carrier;
    const double cr_norm =
        q_only ? ((0.26802288 / 0.73575162) * carrier)
               : (-(0.41271905 / 0.47780269) * carrier);
    line[index].cb = static_cast<std::int16_t>(std::lround(512.0 + (448.0 * cb_norm)));
    line[index].cr = static_cast<std::int16_t>(std::lround(512.0 + (448.0 * cr_norm)));
  }
  return line;
}

TEST(ChromaEncoderTest, NeutralChromaProducesNoSubcarrierEnergy) {
  const std::vector<YCbCr444Pixel> neutral_line(64, YCbCr444Pixel{.y = 512, .cb = 512, .cr = 512});
  const auto pal = CreateChromaEncoder(Standard::kPal, GetTimingConstants(Standard::kPal).sample_rate_4fsc_hz);
  const auto ntsc = CreateChromaEncoder(Standard::kNtsc, GetTimingConstants(Standard::kNtsc).sample_rate_4fsc_hz);
  std::vector<double> pal_output;
  std::vector<double> ntsc_output;

  ASSERT_NE(pal, nullptr);
  ASSERT_NE(ntsc, nullptr);

  pal->EncodeLine(neutral_line, ConstantPhaseLine(neutral_line.size(), 0.0), &pal_output);
  ntsc->EncodeLine(neutral_line, ConstantPhaseLine(neutral_line.size(), 0.0), &ntsc_output);

  for (double value : pal_output) {
    EXPECT_NEAR(value, 0.0, 1e-12);
  }
  for (double value : ntsc_output) {
    EXPECT_NEAR(value, 0.0, 1e-12);
  }
}

TEST(ChromaEncoderTest, PalAndNtscUseDifferentChromaAxes) {
  const std::vector<YCbCr444Pixel> saturated_line(64, YCbCr444Pixel{.y = 512, .cb = 800, .cr = 300});
  const auto pal = CreateChromaEncoder(Standard::kPal, GetTimingConstants(Standard::kPal).sample_rate_4fsc_hz);
  const auto ntsc = CreateChromaEncoder(Standard::kNtsc, GetTimingConstants(Standard::kNtsc).sample_rate_4fsc_hz);
  std::vector<double> pal_output;
  std::vector<double> ntsc_output;

  ASSERT_NE(pal, nullptr);
  ASSERT_NE(ntsc, nullptr);

  pal->EncodeLine(saturated_line, ConstantPhaseLine(saturated_line.size(), 0.75), &pal_output);
  ntsc->EncodeLine(saturated_line, ConstantPhaseLine(saturated_line.size(), 0.75), &ntsc_output);

  EXPECT_GT(std::abs(pal_output[32] - ntsc_output[32]), 1.0);
}

TEST(ChromaEncoderTest, PalLowPassAttenuatesHighFrequencyChromaMoreThanLowFrequency) {
  const TimingConstants pal_timing = GetTimingConstants(Standard::kPal);
  const auto pal = CreateChromaEncoder(Standard::kPal, pal_timing.sample_rate_4fsc_hz);
  std::vector<double> low_output;
  std::vector<double> high_output;

  ASSERT_NE(pal, nullptr);

  const auto low_line = MakeCbSinusoidLine(256, 0.35, 0.01);
  const auto high_line = MakeCbSinusoidLine(256, 0.35, 0.18);

  pal->EncodeLine(low_line, ConstantPhaseLine(low_line.size(), 0.0), &low_output);
  pal->EncodeLine(high_line, ConstantPhaseLine(high_line.size(), 0.0), &high_output);

  EXPECT_GT(RootMeanSquare(low_output), RootMeanSquare(high_output) * 3.0);
}

TEST(ChromaEncoderTest, NtscQAxisIsFilteredMoreAggressivelyThanIAxis) {
  const TimingConstants ntsc_timing = GetTimingConstants(Standard::kNtsc);
  const auto ntsc = CreateChromaEncoder(Standard::kNtsc, ntsc_timing.sample_rate_4fsc_hz);
  std::vector<double> i_output;
  std::vector<double> q_output;

  ASSERT_NE(ntsc, nullptr);

  const auto i_line = MakeNtscAxisLine(256, 0.35, 0.06, false);
  const auto q_line = MakeNtscAxisLine(256, 0.35, 0.06, true);

  ntsc->EncodeLine(i_line, ConstantPhaseLine(i_line.size(), M_PI / 2.0), &i_output);
  ntsc->EncodeLine(q_line, ConstantPhaseLine(q_line.size(), 0.0), &q_output);

  EXPECT_GT(RootMeanSquare(i_output), RootMeanSquare(q_output) * 2.0);
}

}  // namespace
}  // namespace videosynth