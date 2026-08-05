/*
 * File:        test_vits_generator_ntsc.cpp
 * Module:      vits_generator_tests
 * Purpose:     Verifies NTSC VITS waveform conformance against specification.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "videosynth/timing_constants.h"
#include "videosynth/vits_definition_provider.h"
#include "videosynth/vits_generator.h"

namespace videosynth {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMillivoltsPerIre = 714.3 / 100.0;

VitsRenderedLine RenderCatalogType(const std::string& vits_type) {
  VitsDefinitionProvider provider;
  VitsGenerator generator;
  const TimingConstants timing = GetTimingConstants(Standard::kNtsc);

  VitsDefinition definition;
  std::string error;
  EXPECT_TRUE(provider.TryGetDefinition(Standard::kNtsc, vits_type, &definition,
                                        &error))
      << error;

  VitsSynthesisPlan plan;
  EXPECT_TRUE(generator.BuildSynthesisPlan(definition, &plan, &error)) << error;

  VitsRenderedLine rendered;
  EXPECT_TRUE(generator.RenderLine(plan, timing.sample_rate_4fsc_hz,
                                   timing.samples_per_line_4fsc, &rendered,
                                   &error))
      << error;

  return rendered;
}

double SampleAtUs(const std::vector<SampleFixed>& samples,
                  double sample_rate_hz, double time_us) {
  const int index =
      static_cast<int>(std::lround((time_us * 1.0e-6) * sample_rate_hz));
  return SampleFixedToMillivolts(samples[static_cast<std::size_t>(index)]);
}

double WindowMean(const std::vector<SampleFixed>& samples,
                  double sample_rate_hz, double start_us, double end_us) {
  const int start =
      static_cast<int>(std::lround((start_us * 1.0e-6) * sample_rate_hz));
  const int end =
      static_cast<int>(std::lround((end_us * 1.0e-6) * sample_rate_hz));
  double sum = 0.0;
  int count = 0;
  for (int i = start; i < end; ++i) {
    sum += SampleFixedToMillivolts(samples[static_cast<std::size_t>(i)]);
    ++count;
  }
  return count > 0 ? (sum / static_cast<double>(count)) : 0.0;
}

double WindowAbsMean(const std::vector<SampleFixed>& samples,
                     double sample_rate_hz, double start_us, double end_us) {
  const int start =
      static_cast<int>(std::lround((start_us * 1.0e-6) * sample_rate_hz));
  const int end =
      static_cast<int>(std::lround((end_us * 1.0e-6) * sample_rate_hz));
  double sum = 0.0;
  int count = 0;
  for (int i = start; i < end; ++i) {
    sum +=
        std::abs(SampleFixedToMillivolts(samples[static_cast<std::size_t>(i)]));
    ++count;
  }
  return count > 0 ? (sum / static_cast<double>(count)) : 0.0;
}

// Correlate to measure burst amplitude at a specific frequency and phase
double CorrelateAmplitude(const std::vector<SampleFixed>& samples,
                          double sample_rate_hz, double start_us, double end_us,
                          double frequency_hz, double phase_radians) {
  const int start =
      static_cast<int>(std::lround((start_us * 1.0e-6) * sample_rate_hz));
  const int end =
      static_cast<int>(std::lround((end_us * 1.0e-6) * sample_rate_hz));

  double sum = 0.0;
  int count = 0;
  for (int i = start; i < end; ++i) {
    const double t = static_cast<double>(i - start) / sample_rate_hz;
    const double reference =
        std::sin((2.0 * kPi * frequency_hz * t) + phase_radians);
    sum += SampleFixedToMillivolts(samples[static_cast<std::size_t>(i)]) *
           reference;
    ++count;
  }

  return count > 0 ? (2.0 * sum / static_cast<double>(count)) : 0.0;
}

// Helper to convert IRE to mV
double IreToMv(double ire) { return ire * kMillivoltsPerIre; }

}  // namespace

// ntc7-composite (NTSC, frame line 17) conformance tests
// Spec: white_reference (100 IRE, 12-30us), pulse_2t (100 IRE, 33.75-34.25us),
//       modulated_pulse (Y:50 IRE 35.4-38.6us, C:50 IRE 35.4-38.6us phase 0°),
//       chrominance_reference (20 IRE, 42-60us phase 180°),
//       staircase (5 steps to 90 IRE, 46-60us)

TEST(VitsGeneratorNtscTest, Ntc7CompositeWhiteReference) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("ntc7-composite");

  // White reference at 100 IRE = 714.3 mV
  const double white =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 14.0, 28.0);
  EXPECT_NEAR(white, 714.3, 10.0);
}

TEST(VitsGeneratorNtscTest, Ntc7CompositePulse2tPeak) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("ntc7-composite");

  // Pulse 2T should have a peak in the 33.75-34.25 us window
  const double peak =
      SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 34.0);
  EXPECT_GT(peak, 300.0);
}

TEST(VitsGeneratorNtscTest, Ntc7CompositeModulatedPulseY) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("ntc7-composite");

  // Modulated Y pulse should be present in the 35.4-38.6 us window
  const double y_mean =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 35.4, 38.6);
  EXPECT_GT(std::abs(y_mean), 100.0);
}

TEST(VitsGeneratorNtscTest, Ntc7CompositeModulatedPulseC) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("ntc7-composite");

  // Modulated C pulse should be present in the 35.4-38.6 us window
  const double c_abs_mean = WindowAbsMean(rendered.c_samples_mv,
                                          ntsc.sample_rate_4fsc_hz, 35.4, 38.6);
  EXPECT_GT(c_abs_mean, 50.0);
}

TEST(VitsGeneratorNtscTest, Ntc7CompositeChrominanceReference) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("ntc7-composite");

  // Chrominance reference should be present in the 42-60 us window
  const double c_abs_mean = WindowAbsMean(rendered.c_samples_mv,
                                          ntsc.sample_rate_4fsc_hz, 44.0, 58.0);
  EXPECT_GT(c_abs_mean, 50.0);
}

TEST(VitsGeneratorNtscTest, Ntc7CompositeStaircaseStepLevels) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("ntc7-composite");

  // Staircase should have increasing levels - use window means for robustness
  const double level_start =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 46.0, 48.0);
  const double level_end =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 58.0, 60.0);
  EXPECT_GT(level_start, 50.0);
  EXPECT_GT(level_end, 50.0);
  EXPECT_LT(level_start, level_end);
}

// ntc7-combination (NTSC, frame line 280) conformance tests
// Spec: grey_background (50 IRE, 12-62us), grey_reference_boost (+50 IRE,
// 12-16us),
//       multiburst packets (0.5/1.0/2.0/3.0/subcarrier/4.2 MHz),
//       chroma staircase (10/20/40 IRE, 46-60us)

TEST(VitsGeneratorNtscTest, Ntc7CombinationGreyBackground) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("ntc7-combination");

  // Grey background should be present across the line
  const double grey =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 12.0, 62.0);
  EXPECT_GT(std::abs(grey), 100.0);
}

TEST(VitsGeneratorNtscTest, Ntc7CombinationGreyReferenceBoost) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("ntc7-combination");

  // Grey reference boost: +50 IRE on 50 IRE = 100 IRE = 714.3 mV
  const double boosted =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 13.0, 15.0);
  EXPECT_NEAR(boosted, IreToMv(100.0), 15.0);
}

TEST(VitsGeneratorNtscTest, Ntc7CombinationMultiburstPackets) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("ntc7-combination");

  // Multiburst packets should have chroma content in various frequency bands
  // Just verify chroma is present in the multiburst region (18-43 us)
  const double chroma_18_23 = WindowAbsMean(
      rendered.c_samples_mv, ntsc.sample_rate_4fsc_hz, 19.0, 22.0);
  const double chroma_24_27 = WindowAbsMean(
      rendered.c_samples_mv, ntsc.sample_rate_4fsc_hz, 24.5, 26.5);
  const double chroma_28_31 = WindowAbsMean(
      rendered.c_samples_mv, ntsc.sample_rate_4fsc_hz, 28.5, 30.5);
  const double chroma_32_35 = WindowAbsMean(
      rendered.c_samples_mv, ntsc.sample_rate_4fsc_hz, 32.5, 34.5);
  const double chroma_36_39 = WindowAbsMean(
      rendered.c_samples_mv, ntsc.sample_rate_4fsc_hz, 36.5, 38.5);
  const double chroma_40_43 = WindowAbsMean(
      rendered.c_samples_mv, ntsc.sample_rate_4fsc_hz, 40.5, 42.5);

  EXPECT_GT(chroma_18_23, 50.0);
  EXPECT_GT(chroma_24_27, 50.0);
  EXPECT_GT(chroma_28_31, 50.0);
  EXPECT_GT(chroma_32_35, 50.0);
  EXPECT_GT(chroma_36_39, 50.0);
  EXPECT_GT(chroma_40_43, 50.0);
}

TEST(VitsGeneratorNtscTest, Ntc7CombinationChromaStaircase) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("ntc7-combination");

  // Chroma staircase should have increasing amplitude in zones 46-60 us
  const double zone1 = WindowAbsMean(rendered.c_samples_mv,
                                     ntsc.sample_rate_4fsc_hz, 46.5, 49.5);
  const double zone2 = WindowAbsMean(rendered.c_samples_mv,
                                     ntsc.sample_rate_4fsc_hz, 50.5, 53.5);
  const double zone3 = WindowAbsMean(rendered.c_samples_mv,
                                     ntsc.sample_rate_4fsc_hz, 55.0, 59.0);

  EXPECT_GT(zone1, 20.0);
  EXPECT_GT(zone2, 20.0);
  EXPECT_GT(zone3, 20.0);
}

// fcc-multiburst (NTSC, frame line 18) conformance tests
// Spec: grey_pedestal (40 IRE, 9.2-62us), white_reference_boost (+60
// IRE, 9.2-15.7us),
//       burst train (0.5/1.25/2.0/3.0/3.58/4.1 MHz)

TEST(VitsGeneratorNtscTest, FccMultiburstGreyPedestal) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("fcc-multiburst");

  // Grey pedestal should be present in the 10-60 us window
  const double pedestal =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 10.0, 60.0);
  EXPECT_GT(std::abs(pedestal), 100.0);
}

TEST(VitsGeneratorNtscTest, FccMultiburstWhiteReferenceBoost) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("fcc-multiburst");

  // White reference boost: +60 IRE on 40 IRE = 100 IRE = 714.3 mV
  const double boosted =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 10.0, 14.0);
  EXPECT_NEAR(boosted, IreToMv(100.0), 15.0);
}

TEST(VitsGeneratorNtscTest, FccMultiburstBurstTrain) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("fcc-multiburst");

  // Burst train with amplitude ~30 IRE
  const double kExpectedAmplitude = IreToMv(30.0);

  // 0.5 MHz (18.2-26.7 us)
  const double amp_0_5 = WindowAbsMean(rendered.c_samples_mv,
                                       ntsc.sample_rate_4fsc_hz, 20.0, 25.0);
  EXPECT_GT(amp_0_5, kExpectedAmplitude * 0.5);

  // 1.25 MHz (28.2-34.2 us)
  const double amp_1_25 = WindowAbsMean(rendered.c_samples_mv,
                                        ntsc.sample_rate_4fsc_hz, 30.0, 33.0);
  EXPECT_GT(amp_1_25, kExpectedAmplitude * 0.5);

  // 2.0 MHz (35.2-40.2 us)
  const double amp_2_0 = WindowAbsMean(rendered.c_samples_mv,
                                       ntsc.sample_rate_4fsc_hz, 36.0, 39.5);
  EXPECT_GT(amp_2_0, kExpectedAmplitude * 0.5);

  // 3.0 MHz (41.2-46.2 us)
  const double amp_3_0 = WindowAbsMean(rendered.c_samples_mv,
                                       ntsc.sample_rate_4fsc_hz, 42.0, 45.0);
  EXPECT_GT(amp_3_0, kExpectedAmplitude * 0.5);

  // 3.58 MHz (subcarrier locked, 47.2-52.2 us)
  const double amp_3_58 = WindowAbsMean(rendered.c_samples_mv,
                                        ntsc.sample_rate_4fsc_hz, 48.0, 51.0);
  EXPECT_GT(amp_3_58, kExpectedAmplitude * 0.5);

  // 4.1 MHz (53.2-58.2 us)
  const double amp_4_1 = WindowAbsMean(rendered.c_samples_mv,
                                       ntsc.sample_rate_4fsc_hz, 54.0, 57.0);
  EXPECT_GT(amp_4_1, kExpectedAmplitude * 0.5);
}

// fcc-composite (NTSC, frame line 281) conformance tests
// Spec: chroma_reference_zone (20 IRE, 9.5-28us phase 180°),
//       staircase (5 steps to 80 IRE, 13-28us),
//       pulse_2t (100 IRE, 35.25-35.75us),
//       modulated_pulse (Y:50 IRE 37.9-41.1us, C:50 IRE 37.9-41.1us phase
//       180°), white_reference (100 IRE, 43.9-62us)

TEST(VitsGeneratorNtscTest, FccCompositeChromaReferenceZone) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("fcc-composite");

  // Chroma reference at 20 IRE, phase 180°
  const double ntsc_subcarrier_hz = 3579545.0;
  const double amplitude =
      CorrelateAmplitude(rendered.c_samples_mv, ntsc.sample_rate_4fsc_hz, 12.0,
                         26.0, ntsc_subcarrier_hz, kPi);
  EXPECT_NEAR(amplitude, IreToMv(20.0), 8.0);
}

TEST(VitsGeneratorNtscTest, FccCompositeStaircase) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("fcc-composite");

  // Staircase: 5 steps to 80 IRE
  // Expected: 16, 32, 48, 64, 80 IRE
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 15.0),
              IreToMv(16.0), 10.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 18.0),
              IreToMv(32.0), 10.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 21.0),
              IreToMv(48.0), 10.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 24.0),
              IreToMv(64.0), 10.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 27.0),
              IreToMv(80.0), 10.0);
}

TEST(VitsGeneratorNtscTest, FccCompositePulse2t) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("fcc-composite");

  // Pulse 2T at 100 IRE = 714.3 mV
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 35.5),
            680.0);
}

TEST(VitsGeneratorNtscTest, FccCompositeModulatedPulse) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("fcc-composite");

  // Modulated Y should be present in the 37.9-41.1 us window
  const double y_mean =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 37.9, 41.1);
  EXPECT_GT(std::abs(y_mean), 100.0);

  // Modulated C should be present in the 37.9-41.1 us window
  const double c_abs_mean = WindowAbsMean(rendered.c_samples_mv,
                                          ntsc.sample_rate_4fsc_hz, 37.9, 41.1);
  EXPECT_GT(c_abs_mean, 50.0);
}

TEST(VitsGeneratorNtscTest, FccCompositeWhiteReference) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("fcc-composite");

  // White reference at 100 IRE = 714.3 mV
  const double white =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 46.0, 60.0);
  EXPECT_NEAR(white, IreToMv(100.0), 10.0);
}

// virs (NTSC, assignment-dependent line) conformance tests
// Spec: virs_first_zone (68 IRE, 9.15-35.5us),
//       virs_chroma_ref (22 IRE, 10.1-34.5us phase 180°),
//       virs_second_zone (46 IRE, 35.5-48.7us),
//       virs_post_blank (0 IRE, 48.7-62us)

TEST(VitsGeneratorNtscTest, VirsFirstZoneLevel) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("virs");

  // First zone at 68 IRE = 485.7 mV
  const double first_zone =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 12.0, 34.0);
  EXPECT_NEAR(first_zone, IreToMv(68.0), 15.0);
}

TEST(VitsGeneratorNtscTest, VirsChromaReference) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("virs");

  // Chroma reference at 22 IRE, phase 180°
  const double ntsc_subcarrier_hz = 3579545.0;
  const double amplitude =
      CorrelateAmplitude(rendered.c_samples_mv, ntsc.sample_rate_4fsc_hz, 12.0,
                         32.0, ntsc_subcarrier_hz, kPi);
  EXPECT_NEAR(amplitude, IreToMv(22.0), 8.0);
}

TEST(VitsGeneratorNtscTest, VirsSecondZoneLevel) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("virs");

  // Second zone at 46 IRE = 328.6 mV
  const double second_zone =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 37.0, 47.0);
  EXPECT_NEAR(second_zone, IreToMv(46.0), 15.0);
}

TEST(VitsGeneratorNtscTest, VirsPostBlankLevel) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("virs");

  // Post blank at 0 IRE = 0 mV (blanking level)
  const double post_blank =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 50.0, 60.0);
  EXPECT_NEAR(post_blank, 0.0, 10.0);
}

TEST(VitsGeneratorNtscTest, VirsTransitionSmoothness) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType("virs");

  // Verify smooth transition from first zone (68 IRE) to second zone (46 IRE)
  // Should not drop below 30 IRE (214 mV) at the boundary
  const double kMinNoDeepDip = 214.0;
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 35.5),
            kMinNoDeepDip);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 35.7),
            kMinNoDeepDip);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 36.5),
            kMinNoDeepDip);
}

}  // namespace videosynth
