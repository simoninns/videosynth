/*
 * File:        test_vits_generator_pal.cpp
 * Module:      vits_generator_tests
 * Purpose:     Verifies PAL VITS waveform conformance against specification.
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

VitsRenderedLine RenderCatalogType(const std::string& vits_type) {
  VitsDefinitionProvider provider;
  VitsGenerator generator;
  const TimingConstants timing = GetTimingConstants(Standard::kPal);

  VitsDefinition definition;
  std::string error;
  EXPECT_TRUE(provider.TryGetDefinition(Standard::kPal, vits_type, &definition, &error)) << error;

  VitsSynthesisPlan plan;
  EXPECT_TRUE(generator.BuildSynthesisPlan(definition, &plan, &error)) << error;

  VitsRenderedLine rendered;
  EXPECT_TRUE(generator.RenderLine(
      plan, timing.sample_rate_4fsc_hz, timing.samples_per_line_4fsc, &rendered, &error))
      << error;

  return rendered;
}

double SampleAtUs(const std::vector<SampleFixed>& samples,
                  double sample_rate_hz,
                  double time_us) {
  const int index = static_cast<int>(std::lround((time_us * 1.0e-6) * sample_rate_hz));
  return SampleFixedToMillivolts(samples[static_cast<std::size_t>(index)]);
}

double WindowMean(const std::vector<SampleFixed>& samples,
                  double sample_rate_hz,
                  double start_us,
                  double end_us) {
  const int start = static_cast<int>(std::lround((start_us * 1.0e-6) * sample_rate_hz));
  const int end = static_cast<int>(std::lround((end_us * 1.0e-6) * sample_rate_hz));
  double sum = 0.0;
  int count = 0;
  for (int i = start; i < end; ++i) {
    sum += SampleFixedToMillivolts(samples[static_cast<std::size_t>(i)]);
    ++count;
  }
  return count > 0 ? (sum / static_cast<double>(count)) : 0.0;
}

double WindowAbsMean(const std::vector<SampleFixed>& samples,
                     double sample_rate_hz,
                     double start_us,
                     double end_us) {
  const int start = static_cast<int>(std::lround((start_us * 1.0e-6) * sample_rate_hz));
  const int end = static_cast<int>(std::lround((end_us * 1.0e-6) * sample_rate_hz));
  double sum = 0.0;
  int count = 0;
  for (int i = start; i < end; ++i) {
    sum += std::abs(SampleFixedToMillivolts(samples[static_cast<std::size_t>(i)]));
    ++count;
  }
  return count > 0 ? (sum / static_cast<double>(count)) : 0.0;
}

// Correlate to measure burst amplitude at a specific frequency and phase
double CorrelateAmplitude(const std::vector<SampleFixed>& samples,
                          double sample_rate_hz,
                          double start_us,
                          double end_us,
                          double frequency_hz,
                          double phase_radians) {
  const int start = static_cast<int>(std::lround((start_us * 1.0e-6) * sample_rate_hz));
  const int end = static_cast<int>(std::lround((end_us * 1.0e-6) * sample_rate_hz));

  double sum = 0.0;
  int count = 0;
  for (int i = start; i < end; ++i) {
    const double t = static_cast<double>(i - start) / sample_rate_hz;
    const double reference = std::sin((2.0 * kPi * frequency_hz * t) + phase_radians);
    sum += SampleFixedToMillivolts(samples[static_cast<std::size_t>(i)]) * reference;
    ++count;
  }

  return count > 0 ? (2.0 * sum / static_cast<double>(count)) : 0.0;
}

}  // namespace

// vits17 (PAL, frame line 17) conformance tests
// Spec: white_reference (700mV, 12-22us), pulse_2t (700mV, 25.8-26.2us),
//       modulated_pulse (Y:350mV 30-34us, C:350mV 30-34us phase 90°),
//       staircase (140/280/420/560/700mV, 40-62us)

TEST(VitsGeneratorPalTest, Vits17WhiteReferenceLevel) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("vits17");

  // White reference should be near 700 mV across 12-22 us window
  const double white_mean = WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 14.0, 20.0);
  EXPECT_NEAR(white_mean, 700.0, 5.0);
}

TEST(VitsGeneratorPalTest, Vits17Pulse2tPeak) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("vits17");

  // Pulse 2T should peak near 700 mV around 26 us
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 26.0), 650.0);
}

TEST(VitsGeneratorPalTest, Vits17ModulatedPulseYAmplitude) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("vits17");

  // Modulated Y pulse should be present in the 30-34 us window
  // The exact level depends on the catalog definition, just verify it's non-zero
  const double y_mean = WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 30.0, 34.0);
  EXPECT_GT(std::abs(y_mean), 100.0);
}

TEST(VitsGeneratorPalTest, Vits17ModulatedPulseCAmplitude) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("vits17");

  // Modulated C pulse should be present in the 30-34 us window
  const double amplitude = WindowAbsMean(
      rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 30.0, 34.0);
  EXPECT_GT(amplitude, 70.0);
}

TEST(VitsGeneratorPalTest, Vits17StaircaseStepLevels) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("vits17");

  // Staircase steps: 140, 280, 420, 560, 700 mV
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 42.0), 140.0, 15.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 46.0), 280.0, 15.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 50.0), 420.0, 15.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 54.0), 560.0, 15.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 58.0), 700.0, 15.0);
}

TEST(VitsGeneratorPalTest, Vits17StaircaseSmoothTransitions) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("vits17");

  // Verify no deep dips at staircase boundaries - use window means to be more robust
  // The staircase starts at 140 mV, so we expect values above 20 mV at boundaries
  const double kMinStaircase = 20.0;
  EXPECT_GT(WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 39.5, 40.5), kMinStaircase);
  EXPECT_GT(WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 43.5, 44.5), kMinStaircase);
  EXPECT_GT(WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 47.5, 48.5), kMinStaircase);
  EXPECT_GT(WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 51.5, 52.5), kMinStaircase);
  EXPECT_GT(WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 55.5, 56.5), kMinStaircase);
}

// itu-multiburst (PAL, frame line 18) conformance tests
// Spec: grey_pedestal (350mV, 12-62us), burst train (0.5/1.0/2.0/4.0/4.8/5.8 MHz),
//       reference bar pair (+210/-210 mV, 12-20us)

TEST(VitsGeneratorPalTest, ItuMultiburstGreyPedestal) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("itu-multiburst");

  // Grey pedestal at 350 mV
  const double pedestal = WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 12.0, 62.0);
  EXPECT_NEAR(pedestal, 350.0, 20.0);
}

TEST(VitsGeneratorPalTest, ItuMultiburstReferenceBarPositiveBoost) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("itu-multiburst");

  // Positive reference boost: +210 mV on 350 mV pedestal = 560 mV
  const double positive = WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 13.0, 15.0);
  EXPECT_NEAR(positive, 560.0, 25.0);
}

TEST(VitsGeneratorPalTest, ItuMultiburstReferenceBarNegativeBoost) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("itu-multiburst");

  // Negative reference boost: -210 mV on 350 mV pedestal = 140 mV
  const double negative = WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 17.0, 19.0);
  EXPECT_NEAR(negative, 140.0, 25.0);
}

TEST(VitsGeneratorPalTest, ItuMultiburstBurstTrainFrequencies) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("itu-multiburst");

  // Burst train with amplitude ~210 mV at various frequencies
  const double kExpectedAmplitude = 210.0;
  const double kTolerance = 40.0;

  // 0.5 MHz burst (24-28 us)
  const double amp_0_5 = WindowAbsMean(rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 25.0, 27.0);
  EXPECT_GT(amp_0_5, kExpectedAmplitude * 0.5);

  // 1.0 MHz burst (30-35 us)
  const double amp_1_0 = WindowAbsMean(rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 31.0, 34.0);
  EXPECT_GT(amp_1_0, kExpectedAmplitude * 0.5);

  // 2.0 MHz burst (36-41 us)
  const double amp_2_0 = WindowAbsMean(rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 37.0, 40.0);
  EXPECT_GT(amp_2_0, kExpectedAmplitude * 0.5);

  // 4.0 MHz burst (42-47 us)
  const double amp_4_0 = WindowAbsMean(rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 43.5, 46.0);
  EXPECT_GT(amp_4_0, kExpectedAmplitude * 0.5);
}

// uk-national (PAL, frame line 19) conformance tests
// Spec: white_reference (700mV, 12-22us), pulse_2t (700mV, 25.8-26.2us),
//       modulated_pulse (Y:350mV 29-31us, C:350mV 29-31us phase 90°),
//       chroma_reference (70mV, 34-60us phase 60.660°), staircase, black_reference

TEST(VitsGeneratorPalTest, UkNationalWhiteReference) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("uk-national");

  const double white = WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 14.0, 20.0);
  EXPECT_NEAR(white, 700.0, 5.0);
}

TEST(VitsGeneratorPalTest, UkNationalPulse2t) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("uk-national");

  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 26.0), 650.0);
}

TEST(VitsGeneratorPalTest, UkNationalModulatedPulse) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("uk-national");

  // Modulated Y should be present in the 29-31 us window
  const double y_mean = WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 29.0, 31.0);
  EXPECT_GT(std::abs(y_mean), 100.0);

  // Modulated C should be present in the 29-31 us window
  const double c_abs_mean = WindowAbsMean(
      rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 29.0, 31.0);
  EXPECT_GT(c_abs_mean, 50.0);
}

TEST(VitsGeneratorPalTest, UkNationalChromaReference) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("uk-national");

  // Chroma reference should be present in the 34-60 us window
  const double c_abs_mean = WindowAbsMean(
      rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 36.0, 58.0);
  EXPECT_GT(c_abs_mean, 40.0);
}

// vits20 (PAL, frame line 20) conformance tests
// Spec: grey_pedestal (350mV, 12-32us),
//       chroma_reference_full (350mV, 14-28us phase 60.660°),
//       chroma_reference_low (150mV, 34-62us phase 60.660°)

TEST(VitsGeneratorPalTest, Vits20GreyPedestal) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("vits20");

  const double pedestal = WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 14.0, 30.0);
  EXPECT_NEAR(pedestal, 350.0, 10.0);
}

TEST(VitsGeneratorPalTest, Vits20ChromaReferenceFull) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("vits20");

  const double pal_subcarrier_hz = 4433618.75;
  const double phase_rad = 60.660 * kPi / 180.0;
  const double amplitude = CorrelateAmplitude(
      rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 16.0, 26.0, pal_subcarrier_hz, phase_rad);
  EXPECT_NEAR(amplitude, 350.0, 40.0);
}

TEST(VitsGeneratorPalTest, Vits20ChromaReferenceLow) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("vits20");

  // Chroma reference low should be present in the 34-62 us window
  const double c_abs_mean = WindowAbsMean(
      rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 36.0, 58.0);
  EXPECT_GT(c_abs_mean, 50.0);
}

// itu-composite (PAL, frame line 330) conformance tests
// Spec: white_reference (700mV, 12-22us), pulse_2t (700mV, 25.8-26.2us),
//       chroma_reference (140mV, 30-60us phase 60.660°),
//       staircase (140/280/420/560/700mV, 40-62us)

TEST(VitsGeneratorPalTest, ItuCompositeWhiteReference) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("itu-composite");

  const double white = WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 14.0, 20.0);
  EXPECT_NEAR(white, 700.0, 5.0);
}

TEST(VitsGeneratorPalTest, ItuCompositePulse2t) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("itu-composite");

  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 26.0), 650.0);
}

TEST(VitsGeneratorPalTest, ItuCompositeChromaReference) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("itu-composite");

  const double pal_subcarrier_hz = 4433618.75;
  const double phase_rad = 60.660 * kPi / 180.0;
  const double amplitude = CorrelateAmplitude(
      rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 32.0, 58.0, pal_subcarrier_hz, phase_rad);
  EXPECT_NEAR(amplitude, 140.0, 20.0);
}

TEST(VitsGeneratorPalTest, ItuCompositeStaircase) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("itu-composite");

  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 42.0), 140.0, 15.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 46.0), 280.0, 15.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 50.0), 420.0, 15.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 54.0), 560.0, 15.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 58.0), 700.0, 15.0);
}

// itu-combination (PAL, frame line 331) conformance tests
// Spec: grey_pedestal (350mV, 12-62us),
//       chroma staircase (70/210/350mV steps, 14-28us phase 60.660°),
//       sustained_reference (210mV, 34-60us phase 60.660°)

TEST(VitsGeneratorPalTest, ItuCombinationGreyPedestal) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("itu-combination");

  const double pedestal = WindowMean(rendered.y_samples_mv, pal.sample_rate_4fsc_hz, 14.0, 60.0);
  EXPECT_NEAR(pedestal, 350.0, 10.0);
}

TEST(VitsGeneratorPalTest, ItuCombinationChromaStaircase) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("itu-combination");

  // Chroma staircase should have increasing amplitude through steps
  const double step1 = WindowAbsMean(
      rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 15.0, 17.0);
  const double step2 = WindowAbsMean(
      rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 19.0, 21.0);
  const double step3 = WindowAbsMean(
      rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 23.0, 27.0);
  
  // Verify steps are present and increasing
  EXPECT_GT(step1, 20.0);
  EXPECT_GT(step2, 20.0);
  EXPECT_GT(step3, 20.0);
  EXPECT_LT(step1, step3);
}

TEST(VitsGeneratorPalTest, ItuCombinationSustainedReference) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("itu-combination");

  // Sustained reference should be present in the 34-60 us window
  const double c_abs_mean = WindowAbsMean(
      rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 36.0, 58.0);
  EXPECT_GT(c_abs_mean, 50.0);
}

TEST(VitsGeneratorPalTest, ItuCombinationChromaStaircaseSmoothTransitions) {
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const VitsRenderedLine rendered = RenderCatalogType("itu-combination");

  // Verify no deep dips at chroma staircase boundaries
  const double kMinChroma = 30.0;
  EXPECT_GT(WindowAbsMean(rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 17.5, 18.5), kMinChroma);
  EXPECT_GT(WindowAbsMean(rendered.c_samples_mv, pal.sample_rate_4fsc_hz, 21.5, 22.5), kMinChroma);
}

}  // namespace videosynth
