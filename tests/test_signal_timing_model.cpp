/*
 * File:        test_signal_timing_model.cpp
 * Module:      signal_timing_model_tests
 * Purpose:     Validates PAL and NTSC line timing classification logic.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include "videosynth/signal_timing_model.h"

namespace videosynth {
namespace {

TEST(SignalTimingModelTest, AssignsFieldIndexFromSequentialLineNumber) {
  EXPECT_EQ(GetFieldIndex(Standard::kPal, 1), 1);
  EXPECT_EQ(GetFieldIndex(Standard::kPal, 312), 1);
  EXPECT_EQ(GetFieldIndex(Standard::kPal, 313), 2);
  EXPECT_EQ(GetFieldIndex(Standard::kPal, 625), 2);

  EXPECT_EQ(GetFieldIndex(Standard::kNtsc, 1), 1);
  EXPECT_EQ(GetFieldIndex(Standard::kNtsc, 262), 1);
  EXPECT_EQ(GetFieldIndex(Standard::kNtsc, 263), 2);
  EXPECT_EQ(GetFieldIndex(Standard::kNtsc, 525), 2);
}

TEST(SignalTimingModelTest, ClassifiesPalSyncBlocksAndLineKinds) {
  EXPECT_EQ(GetSyncPulseKind(Standard::kPal, 1), SyncPulseKind::kVerticalSync);
  EXPECT_EQ(GetSyncPulseKind(Standard::kPal, 6), SyncPulseKind::kEqualizing);
  EXPECT_EQ(GetSyncPulseKind(Standard::kPal, 16), SyncPulseKind::kHorizontal);
  EXPECT_EQ(GetSyncPulseKind(Standard::kPal, 311), SyncPulseKind::kEqualizing);
  EXPECT_EQ(GetSyncPulseKind(Standard::kPal, 314),
            SyncPulseKind::kVerticalSync);
  EXPECT_EQ(GetSyncPulseKind(Standard::kPal, 623), SyncPulseKind::kEqualizing);

  EXPECT_EQ(GetLineContentKind(Standard::kPal, 20),
            LineContentKind::kVbiBlanking);
  EXPECT_EQ(GetLineContentKind(Standard::kPal, 23),
            LineContentKind::kActivePicture);
  EXPECT_EQ(GetLineContentKind(Standard::kPal, 320),
            LineContentKind::kVbiBlanking);
  EXPECT_EQ(GetLineContentKind(Standard::kPal, 332),
            LineContentKind::kVbiBlanking);
  EXPECT_EQ(GetLineContentKind(Standard::kPal, 335),
            LineContentKind::kActivePicture);
  EXPECT_EQ(GetLineContentKind(Standard::kPal, 336),
            LineContentKind::kActivePicture);
}

TEST(SignalTimingModelTest, ClassifiesNtscSyncBlocksAndLineKinds) {
  EXPECT_EQ(GetSyncPulseKind(Standard::kNtsc, 1), SyncPulseKind::kEqualizing);
  EXPECT_EQ(GetSyncPulseKind(Standard::kNtsc, 4), SyncPulseKind::kVerticalSync);
  EXPECT_EQ(GetSyncPulseKind(Standard::kNtsc, 10), SyncPulseKind::kHorizontal);
  EXPECT_EQ(GetSyncPulseKind(Standard::kNtsc, 263), SyncPulseKind::kHorizontal);
  EXPECT_EQ(GetSyncPulseKind(Standard::kNtsc, 264), SyncPulseKind::kEqualizing);
  EXPECT_EQ(GetSyncPulseKind(Standard::kNtsc, 267),
            SyncPulseKind::kVerticalSync);
  EXPECT_EQ(GetSyncPulseKind(Standard::kNtsc, 523), SyncPulseKind::kHorizontal);

  EXPECT_EQ(GetLineContentKind(Standard::kNtsc, 20),
            LineContentKind::kVbiBlanking);
  EXPECT_EQ(GetLineContentKind(Standard::kNtsc, 22),
            LineContentKind::kActivePicture);
  EXPECT_EQ(GetLineContentKind(Standard::kNtsc, 263),
            LineContentKind::kVbiBlanking);
  EXPECT_EQ(GetLineContentKind(Standard::kNtsc, 283),
            LineContentKind::kVbiBlanking);
  EXPECT_EQ(GetLineContentKind(Standard::kNtsc, 284),
            LineContentKind::kActivePicture);
}

TEST(SignalTimingModelTest, ModelsHalfLinePairingAndBurstPolicy) {
  EXPECT_TRUE(HasTwoHalfLinePulses(SyncPulseKind::kEqualizing));
  EXPECT_TRUE(HasTwoHalfLinePulses(SyncPulseKind::kVerticalSync));
  EXPECT_FALSE(HasTwoHalfLinePulses(SyncPulseKind::kHorizontal));

  EXPECT_TRUE(BurstEnabledForLine(SyncPulseKind::kHorizontal));
  EXPECT_FALSE(BurstEnabledForLine(SyncPulseKind::kEqualizing));
  EXPECT_FALSE(BurstEnabledForLine(SyncPulseKind::kVerticalSync));
}

TEST(SignalTimingModelTest,
     ModelsPalAlternatingAndNtscConstantBurstReferencePhase) {
  constexpr double kPi = 3.14159265358979323846;

  // NTSC line-to-line subcarrier progression is handled by absolute-time
  // synthesis in generation_stage. Timing model burst reference is constant per
  // line.
  const double ntsc_line20 = BurstPhaseRad(Standard::kNtsc, 20);
  const double ntsc_line21 = BurstPhaseRad(Standard::kNtsc, 21);
  const double ntsc_line22 = BurstPhaseRad(Standard::kNtsc, 22);
  EXPECT_DOUBLE_EQ(ntsc_line21, ntsc_line20);
  EXPECT_DOUBLE_EQ(ntsc_line22, ntsc_line20);

  // PAL burst phase alternates +135°/−135° line-to-line (ITU-R BT.1700 Figure
  // 8).
  const double pal_line20 = BurstPhaseRad(Standard::kPal, 20);
  const double pal_line21 = BurstPhaseRad(Standard::kPal, 21);
  EXPECT_DOUBLE_EQ(pal_line20, -pal_line21);
}

TEST(SignalTimingModelTest, PalSecondFieldVbiBoundaryStartsActiveAtLine335) {
  // Guard against off-by-one regressions around PAL field-2 vertical blanking.
  EXPECT_EQ(GetLineContentKind(Standard::kPal, 334),
            LineContentKind::kVbiBlanking);
  EXPECT_EQ(GetLineContentKind(Standard::kPal, 335),
            LineContentKind::kActivePicture);
}

}  // namespace
}  // namespace videosynth
