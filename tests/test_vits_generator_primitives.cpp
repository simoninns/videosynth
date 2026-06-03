/*
 * File:        test_vits_generator_primitives.cpp
 * Module:      vits_generator_tests
 * Purpose:     Verifies VITS primitive rendering and composition semantics.
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

VitsDefinition MakeDefinition(Standard standard) {
  VitsDefinition definition;
  definition.standard = standard;
  definition.vits_type = "test-pattern";
  definition.levels_unit = standard == Standard::kPal
                               ? VitsLevelsUnit::kMillivolts
                               : VitsLevelsUnit::kIre;
  return definition;
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

VitsRenderedLine RenderCatalogType(Standard standard,
                                   const std::string& vits_type) {
  VitsDefinitionProvider provider;
  VitsGenerator generator;
  const TimingConstants timing = GetTimingConstants(standard);

  VitsDefinition definition;
  std::string error;
  EXPECT_TRUE(
      provider.TryGetDefinition(standard, vits_type, &definition, &error))
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

TEST(VitsGeneratorTest, ResolvesSubcarrierLockedFrequencyDuringPlanning) {
  VitsDefinition definition = MakeDefinition(Standard::kNtsc);
  definition.primitives.push_back(VitsPrimitiveDefinition{
      .id = "locked-burst",
      .primitive_type = VitsPrimitiveType::kBurst,
      .signal_component = VitsSignalComponent::kC,
      .combine_mode = VitsCombineMode::kAdd,
      .start_us = 12.0,
      .end_us = 20.0,
      .rise_time_us = 0.20,
      .level_or_amplitude = 20.0,
      .subcarrier_lock_multiple = 1.0,
      .phase_deg = 180.0,
  });
  definition.render_order = {"locked-burst"};

  VitsGenerator generator;
  VitsSynthesisPlan plan;
  std::string error;
  ASSERT_TRUE(generator.BuildSynthesisPlan(definition, &plan, &error));
  ASSERT_TRUE(error.empty());
  ASSERT_EQ(plan.primitives.size(), 1U);
  EXPECT_NEAR(plan.primitives[0].resolved_frequency_hz, 3579545.0, 1e-6);
  EXPECT_NEAR(plan.primitives[0].phase_radians, kPi, 1e-12);
}

TEST(VitsGeneratorTest, RendersColourBarWithinWindow) {
  const TimingConstants timing = GetTimingConstants(Standard::kPal);

  VitsDefinition definition = MakeDefinition(Standard::kPal);
  definition.primitives.push_back(VitsPrimitiveDefinition{
      .id = "white-reference",
      .primitive_type = VitsPrimitiveType::kColourBar,
      .signal_component = VitsSignalComponent::kY,
      .combine_mode = VitsCombineMode::kReplace,
      .start_us = 12.0,
      .end_us = 22.0,
      .rise_time_us = 0.20,
      .level_or_amplitude = 700.0,
  });
  definition.render_order = {"white-reference"};

  VitsGenerator generator;
  VitsSynthesisPlan plan;
  VitsRenderedLine rendered;
  std::string error;
  ASSERT_TRUE(generator.BuildSynthesisPlan(definition, &plan, &error));
  ASSERT_TRUE(generator.RenderLine(plan, timing.sample_rate_4fsc_hz,
                                   timing.samples_per_line_4fsc, &rendered,
                                   &error));

  EXPECT_NEAR(
      SampleAtUs(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 8.0), 0.0,
      1.0);
  EXPECT_NEAR(
      SampleAtUs(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 17.0),
      700.0, 1.0);
  EXPECT_NEAR(
      SampleAtUs(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 24.0), 0.0,
      1.0);
}

TEST(VitsGeneratorTest, RendersSinSquaredPulseWithCenterPeak) {
  const TimingConstants timing = GetTimingConstants(Standard::kPal);

  VitsDefinition definition = MakeDefinition(Standard::kPal);
  definition.primitives.push_back(VitsPrimitiveDefinition{
      .id = "pulse",
      .primitive_type = VitsPrimitiveType::kSinSquaredPulse,
      .signal_component = VitsSignalComponent::kY,
      .combine_mode = VitsCombineMode::kReplace,
      .start_us = 10.0,
      .end_us = 20.0,
      .rise_time_us = 0.20,
      .level_or_amplitude = 500.0,
  });
  definition.render_order = {"pulse"};

  VitsGenerator generator;
  VitsSynthesisPlan plan;
  VitsRenderedLine rendered;
  std::string error;
  ASSERT_TRUE(generator.BuildSynthesisPlan(definition, &plan, &error));
  ASSERT_TRUE(generator.RenderLine(plan, timing.sample_rate_4fsc_hz,
                                   timing.samples_per_line_4fsc, &rendered,
                                   &error));

  EXPECT_NEAR(
      SampleAtUs(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 10.0), 0.0,
      2.0);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 15.0),
            450.0);
  EXPECT_NEAR(
      SampleAtUs(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 20.0), 0.0,
      2.0);
}

TEST(VitsGeneratorTest, RendersBurstWithResolvedFrequencyAndPhase) {
  const TimingConstants timing = GetTimingConstants(Standard::kPal);

  VitsDefinition definition = MakeDefinition(Standard::kPal);
  definition.primitives.push_back(VitsPrimitiveDefinition{
      .id = "burst",
      .primitive_type = VitsPrimitiveType::kBurst,
      .signal_component = VitsSignalComponent::kC,
      .combine_mode = VitsCombineMode::kAdd,
      .start_us = 12.0,
      .end_us = 24.0,
      .rise_time_us = 0.20,
      .level_or_amplitude = 210.0,
      .frequency_mhz = 1.0,
      .phase_deg = 90.0,
  });
  definition.render_order = {"burst"};

  VitsGenerator generator;
  VitsSynthesisPlan plan;
  VitsRenderedLine rendered;
  std::string error;
  ASSERT_TRUE(generator.BuildSynthesisPlan(definition, &plan, &error));
  ASSERT_TRUE(generator.RenderLine(plan, timing.sample_rate_4fsc_hz,
                                   timing.samples_per_line_4fsc, &rendered,
                                   &error));

  const double amplitude =
      CorrelateAmplitude(rendered.c_samples_mv, timing.sample_rate_4fsc_hz,
                         14.0, 22.0, 1000000.0, kPi / 2.0);
  EXPECT_NEAR(amplitude, 210.0, 20.0);
}

TEST(VitsGeneratorTest, RendersStaircaseWithAscendingStepLevels) {
  const TimingConstants timing = GetTimingConstants(Standard::kPal);

  VitsDefinition definition = MakeDefinition(Standard::kPal);
  definition.primitives.push_back(VitsPrimitiveDefinition{
      .id = "stairs",
      .primitive_type = VitsPrimitiveType::kStaircase,
      .signal_component = VitsSignalComponent::kY,
      .combine_mode = VitsCombineMode::kReplace,
      .start_us = 10.0,
      .end_us = 20.0,
      .rise_time_us = 0.20,
      .level_or_amplitude = 500.0,
      .staircase_steps = 5,
  });
  definition.render_order = {"stairs"};

  VitsGenerator generator;
  VitsSynthesisPlan plan;
  VitsRenderedLine rendered;
  std::string error;
  ASSERT_TRUE(generator.BuildSynthesisPlan(definition, &plan, &error));
  ASSERT_TRUE(generator.RenderLine(plan, timing.sample_rate_4fsc_hz,
                                   timing.samples_per_line_4fsc, &rendered,
                                   &error));

  const double step1 =
      WindowMean(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 10.4, 11.8);
  const double step2 =
      WindowMean(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 12.2, 13.8);
  const double step3 =
      WindowMean(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 14.2, 15.8);
  const double step4 =
      WindowMean(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 16.2, 17.8);
  const double step5 =
      WindowMean(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 18.2, 19.6);

  EXPECT_LT(step1, step2);
  EXPECT_LT(step2, step3);
  EXPECT_LT(step3, step4);
  EXPECT_LT(step4, step5);
  EXPECT_NEAR(step5, 500.0, 15.0);
}

TEST(VitsGeneratorTest, AppliesAddAndReplaceCompositionSemantics) {
  const TimingConstants timing = GetTimingConstants(Standard::kPal);

  VitsDefinition definition = MakeDefinition(Standard::kPal);
  definition.primitives.push_back(VitsPrimitiveDefinition{
      .id = "baseline",
      .primitive_type = VitsPrimitiveType::kColourBar,
      .signal_component = VitsSignalComponent::kY,
      .combine_mode = VitsCombineMode::kReplace,
      .start_us = 10.0,
      .end_us = 20.0,
      .rise_time_us = 0.20,
      .level_or_amplitude = 200.0,
  });
  definition.primitives.push_back(VitsPrimitiveDefinition{
      .id = "boost",
      .primitive_type = VitsPrimitiveType::kColourBar,
      .signal_component = VitsSignalComponent::kY,
      .combine_mode = VitsCombineMode::kAdd,
      .start_us = 12.0,
      .end_us = 18.0,
      .rise_time_us = 0.20,
      .level_or_amplitude = 100.0,
  });
  definition.composites.push_back(VitsCompositeDefinition{
      .id = "serial-group",
      .mode = VitsCompositeMode::kSerial,
      .children = {"baseline", "boost"},
  });
  definition.render_order = {"serial-group"};

  VitsGenerator generator;
  VitsSynthesisPlan plan;
  VitsRenderedLine rendered;
  std::string error;
  ASSERT_TRUE(generator.BuildSynthesisPlan(definition, &plan, &error));
  ASSERT_TRUE(generator.RenderLine(plan, timing.sample_rate_4fsc_hz,
                                   timing.samples_per_line_4fsc, &rendered,
                                   &error));

  EXPECT_NEAR(
      SampleAtUs(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 11.0),
      200.0, 3.0);
  EXPECT_NEAR(
      SampleAtUs(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 15.0),
      300.0, 3.0);
  EXPECT_NEAR(
      SampleAtUs(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 19.0),
      200.0, 3.0);
}

TEST(VitsGeneratorTest, AppliesParallelCompositeDeltasFromSharedBaseline) {
  const TimingConstants timing = GetTimingConstants(Standard::kPal);

  VitsDefinition definition = MakeDefinition(Standard::kPal);
  definition.primitives.push_back(VitsPrimitiveDefinition{
      .id = "baseline",
      .primitive_type = VitsPrimitiveType::kColourBar,
      .signal_component = VitsSignalComponent::kY,
      .combine_mode = VitsCombineMode::kReplace,
      .start_us = 10.0,
      .end_us = 20.0,
      .rise_time_us = 0.20,
      .level_or_amplitude = 200.0,
  });
  definition.primitives.push_back(VitsPrimitiveDefinition{
      .id = "plus50",
      .primitive_type = VitsPrimitiveType::kColourBar,
      .signal_component = VitsSignalComponent::kY,
      .combine_mode = VitsCombineMode::kAdd,
      .start_us = 12.0,
      .end_us = 18.0,
      .rise_time_us = 0.20,
      .level_or_amplitude = 50.0,
  });
  definition.primitives.push_back(VitsPrimitiveDefinition{
      .id = "plus30",
      .primitive_type = VitsPrimitiveType::kColourBar,
      .signal_component = VitsSignalComponent::kY,
      .combine_mode = VitsCombineMode::kAdd,
      .start_us = 12.0,
      .end_us = 18.0,
      .rise_time_us = 0.20,
      .level_or_amplitude = 30.0,
  });
  definition.composites.push_back(VitsCompositeDefinition{
      .id = "parallel-group",
      .mode = VitsCompositeMode::kParallel,
      .children = {"plus50", "plus30"},
  });
  definition.render_order = {"baseline", "parallel-group"};

  VitsGenerator generator;
  VitsSynthesisPlan plan;
  VitsRenderedLine rendered;
  std::string error;
  ASSERT_TRUE(generator.BuildSynthesisPlan(definition, &plan, &error));
  ASSERT_TRUE(generator.RenderLine(plan, timing.sample_rate_4fsc_hz,
                                   timing.samples_per_line_4fsc, &rendered,
                                   &error));

  EXPECT_NEAR(
      SampleAtUs(rendered.y_samples_mv, timing.sample_rate_4fsc_hz, 15.0),
      280.0, 3.0);
}

TEST(VitsGeneratorTest, AppliesSerialCrossfadeTransitionPolicyAtBoundary) {
  const TimingConstants timing = GetTimingConstants(Standard::kNtsc);

  VitsDefinition definition = MakeDefinition(Standard::kNtsc);
  definition.primitives.push_back(VitsPrimitiveDefinition{
      .id = "zone1",
      .primitive_type = VitsPrimitiveType::kBurst,
      .signal_component = VitsSignalComponent::kC,
      .combine_mode = VitsCombineMode::kAdd,
      .continuity_group = "test_chroma",
      .transition_out_policy = VitsTransitionOutPolicy::kCrossfade,
      .transition_out_duration_us = 1.0,
      .start_us = 20.0,
      .end_us = 24.0,
      .rise_time_us = 1.0,
      .level_or_amplitude = 20.0,
      .subcarrier_lock_multiple = 1.0,
      .phase_deg = 90.0,
  });
  definition.primitives.push_back(VitsPrimitiveDefinition{
      .id = "zone2",
      .primitive_type = VitsPrimitiveType::kBurst,
      .signal_component = VitsSignalComponent::kC,
      .combine_mode = VitsCombineMode::kAdd,
      .continuity_group = "test_chroma",
      .start_us = 24.0,
      .end_us = 28.0,
      .rise_time_us = 1.0,
      .level_or_amplitude = 40.0,
      .subcarrier_lock_multiple = 1.0,
      .phase_deg = 90.0,
  });
  definition.composites.push_back(VitsCompositeDefinition{
      .id = "test_stair",
      .mode = VitsCompositeMode::kSerial,
      .children = {"zone1", "zone2"},
      .continuity_group = "test_chroma",
  });
  definition.render_order = {"test_stair"};

  VitsGenerator generator;
  VitsSynthesisPlan plan;
  VitsRenderedLine rendered;
  std::string error;
  ASSERT_TRUE(generator.BuildSynthesisPlan(definition, &plan, &error));
  ASSERT_TRUE(generator.RenderLine(plan, timing.sample_rate_4fsc_hz,
                                   timing.samples_per_line_4fsc, &rendered,
                                   &error));

  const double zone1_abs = WindowAbsMean(
      rendered.c_samples_mv, timing.sample_rate_4fsc_hz, 21.0, 23.0);
  const double zone2_abs = WindowAbsMean(
      rendered.c_samples_mv, timing.sample_rate_4fsc_hz, 25.0, 27.0);
  const double boundary_abs = WindowAbsMean(
      rendered.c_samples_mv, timing.sample_rate_4fsc_hz, 23.9, 24.1);

  EXPECT_GT(boundary_abs, zone1_abs * 0.20);
  EXPECT_GT(boundary_abs, zone2_abs * 0.10);
}

TEST(VitsGeneratorTest, Ntc7CompositeDoesNotDropToZeroAtStaircaseBoundary) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered =
      RenderCatalogType(Standard::kNtsc, "ntc7-composite");

  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 59.8),
            250.0);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 60.8),
            300.0);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 61.2),
            500.0);
}

TEST(VitsGeneratorTest, Ntc7CombinationChromaStaircaseHasNoDeepBoundaryGaps) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered =
      RenderCatalogType(Standard::kNtsc, "ntc7-combination");

  const double zone1_abs = WindowAbsMean(rendered.c_samples_mv,
                                         ntsc.sample_rate_4fsc_hz, 46.6, 49.4);
  const double zone2_abs = WindowAbsMean(rendered.c_samples_mv,
                                         ntsc.sample_rate_4fsc_hz, 50.6, 53.4);
  const double zone3_abs = WindowAbsMean(rendered.c_samples_mv,
                                         ntsc.sample_rate_4fsc_hz, 54.8, 59.0);
  const double boundary_50_abs = WindowAbsMean(
      rendered.c_samples_mv, ntsc.sample_rate_4fsc_hz, 49.9, 50.1);
  const double boundary_54_abs = WindowAbsMean(
      rendered.c_samples_mv, ntsc.sample_rate_4fsc_hz, 53.9, 54.1);

  EXPECT_GT(boundary_50_abs, zone1_abs * 0.35);
  EXPECT_GT(boundary_50_abs, zone2_abs * 0.20);
  EXPECT_GT(boundary_54_abs, zone2_abs * 0.35);
  EXPECT_GT(boundary_54_abs, zone3_abs * 0.15);
}

TEST(VitsGeneratorTest,
     Ntc7CombinationChromaStaircaseZonesIncreaseToSpecifiedLevels) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered =
      RenderCatalogType(Standard::kNtsc, "ntc7-combination");

  // Zone amplitudes are specified as 10/20/40 IRE and should increase
  // accordingly.
  const double zone1_abs = WindowAbsMean(rendered.c_samples_mv,
                                         ntsc.sample_rate_4fsc_hz, 47.2, 48.8);
  const double zone2_abs = WindowAbsMean(rendered.c_samples_mv,
                                         ntsc.sample_rate_4fsc_hz, 51.2, 52.8);
  const double zone3_abs = WindowAbsMean(rendered.c_samples_mv,
                                         ntsc.sample_rate_4fsc_hz, 55.4, 57.0);

  EXPECT_GT(zone2_abs, zone1_abs * 1.4);
  EXPECT_GT(zone3_abs, zone2_abs * 0.9);
  EXPECT_GT(zone3_abs, zone1_abs * 1.6);
}

TEST(VitsGeneratorTest, FccCompositeDoesNotDropToZeroAfterStaircase) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered =
      RenderCatalogType(Standard::kNtsc, "fcc-composite");

  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 28.1),
            200.0);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 28.8),
            250.0);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 29.2),
            400.0);
}

TEST(VitsGeneratorTest, FccMultiburstWhiteReferenceRampsInAndOut) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered =
      RenderCatalogType(Standard::kNtsc, "fcc-multiburst");

  const double pre_blank =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 8.8, 9.1);
  const double entry =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 9.34, 9.48);
  const double white_plateau =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 12.0, 14.0);
  const double exit =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 15.46, 15.60);
  const double grey_plateau =
      WindowMean(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 16.4, 17.8);

  // 0 IRE blanking before the VITS bar section.
  EXPECT_NEAR(pre_blank, 0.0, 20.0);

  // White reference should ramp up from blanking (not hard-step).
  EXPECT_GT(entry, pre_blank + 20.0);
  EXPECT_LT(entry, white_plateau - 80.0);

  // During white reference interval, level should be near 100 IRE.
  EXPECT_GT(white_plateau, 640.0);

  // White reference should ramp down to the 40 IRE pedestal (not hard-step).
  EXPECT_GT(exit, grey_plateau + 40.0);
  EXPECT_LT(exit, white_plateau - 80.0);

  // Post-reference pedestal should sit near 40 IRE.
  EXPECT_NEAR(grey_plateau, 286.0, 35.0);
}

TEST(VitsGeneratorTest, VirsSecondZoneAndPostBlankMatchSpecLevels) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType(Standard::kNtsc, "virs");

  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 35.2),
            250.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 40.0),
              328.6, 25.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 47.0),
              328.6, 25.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 49.5),
              0.0, 15.0);
  EXPECT_NEAR(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 58.0),
              0.0, 15.0);
}

TEST(VitsGeneratorTest, VirsTransitionFromFirstToSecondZoneNoUnintendedDip) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered = RenderCatalogType(Standard::kNtsc, "virs");

  // Verify smooth transition from 68 IRE (first zone) to 46 IRE (second zone).
  // With automatic overlap correction, should not drop below 30 IRE (214 mV).
  const double kMinNoDeepDip = 214.0;  // 30 IRE in mV

  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 35.5),
            kMinNoDeepDip);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 35.7),
            kMinNoDeepDip);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 36.5),
            kMinNoDeepDip);
}

TEST(VitsGeneratorTest, GateEnvelopeOverlapCorrectionIsApplied) {
  VitsDefinitionProvider provider;
  VitsGenerator generator;

  VitsDefinition definition;
  std::string error;
  ASSERT_TRUE(
      provider.TryGetDefinition(Standard::kNtsc, "virs", &definition, &error));

  VitsSynthesisPlan plan;
  ASSERT_TRUE(generator.BuildSynthesisPlan(definition, &plan, &error));

  // Find virs_first_zone and virs_second_zone in the plan
  const VitsPlannedPrimitive* first_zone = nullptr;
  const VitsPlannedPrimitive* second_zone = nullptr;

  for (const auto& prim : plan.primitives) {
    if (prim.definition.id == "virs_first_zone") {
      first_zone = &prim;
    } else if (prim.definition.id == "virs_second_zone") {
      second_zone = &prim;
    }
  }

  ASSERT_NE(first_zone, nullptr);
  ASSERT_NE(second_zone, nullptr);

  // The overlap correction should span the join point instead of landing on it.
  EXPECT_NEAR(first_zone->definition.end_us, 36.0, 0.01);
  EXPECT_NEAR(second_zone->definition.start_us, 35.0, 0.01);

  const VitsPlannedPrimitive* virs_chroma_ref = nullptr;
  for (const auto& prim : plan.primitives) {
    if (prim.definition.id == "virs_chroma_ref") {
      virs_chroma_ref = &prim;
      break;
    }
  }

  ASSERT_NE(virs_chroma_ref, nullptr);
  // Chroma burst is independent and should end at the defined boundary.
  EXPECT_NEAR(virs_chroma_ref->definition.end_us, 34.5, 0.01);
}

TEST(VitsGeneratorTest, Ntc7CompositeStaircaseToTerminusSmoothTransition) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered =
      RenderCatalogType(Standard::kNtsc, "ntc7-composite");

  // Verify smooth transition from staircase (90 IRE) to terminus (90 IRE) at 60
  // us. With automatic overlap, should not drop below 50 IRE (357 mV).
  const double kMin50Ire = 357.0;  // 50 IRE in mV

  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 60.0),
            kMin50Ire);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 60.2),
            kMin50Ire);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 60.5),
            kMin50Ire);
}

TEST(VitsGeneratorTest, FccCompositeStaircaseToTerminusSmoothTransition) {
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const VitsRenderedLine rendered =
      RenderCatalogType(Standard::kNtsc, "fcc-composite");

  // Verify smooth transition from staircase (80 IRE) to terminus (80 IRE) at 28
  // us. With automatic overlap, should not drop below 40 IRE (286 mV).
  const double kMin40Ire = 286.0;  // 40 IRE in mV

  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 28.0),
            kMin40Ire);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 28.2),
            kMin40Ire);
  EXPECT_GT(SampleAtUs(rendered.y_samples_mv, ntsc.sample_rate_4fsc_hz, 28.5),
            kMin40Ire);
}

}  // namespace
}  // namespace videosynth