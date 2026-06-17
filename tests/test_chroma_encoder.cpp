/*
 * File:        test_chroma_encoder.cpp
 * Module:      chroma_encoder_tests
 * Purpose:     Validates standard-specific chroma encoder dispatch and neutral
 * behavior.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "videosynth/chroma_encoder.h"
#include "videosynth/fixed_point.h"
#include "videosynth/timing_constants.h"

namespace videosynth {
namespace {

double RootMeanSquare(const std::vector<SampleFixed>& values) {
  double square_sum = 0.0;
  for (SampleFixed value : values) {
    const double value_mv = SampleFixedToMillivolts(value);
    square_sum += value_mv * value_mv;
  }
  return values.empty()
             ? 0.0
             : std::sqrt(square_sum / static_cast<double>(values.size()));
}

std::vector<YCbCr444Pixel> MakeCbSinusoidLine(std::size_t sample_count,
                                              double amplitude_norm,
                                              double cycles_per_sample) {
  std::vector<YCbCr444Pixel> line(
      sample_count, YCbCr444Pixel{.y = 512, .cb = 512, .cr = 512});
  for (std::size_t index = 0; index < sample_count; ++index) {
    const double angle =
        2.0 * M_PI * cycles_per_sample * static_cast<double>(index);
    const double cb_norm = amplitude_norm * std::sin(angle);
    line[index].cb =
        static_cast<std::int16_t>(std::lround(512.0 + (448.0 * cb_norm)));
  }
  return line;
}

std::vector<YCbCr444Pixel> MakeCrSinusoidLine(std::size_t sample_count,
                                              double amplitude_norm,
                                              double cycles_per_sample) {
  std::vector<YCbCr444Pixel> line(
      sample_count, YCbCr444Pixel{.y = 512, .cb = 512, .cr = 512});
  for (std::size_t index = 0; index < sample_count; ++index) {
    const double angle =
        2.0 * M_PI * cycles_per_sample * static_cast<double>(index);
    const double cr_norm = amplitude_norm * std::sin(angle);
    line[index].cr =
        static_cast<std::int16_t>(std::lround(512.0 + (448.0 * cr_norm)));
  }
  return line;
}

TEST(ChromaEncoderTest, NeutralChromaProducesNoSubcarrierEnergy) {
  const std::vector<YCbCr444Pixel> neutral_line(
      64, YCbCr444Pixel{.y = 512, .cb = 512, .cr = 512});
  const auto pal = CreateChromaEncoder(
      Standard::kPal, GetTimingConstants(Standard::kPal).sample_rate_4fsc_hz);
  const auto ntsc = CreateChromaEncoder(
      Standard::kNtsc, GetTimingConstants(Standard::kNtsc).sample_rate_4fsc_hz);
  std::vector<SampleFixed> pal_output;
  std::vector<SampleFixed> ntsc_output;

  ASSERT_NE(pal, nullptr);
  ASSERT_NE(ntsc, nullptr);

  pal->EncodeLineFromPhaseStart(neutral_line, 0.0, &pal_output);
  ntsc->EncodeLineFromPhaseStart(neutral_line, 0.0, &ntsc_output);

  for (SampleFixed value : pal_output) {
    EXPECT_NEAR(SampleFixedToMillivolts(value), 0.0, 1e-12);
  }
  for (SampleFixed value : ntsc_output) {
    EXPECT_NEAR(SampleFixedToMillivolts(value), 0.0, 1e-12);
  }
}

TEST(ChromaEncoderTest, NtscCrAxisIsScaledLargerThanCbAxisPerSmpte170M) {
  // SMPTE 170M-2004 Annex A eqs (4)/(5)/(10):
  //   Cb (b-y) axis peak scale = 0.925 × 0.492111 × 0.886 × 100 × (1000/140)
  //   Cr (r-y) axis peak scale = 0.925 × 0.877283 × 0.701 × 100 × (1000/140)
  // Cr/Cb ratio ≈ 1.411 at equal cb_norm / cr_norm input magnitudes.
  const TimingConstants ntsc_timing = GetTimingConstants(Standard::kNtsc);
  const auto ntsc =
      CreateChromaEncoder(Standard::kNtsc, ntsc_timing.sample_rate_4fsc_hz);
  ASSERT_NE(ntsc, nullptr);

  // Full positive excursion on each axis in isolation (phase chosen to land
  // the axis squarely on the carrier at sample 0).
  const std::vector<YCbCr444Pixel> max_cb_line(
      64, YCbCr444Pixel{.y = 512, .cb = 960, .cr = 512});
  const std::vector<YCbCr444Pixel> max_cr_line(
      64, YCbCr444Pixel{.y = 512, .cb = 512, .cr = 960});

  std::vector<SampleFixed> cb_output;
  std::vector<SampleFixed> cr_output;
  ntsc->EncodeLineFromPhaseStart(max_cb_line, M_PI / 2.0, &cb_output);
  ntsc->EncodeLineFromPhaseStart(max_cr_line, 0.0, &cr_output);

  const double cb_peak = SampleFixedToMillivolts(cb_output[0]);
  const double cr_peak = SampleFixedToMillivolts(cr_output[0]);

  // Both axes must have non-trivial amplitude.
  EXPECT_GT(std::abs(cb_peak), 100.0);
  EXPECT_GT(std::abs(cr_peak), 100.0);

  // Cr peak must be ~41 % larger than Cb peak (ratio ≈ 1.411, ±5 %).
  const double expected_ratio = (0.877283 * 0.701) / (0.492111 * 0.886);
  EXPECT_NEAR(std::abs(cr_peak) / std::abs(cb_peak), expected_ratio,
              expected_ratio * 0.05);
}

TEST(ChromaEncoderTest,
     PalLowPassAttenuatesHighFrequencyChromaMoreThanLowFrequency) {
  const TimingConstants pal_timing = GetTimingConstants(Standard::kPal);
  const auto pal =
      CreateChromaEncoder(Standard::kPal, pal_timing.sample_rate_4fsc_hz);
  std::vector<SampleFixed> low_output;
  std::vector<SampleFixed> high_output;

  ASSERT_NE(pal, nullptr);

  const auto low_line = MakeCbSinusoidLine(256, 0.35, 0.01);
  const auto high_line = MakeCbSinusoidLine(256, 0.35, 0.18);

  // PAL uses E'U on the sin() axis, so use +90 deg carrier phase to isolate Cb.
  pal->EncodeLineFromPhaseStart(low_line, M_PI / 2.0, &low_output);
  pal->EncodeLineFromPhaseStart(high_line, M_PI / 2.0, &high_output);

  EXPECT_GT(RootMeanSquare(low_output), RootMeanSquare(high_output) * 3.0);
}

TEST(ChromaEncoderTest, NtscCbAndCrAxesUseSymmetricBandwidth) {
  // Both NTSC axes share the same low-pass filter (kNtscCbCrCutoffHz), so a
  // sinusoid of the same normalised frequency and amplitude should be
  // attenuated identically on each axis. The output RMS ratio must therefore
  // equal the per-axis voltage scale ratio (Cr/Cb ≈ 1.411) within ±10 %.
  // SMPTE 170M-2004 Annex A eqs (4)/(5)/(10).
  const TimingConstants ntsc_timing = GetTimingConstants(Standard::kNtsc);
  const auto ntsc =
      CreateChromaEncoder(Standard::kNtsc, ntsc_timing.sample_rate_4fsc_hz);
  std::vector<SampleFixed> cb_output;
  std::vector<SampleFixed> cr_output;

  ASSERT_NE(ntsc, nullptr);

  const auto cb_line = MakeCbSinusoidLine(256, 0.35, 0.06);
  const auto cr_line = MakeCrSinusoidLine(256, 0.35, 0.06);

  ntsc->EncodeLineFromPhaseStart(cb_line, M_PI / 2.0, &cb_output);
  ntsc->EncodeLineFromPhaseStart(cr_line, 0.0, &cr_output);

  const double cb_rms = RootMeanSquare(cb_output);
  const double cr_rms = RootMeanSquare(cr_output);
  EXPECT_GT(cb_rms, 1.0);
  EXPECT_GT(cr_rms, 1.0);

  const double expected_ratio = (0.877283 * 0.701) / (0.492111 * 0.886);
  EXPECT_NEAR(cr_rms / cb_rms, expected_ratio, expected_ratio * 0.1);
}

TEST(ChromaEncoderTest, PhaseStartEncodingIsDeterministic) {
  const TimingConstants ntsc_timing = GetTimingConstants(Standard::kNtsc);
  const auto ntsc =
      CreateChromaEncoder(Standard::kNtsc, ntsc_timing.sample_rate_4fsc_hz);
  ASSERT_NE(ntsc, nullptr);

  std::vector<YCbCr444Pixel> line(
      128, YCbCr444Pixel{.y = 512, .cb = 760, .cr = 300});
  const double phase_start = M_PI / 6.0;

  std::vector<SampleFixed> first_run;
  std::vector<SampleFixed> second_run;
  ntsc->EncodeLineFromPhaseStart(line, phase_start, &first_run);
  ntsc->EncodeLineFromPhaseStart(line, phase_start, &second_run);

  ASSERT_EQ(first_run.size(), second_run.size());
  for (std::size_t i = 0; i < first_run.size(); ++i) {
    EXPECT_EQ(first_run[i], second_run[i]);
  }
}

}  // namespace
}  // namespace videosynth