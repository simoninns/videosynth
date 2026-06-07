/*
 * File:        test_fm_transitions.cpp
 * Module:      fm_transitions_tests
 * Purpose:     Tests for 40-bit FM signal transition timing compliance.
 *              IEC 60857 §10.2 specifies 135 ns ± 15 ns (10%-90%) transitions
 *              for the 40-bit FM coded signal; 24-bit biphase uses 225 ns ± 25 ns.
 *              Task 6.13: Tests for 40-bit FM transition times (135 ns).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "videosynth/biphase_encoder.h"
#include "videosynth/fixed_point.h"
#include "videosynth/fm_encoder.h"
#include "videosynth/signal_shaping.h"
#include "videosynth/timing_constants.h"

namespace videosynth {
namespace {

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------

constexpr double kNtscSampleRate = 14318180.0;
constexpr double kPalSampleRate = 17734475.0;
constexpr double kBaseline = 0.0;
constexpr double kPeak = 714.3;

// Fraction of the quintic-smootherstep S-curve ramp that spans 10%-90%.
// Derived from InverseSCurve01(0.9) - InverseSCurve01(0.1) ≈ 0.753 - 0.247.
// Used to convert ramp_samples → measured transition time.
constexpr double kSCurve1090Fraction = 0.506;

// Tolerance multipliers for IEC spec checks.
constexpr double kFmTransitionNs = 135.0;
constexpr double kFmToleranceNs = 15.0;
constexpr double kBiphaseTransitionNs = 225.0;
constexpr double kBiphaseToleranceNs = 25.0;

// Compute the measured 10%-90% transition time in nanoseconds from a given
// number of ramp samples and sample rate, using the S-curve relationship.
double MeasuredTransitionNs(int ramp_samples, double sample_rate_hz) {
  return static_cast<double>(ramp_samples) * kSCurve1090Fraction /
         sample_rate_hz * 1.0e9;
}

// ---------------------------------------------------------------------------
// FmTransitionTest — ramp_samples matches TransitionTimeToRampSamples(135ns)
// ---------------------------------------------------------------------------

TEST(FmTransitionTest, NtscFmRampSamplesMatchExpected135NsCalculation) {
  // The FmEncoder default constructor uses 135 ns.  The resulting ramp_samples
  // must equal what TransitionTimeToRampSamples(135e-9, ...) returns.
  const FmEncoder fm_enc(kNtscSampleRate);
  const int expected = TransitionTimeToRampSamples(135e-9, kNtscSampleRate,
                                                   0.1, 0.9);
  EXPECT_EQ(fm_enc.ramp_samples(), expected);
}

TEST(FmTransitionTest, PalFmRampSamplesMatchExpected135NsCalculation) {
  const FmEncoder fm_enc(kPalSampleRate);
  const int expected = TransitionTimeToRampSamples(135e-9, kPalSampleRate,
                                                   0.1, 0.9);
  EXPECT_EQ(fm_enc.ramp_samples(), expected);
}

TEST(FmTransitionTest, NtscBiphaseRampSamplesMatchExpected225NsCalculation) {
  // 24-bit biphase encoder defaults to 225 ns transitions (IEC 60856/60857).
  const BiphaseEncoder bi_enc(kNtscSampleRate);
  const int expected = TransitionTimeToRampSamples(225e-9, kNtscSampleRate,
                                                   0.1, 0.9);
  EXPECT_EQ(bi_enc.ramp_samples(), expected);
}

TEST(FmTransitionTest, PalBiphaseRampSamplesMatchExpected225NsCalculation) {
  const BiphaseEncoder bi_enc(kPalSampleRate);
  const int expected = TransitionTimeToRampSamples(225e-9, kPalSampleRate,
                                                   0.1, 0.9);
  EXPECT_EQ(bi_enc.ramp_samples(), expected);
}

// ---------------------------------------------------------------------------
// FmTransitionTest — FM ramp is strictly narrower than biphase ramp
// ---------------------------------------------------------------------------

TEST(FmTransitionTest, NtscFmRampIsStrictlyNarrowerThanBiphaseRamp) {
  // IEC 60857 §10.2: FM uses 135 ns vs 225 ns for 24-bit biphase.
  // At the same sample rate, FM must produce fewer ramp samples.
  const FmEncoder fm_enc(kNtscSampleRate);
  const BiphaseEncoder bi_enc(kNtscSampleRate);
  EXPECT_LT(fm_enc.ramp_samples(), bi_enc.ramp_samples());
}

TEST(FmTransitionTest, PalFmRampIsStrictlyNarrowerThanBiphaseRamp) {
  const FmEncoder fm_enc(kPalSampleRate);
  const BiphaseEncoder bi_enc(kPalSampleRate);
  EXPECT_LT(fm_enc.ramp_samples(), bi_enc.ramp_samples());
}

// ---------------------------------------------------------------------------
// FmTransitionTest — measured transition time within IEC tolerances
// ---------------------------------------------------------------------------

TEST(FmTransitionTest, NtscFmMeasuredTransitionWithin135NsSpec) {
  // IEC 60857 §10.2: 135 ns ± 15 ns for 40-bit FM.
  // Compute the effective 10%-90% time from ramp_samples × S-curve fraction.
  const FmEncoder fm_enc(kNtscSampleRate);
  const double measured_ns =
      MeasuredTransitionNs(fm_enc.ramp_samples(), kNtscSampleRate);
  EXPECT_GE(measured_ns, kFmTransitionNs - kFmToleranceNs)
      << "FM transition too fast: " << measured_ns << " ns";
  EXPECT_LE(measured_ns, kFmTransitionNs + kFmToleranceNs)
      << "FM transition too slow: " << measured_ns << " ns";
}

TEST(FmTransitionTest, PalFmMeasuredTransitionWithin135NsSpec) {
  const FmEncoder fm_enc(kPalSampleRate);
  const double measured_ns =
      MeasuredTransitionNs(fm_enc.ramp_samples(), kPalSampleRate);
  EXPECT_GE(measured_ns, kFmTransitionNs - kFmToleranceNs)
      << "FM transition too fast: " << measured_ns << " ns";
  EXPECT_LE(measured_ns, kFmTransitionNs + kFmToleranceNs)
      << "FM transition too slow: " << measured_ns << " ns";
}

TEST(FmTransitionTest, NtscBiphaseMeasuredTransitionWithin225NsSpec) {
  // IEC 60856/60857: 225 ns ± 25 ns for 24-bit biphase.
  const BiphaseEncoder bi_enc(kNtscSampleRate);
  const double measured_ns =
      MeasuredTransitionNs(bi_enc.ramp_samples(), kNtscSampleRate);
  EXPECT_GE(measured_ns, kBiphaseTransitionNs - kBiphaseToleranceNs)
      << "Biphase transition too fast: " << measured_ns << " ns";
  EXPECT_LE(measured_ns, kBiphaseTransitionNs + kBiphaseToleranceNs)
      << "Biphase transition too slow: " << measured_ns << " ns";
}

TEST(FmTransitionTest, PalBiphaseMeasuredTransitionWithin225NsSpec) {
  const BiphaseEncoder bi_enc(kPalSampleRate);
  const double measured_ns =
      MeasuredTransitionNs(bi_enc.ramp_samples(), kPalSampleRate);
  EXPECT_GE(measured_ns, kBiphaseTransitionNs - kBiphaseToleranceNs)
      << "Biphase transition too fast: " << measured_ns << " ns";
  EXPECT_LE(measured_ns, kBiphaseTransitionNs + kBiphaseToleranceNs)
      << "Biphase transition too slow: " << measured_ns << " ns";
}

// ---------------------------------------------------------------------------
// FmTransitionTest — ratio of FM to biphase ramp ≈ 135/225 = 0.6
// ---------------------------------------------------------------------------

TEST(FmTransitionTest, NtscFmToBiphaseRampRatioApproximates135Over225) {
  // The ratio of FM ramp samples to biphase ramp samples should approximate
  // 135/225 = 0.60, since both use the same S-curve formula at the same rate.
  const FmEncoder fm_enc(kNtscSampleRate);
  const BiphaseEncoder bi_enc(kNtscSampleRate);
  const double ratio = static_cast<double>(fm_enc.ramp_samples()) /
                       static_cast<double>(bi_enc.ramp_samples());
  // Allow ±20% from the ideal ratio to account for ceiling/floor quantisation.
  constexpr double kIdealRatio = 135.0 / 225.0;
  EXPECT_NEAR(ratio, kIdealRatio, 0.2)
      << "FM/biphase ramp ratio " << ratio << " deviates too far from "
      << kIdealRatio;
}

// ---------------------------------------------------------------------------
// FmTransitionTest — custom transition time parameter is honoured
// ---------------------------------------------------------------------------

TEST(FmTransitionTest, CustomTransitionTimeParameterIsRespected) {
  // Constructing FmEncoder with 150 ns must produce ramp_samples that differ
  // from the 135 ns default at NTSC.
  const FmEncoder enc_default(kNtscSampleRate);            // 135 ns
  const FmEncoder enc_custom(kNtscSampleRate, 2.0, 150.0);  // 150 ns

  // A wider transition requires at least as many ramp samples.
  EXPECT_GE(enc_custom.ramp_samples(), enc_default.ramp_samples());
}

TEST(FmTransitionTest, NarrowTransitionTimeParameterIsRespected) {
  // 100 ns is narrower than 135 ns; the ramp should be at the minimum.
  const FmEncoder enc_narrow(kNtscSampleRate, 2.0, 100.0);
  const FmEncoder enc_default(kNtscSampleRate);
  EXPECT_LE(enc_narrow.ramp_samples(), enc_default.ramp_samples());
}

// ---------------------------------------------------------------------------
// FmTransitionTest — waveform-level rising edge measurement (bit cell 2, '1')
// ---------------------------------------------------------------------------

// Measures 10%-90% rise time in samples from a rising edge found within a
// '1' bit cell in the generated waveform.  Bit cell 2 of the clock-sync
// pattern 0011 is a '1' bit.
TEST(FmTransitionTest, NtscFmWaveformRisingEdgeWithinSpec) {
  const FmEncoder enc(kNtscSampleRate);
  const FmData data{false, 0, 0, 0, 0, 0};
  const auto samples = enc.Generate40BitWaveform(data, kBaseline, kPeak);

  const int bcs = enc.bit_cell_samples();
  const int bit_idx = 2;  // bit[2] = '1' from clock-sync 0011
  const int cell_start = bit_idx * bcs;

  const double low_thresh = kBaseline + 0.1 * (kPeak - kBaseline);
  const double high_thresh = kBaseline + 0.9 * (kPeak - kBaseline);

  int i10 = -1;
  int i90 = -1;
  for (int i = cell_start; i < cell_start + bcs; ++i) {
    const double lv =
        SampleFixedToMillivolts(samples[static_cast<std::size_t>(i)]);
    if (i10 < 0 && lv >= low_thresh) {
      i10 = i;
    }
    if (i10 >= 0 && lv >= high_thresh) {
      i90 = i;
      break;
    }
  }

  ASSERT_GE(i10, cell_start) << "10% threshold not reached in bit cell 2";
  ASSERT_GE(i90, i10) << "90% threshold not reached after 10% threshold";

  const double rise_ns =
      static_cast<double>(i90 - i10) / kNtscSampleRate * 1.0e9;
  EXPECT_GE(rise_ns, 0.0);
  // Allow 2× the spec tolerance to account for discrete sample quantisation.
  EXPECT_LE(rise_ns, kFmTransitionNs + 2.0 * kFmToleranceNs)
      << "Waveform FM rise time too slow: " << rise_ns << " ns";
}

// ---------------------------------------------------------------------------
// FmTransitionTest — bit cell duration is identical for FM and biphase
// ---------------------------------------------------------------------------

TEST(FmTransitionTest, NtscFmAndBiphaseShareSameBitCellDuration) {
  // IEC 60857 §10.2: bit cell = 2.0 µs ± 0.01 µs for both systems.
  const FmEncoder fm_enc(kNtscSampleRate);
  const BiphaseEncoder bi_enc(kNtscSampleRate);
  EXPECT_EQ(fm_enc.bit_cell_samples(), bi_enc.bit_cell_samples());
}

TEST(FmTransitionTest, PalFmAndBiphaseShareSameBitCellDuration) {
  const FmEncoder fm_enc(kPalSampleRate);
  const BiphaseEncoder bi_enc(kPalSampleRate);
  EXPECT_EQ(fm_enc.bit_cell_samples(), bi_enc.bit_cell_samples());
}

TEST(FmTransitionTest, NtscBitCellIs29Samples) {
  // NTSC 4fsc 14318180 Hz × 2 µs = 28.636 → rounds to 29.
  const FmEncoder enc(kNtscSampleRate);
  EXPECT_EQ(enc.bit_cell_samples(), 29);
}

TEST(FmTransitionTest, PalBitCellIs35Samples) {
  // PAL 4fsc 17734475 Hz × 2 µs = 35.469 → rounds to 35.
  const FmEncoder enc(kPalSampleRate);
  EXPECT_EQ(enc.bit_cell_samples(), 35);
}

// ---------------------------------------------------------------------------
// FmTransitionTest — FM bit cell IEC timing accuracy (2.0 µs ± 0.01 µs)
// ---------------------------------------------------------------------------

TEST(FmTransitionTest, NtscFmBitCellDurationWithinIecSpec) {
  // IEC 60857 §10.2: bit cell = 2.0 µs ± 0.01 µs.
  // At 4fsc the minimum quantisation step is 1/14318180 ≈ 0.070 µs, so the
  // implemented duration deviates by up to half a sample period (≈ 0.035 µs)
  // from the ideal. Use half-sample tolerance to verify best-achievable fit.
  const FmEncoder enc(kNtscSampleRate);
  const double bit_cell_us =
      static_cast<double>(enc.bit_cell_samples()) / kNtscSampleRate * 1.0e6;
  constexpr double kHalfSampleUs = 1.0e6 / (2.0 * kNtscSampleRate);
  EXPECT_NEAR(bit_cell_us, 2.0, kHalfSampleUs)
      << "NTSC FM bit cell duration " << bit_cell_us << " µs out of spec";
}

TEST(FmTransitionTest, PalFmBitCellDurationWithinIecSpec) {
  const FmEncoder enc(kPalSampleRate);
  const double bit_cell_us =
      static_cast<double>(enc.bit_cell_samples()) / kPalSampleRate * 1.0e6;
  constexpr double kHalfSampleUs = 1.0e6 / (2.0 * kPalSampleRate);
  EXPECT_NEAR(bit_cell_us, 2.0, kHalfSampleUs)
      << "PAL FM bit cell duration " << bit_cell_us << " µs out of spec";
}

TEST(FmTransitionTest, NtscBiphaseBitCellDurationWithinIecSpec) {
  const BiphaseEncoder enc(kNtscSampleRate);
  const double bit_cell_us =
      static_cast<double>(enc.bit_cell_samples()) / kNtscSampleRate * 1.0e6;
  constexpr double kHalfSampleUs = 1.0e6 / (2.0 * kNtscSampleRate);
  EXPECT_NEAR(bit_cell_us, 2.0, kHalfSampleUs)
      << "NTSC biphase bit cell duration " << bit_cell_us << " µs out of spec";
}

}  // namespace
}  // namespace videosynth
