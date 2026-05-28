/*
 * File:        test_signal_shaping.cpp
 * Module:      signal_shaping_tests
 * Purpose:     Validates S-curve edge shaping and timing conversions.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "videosynth/signal_shaping.h"

namespace videosynth {
namespace {

TEST(SignalShapingTest, ExpandsTenToNinetyRiseTimeUsingSCurveCalibration) {
  // NTSC sync rise time nominal is 140 ns measured 10%-90%.
  // At 4fsc this should produce at least four edge samples with the S-curve
  // calibration, avoiding a nearly linear three-sample edge.
  constexpr double kNtsc4FscHz = 14318180.0;
  const int ramp_samples = RiseTimeToRampSamples(140.0e-9, kNtsc4FscHz);
  EXPECT_GE(ramp_samples, 4);
}

TEST(SignalShapingTest, ConvertsHalfAmplitudeTimingToFullRampDuration) {
  constexpr double kSampleRateHz = 10.0e6;
  constexpr double kHalfAmplitudeSeconds = 250.0e-9;

  const int ramp_samples = HalfAmplitudeTimeToRampSamples(kHalfAmplitudeSeconds, kSampleRateHz);

  // Full edge duration is twice the half-amplitude interval.
  EXPECT_EQ(ramp_samples, 5);
}

TEST(SignalShapingTest, ProducesCurvedLeadingEdgeWithGentleEndpointSlope) {
  constexpr int kRampSamples = 8;
  constexpr int kPulseWidthSamples = 24;
  constexpr double kBaseline = 0.0;
  constexpr double kSyncTip = -300.0;

  std::vector<double> depths;
  depths.reserve(static_cast<std::size_t>(kRampSamples));
  for (int i = 0; i < kRampSamples; ++i) {
    const double level =
        ShapedPulseLevel(i, kPulseWidthSamples, kRampSamples, kBaseline, kSyncTip);
    const double depth = (level - kBaseline) / (kSyncTip - kBaseline);
    depths.push_back(depth);
  }

  ASSERT_EQ(depths.size(), static_cast<std::size_t>(kRampSamples));
  EXPECT_DOUBLE_EQ(depths.front(), 0.0);
  EXPECT_DOUBLE_EQ(depths.back(), 1.0);

  const double slope_start = depths[1] - depths[0];
  const double slope_mid = depths[4] - depths[3];
  EXPECT_LT(slope_start, slope_mid);

  // The S-curve is symmetric around its midpoint.
  EXPECT_NEAR(depths[1], 1.0 - depths[6], 1e-9);
  EXPECT_NEAR(depths[2], 1.0 - depths[5], 1e-9);
}

}  // namespace
}  // namespace videosynth
