/*
 * File:        test_waveform_mapping.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for the waveform scope's pure sample/time/level
 *              mapping helpers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

#include "videosynth/model.h"
#include "videosynth/timing_constants.h"
#include "waveform_mapping.h"

namespace videosynth::gui {
namespace {

TEST(WaveformMappingTest, MillivoltsPerIreMatchesSmpte170m) {
  // SMPTE 170M-2004 Section 12.1: 140 IRE = 1 V, so 1 IRE ≈ 7.143 mV.
  EXPECT_NEAR(kMillivoltsPerIre, 7.143, 0.001);
}

TEST(WaveformMappingTest, MillivoltsIreRoundTrip) {
  EXPECT_NEAR(MillivoltsToIre(714.3), 100.0, 0.01);
  EXPECT_NEAR(MillivoltsToIre(-285.7), -40.0, 0.01);
  EXPECT_NEAR(MillivoltsToIre(53.6), 7.5, 0.01);
  EXPECT_NEAR(IreToMillivolts(MillivoltsToIre(123.4)), 123.4, 1e-9);
}

TEST(WaveformMappingTest, SampleIndexToMicrosecondsPal) {
  // EBU Tech. 3280-E Section 1.1.1 Table 1: PAL 4fsc = 17.734475 MHz, so a
  // 1135-sample nominal line spans ~64 µs.
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  EXPECT_NEAR(SampleIndexToMicroseconds(1135, timing.sample_rate_4fsc_hz), 64.0,
              0.01);
  EXPECT_NEAR(SampleIndexToMicroseconds(0, timing.sample_rate_4fsc_hz), 0.0,
              1e-9);
}

TEST(WaveformMappingTest, SampleIndexToMicrosecondsNtsc) {
  // SMPTE 244M-2003 Section 3.4/4.1.1: 910 samples at 14.31818 MHz is one
  // 63.556 µs line.
  const TimingConstants timing = GetTimingConstants(Standard::kNtsc);
  EXPECT_NEAR(SampleIndexToMicroseconds(910, timing.sample_rate_4fsc_hz),
              63.556, 0.01);
}

TEST(WaveformMappingTest, MicrosecondsToSampleIndexRoundTrip) {
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  for (const int sample : {0, 1, 567, 1134}) {
    const double microseconds =
        SampleIndexToMicroseconds(sample, timing.sample_rate_4fsc_hz);
    EXPECT_EQ(
        MicrosecondsToSampleIndex(microseconds, timing.sample_rate_4fsc_hz),
        sample);
  }
}

TEST(WaveformMappingTest, PalGridlineAnchorsMatchStandardLevels) {
  // ITU-R BT.1700 Annex 1 Part B Table 2: PAL sync −300, blanking = black
  // = 0, white 700 mV — black collapses onto blanking, giving 3 anchors.
  const std::vector<SignalLevelAnchor> anchors =
      SignalLevelAnchors(GetSignalLevels(Standard::kPal));
  ASSERT_EQ(anchors.size(), 3U);
  EXPECT_DOUBLE_EQ(anchors[0].millivolts, -300.0);
  EXPECT_DOUBLE_EQ(anchors[1].millivolts, 0.0);
  EXPECT_DOUBLE_EQ(anchors[2].millivolts, 700.0);
}

TEST(WaveformMappingTest, NtscGridlineAnchorsMatchStandardLevels) {
  // SMPTE 170M-2004 Section 12.3 Table 1: sync −285.7, blanking 0, black
  // setup 53.6, white 714.3 mV.
  const std::vector<SignalLevelAnchor> anchors =
      SignalLevelAnchors(GetSignalLevels(Standard::kNtsc));
  ASSERT_EQ(anchors.size(), 4U);
  EXPECT_DOUBLE_EQ(anchors[0].millivolts, -285.7);
  EXPECT_DOUBLE_EQ(anchors[1].millivolts, 0.0);
  EXPECT_DOUBLE_EQ(anchors[2].millivolts, 53.6);
  EXPECT_DOUBLE_EQ(anchors[3].millivolts, 714.3);
  EXPECT_EQ(std::string(anchors[2].label), "black");
}

TEST(WaveformMappingTest, PlotYMappingIsLinearAndInvertible) {
  const PlotRange range{-350.0, 750.0};
  constexpr double kPlotHeight = 220.0;

  // Top edge is max_mv, bottom edge is min_mv.
  EXPECT_NEAR(MillivoltsToPlotY(750.0, range, kPlotHeight), 0.0, 1e-9);
  EXPECT_NEAR(MillivoltsToPlotY(-350.0, range, kPlotHeight), kPlotHeight, 1e-9);
  EXPECT_NEAR(MillivoltsToPlotY(200.0, range, kPlotHeight), kPlotHeight / 2.0,
              1e-9);

  for (const double millivolts : {-300.0, 0.0, 53.6, 700.0}) {
    const double y = MillivoltsToPlotY(millivolts, range, kPlotHeight);
    EXPECT_NEAR(PlotYToMillivolts(y, range, kPlotHeight), millivolts, 1e-9);
  }
}

TEST(WaveformMappingTest, PlotXMappingRoundTripsAndClamps) {
  constexpr int kLineSamples = 1135;
  constexpr double kPlotWidth = 800.0;

  for (const int sample : {0, 1, 500, 1134}) {
    const double x = SampleToPlotX(sample, kLineSamples, kPlotWidth);
    EXPECT_EQ(PlotXToSample(x, kLineSamples, kPlotWidth), sample);
  }
  EXPECT_EQ(PlotXToSample(-10.0, kLineSamples, kPlotWidth), 0);
  EXPECT_EQ(PlotXToSample(kPlotWidth + 10.0, kLineSamples, kPlotWidth),
            kLineSamples - 1);
}

TEST(WaveformMappingTest, DefaultPlotRangeCoversLevelsWithHeadroom) {
  const SignalLevels levels = GetSignalLevels(Standard::kNtsc);
  const PlotRange range = DefaultPlotRange(levels);
  EXPECT_LT(range.min_mv, levels.sync_tip_mv);
  EXPECT_GT(range.max_mv, levels.white_mv);
}

TEST(WaveformMappingTest, DefaultPlotRangeCoversFullScaleColourBars) {
  // ITU-R BT.1700 Annex 1 Part B: PAL 100% colour bars peak at 933 mV and
  // trough at -233 mV. SMPTE 170M-2004 Section 12.3: NTSC 100% bars peak at
  // 131 IRE and trough at -33 IRE.
  const PlotRange pal = DefaultPlotRange(GetSignalLevels(Standard::kPal));
  EXPECT_GT(pal.max_mv, 933.0);
  EXPECT_LT(pal.min_mv, -233.0);

  const PlotRange ntsc = DefaultPlotRange(GetSignalLevels(Standard::kNtsc));
  EXPECT_GT(ntsc.max_mv, IreToMillivolts(131.0));
  EXPECT_LT(ntsc.min_mv, IreToMillivolts(-33.0));
}

TEST(WaveformMappingTest, DefaultPlotRangeClearsSyncTipOnBothStandards) {
  for (const Standard standard : {Standard::kPal, Standard::kNtsc}) {
    const SignalLevels levels = GetSignalLevels(standard);
    const PlotRange range = DefaultPlotRange(levels);
    EXPECT_LT(range.min_mv, levels.sync_tip_mv - 200.0);
  }
}

TEST(WaveformMappingTest, SubSyncRangeCoversPalPilotBurstTroughs) {
  const SignalLevels levels = GetSignalLevels(Standard::kPal);
  // The PAL pilot burst swings +/-300 mV about sync tip, so troughs reach
  // -600 mV; the standard range stops well above that.
  const double trough_mv = levels.sync_tip_mv - 300.0;
  EXPECT_GT(DefaultPlotRange(levels).min_mv, trough_mv);

  const PlotRange range =
      PlotRangeForMode(PlotRangeMode::kSubSync, levels, 0.0, 0.0);
  EXPECT_LT(range.min_mv, trough_mv);
  EXPECT_DOUBLE_EQ(range.max_mv, DefaultPlotRange(levels).max_mv);
}

TEST(WaveformMappingTest, BlankingDetailRangeIsCentredOnBlanking) {
  const SignalLevels levels = GetSignalLevels(Standard::kNtsc);
  const PlotRange range =
      PlotRangeForMode(PlotRangeMode::kBlankingDetail, levels, 0.0, 0.0);
  EXPECT_DOUBLE_EQ((range.min_mv + range.max_mv) / 2.0, levels.blanking_mv);
  // The 300 mV p-p PAL burst about blanking must clear the zoomed range.
  EXPECT_LT(range.min_mv, levels.blanking_mv - 150.0);
  EXPECT_GT(range.max_mv, levels.blanking_mv + 150.0);
  // Zoomed in means narrower than the standard range.
  const PlotRange standard = DefaultPlotRange(levels);
  EXPECT_LT(range.max_mv - range.min_mv, standard.max_mv - standard.min_mv);
}

TEST(WaveformMappingTest, FitRangeEnclosesSamplesWithPadding) {
  const SignalLevels levels = GetSignalLevels(Standard::kPal);
  const PlotRange range =
      PlotRangeForMode(PlotRangeMode::kFit, levels, -600.0, 700.0);
  EXPECT_LT(range.min_mv, -600.0);
  EXPECT_GT(range.max_mv, 700.0);
}

TEST(WaveformMappingTest, FitRangeGivesFlatTraceAUsableSpan) {
  const SignalLevels levels = GetSignalLevels(Standard::kPal);
  const PlotRange range =
      PlotRangeForMode(PlotRangeMode::kFit, levels, 0.0, 0.0);
  EXPECT_GT(range.max_mv - range.min_mv, 0.0);
  EXPECT_LT(range.min_mv, 0.0);
  EXPECT_GT(range.max_mv, 0.0);
}

TEST(WaveformMappingTest, FitRangeFallsBackToStandardWhenThereAreNoSamples) {
  const SignalLevels levels = GetSignalLevels(Standard::kPal);
  const PlotRange range = PlotRangeForMode(
      PlotRangeMode::kFit, levels, std::numeric_limits<double>::max(),
      std::numeric_limits<double>::lowest());
  const PlotRange standard = DefaultPlotRange(levels);
  EXPECT_DOUBLE_EQ(range.min_mv, standard.min_mv);
  EXPECT_DOUBLE_EQ(range.max_mv, standard.max_mv);
}

}  // namespace
}  // namespace videosynth::gui
