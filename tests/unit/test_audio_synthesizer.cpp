/*
 * File:        test_audio_synthesizer.cpp
 * Module:      audio_synthesizer_tests
 * Purpose:     Validates AudioSynthesizer oscillator output, frequency
 *              trajectories, per-section continuity, and waveform shapes.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

#include "videosynth/audio_synthesizer.h"
#include "videosynth/model.h"

namespace videosynth {
namespace {

// 48 kHz audio rate (see timing_constants.h) and the PAL per-frame sample count
// used by most tests; 48000 divides cleanly for integer-cycle assertions.
constexpr double kRate = 48000.0;
constexpr int kPalSamplesPerFrame = 1920;

// A generous tolerance for naive (non-band-limited) waveform-shape assertions,
// scaled to the 24-bit full-scale so it tracks the ~35 k per-sample step of the
// steep shapes at 100 Hz / 480 samples per cycle.
constexpr int kShapeTol = AudioSynthesizer::kFullScale / 64;

AudioParameters FixedSine(double frequency_hz, double amplitude) {
  AudioParameters params;
  params.enabled = true;
  params.waveform = AudioWaveform::kSine;
  params.frequency_hz = frequency_hz;
  params.amplitude = amplitude;
  return params;
}

// Peak magnitude across a sample buffer.
std::int64_t PeakMagnitude(const std::vector<std::int32_t>& samples) {
  std::int64_t peak = 0;
  for (const std::int32_t s : samples) {
    peak = std::max<std::int64_t>(peak, std::abs(static_cast<std::int64_t>(s)));
  }
  return peak;
}

// ---------------------------------------------------------------------------
// Oscillator core
// ---------------------------------------------------------------------------

TEST(AudioSynthesizerTest, ProducesRequestedSampleCount) {
  AudioSynthesizer synth(kRate);
  synth.BeginSection(FixedSine(1000.0, 0.5), kPalSamplesPerFrame);

  const std::vector<std::int32_t> frame = synth.Synthesize(kPalSamplesPerFrame);

  EXPECT_EQ(frame.size(), static_cast<std::size_t>(kPalSamplesPerFrame));
}

TEST(AudioSynthesizerTest, PeakAmplitudeRespectsAmplitudeFraction) {
  AudioSynthesizer synth(kRate);
  // 1 kHz at 48 kHz => 48 samples/cycle, and a sample lands exactly on the
  // crest, so the peak equals the configured amplitude to within rounding.
  synth.BeginSection(FixedSine(1000.0, 0.5), kPalSamplesPerFrame);

  const std::vector<std::int32_t> frame = synth.Synthesize(kPalSamplesPerFrame);

  const std::int64_t expected_peak = static_cast<std::int64_t>(
      std::lround(0.5 * AudioSynthesizer::kFullScale));
  EXPECT_LE(PeakMagnitude(frame), expected_peak);
  EXPECT_NEAR(PeakMagnitude(frame), expected_peak, 2);
}

TEST(AudioSynthesizerTest, SineHasNearZeroDcOverIntegerCycles) {
  AudioSynthesizer synth(kRate);
  // 100 Hz at 48000 Hz => 480 samples/cycle; 4800 samples == exactly 10 cycles.
  synth.BeginSection(FixedSine(100.0, 0.8), 4800);

  const std::vector<std::int32_t> samples = synth.Synthesize(4800);

  const std::int64_t sum = std::accumulate(
      samples.begin(), samples.end(), static_cast<std::int64_t>(0),
      [](std::int64_t acc, std::int32_t s) { return acc + s; });
  const double mean =
      static_cast<double>(sum) / static_cast<double>(samples.size());
  EXPECT_NEAR(mean, 0.0, 2.0);
}

TEST(AudioSynthesizerTest, DisabledSectionEmitsSilence) {
  AudioParameters params = FixedSine(1000.0, 0.9);
  params.enabled = false;

  AudioSynthesizer synth(kRate);
  synth.BeginSection(params, kPalSamplesPerFrame);
  const std::vector<std::int32_t> frame = synth.Synthesize(kPalSamplesPerFrame);

  EXPECT_EQ(frame.size(), static_cast<std::size_t>(kPalSamplesPerFrame));
  EXPECT_EQ(PeakMagnitude(frame), 0);
}

// ---------------------------------------------------------------------------
// Frequency trajectories
// ---------------------------------------------------------------------------

AudioParameters Ramp(double start_hz, double end_hz, AudioRampMode mode,
                     double period_seconds) {
  AudioParameters params;
  params.enabled = true;
  params.waveform = AudioWaveform::kSine;
  params.ramp_enabled = true;
  params.ramp_start_hz = start_hz;
  params.ramp_end_hz = end_hz;
  params.ramp_mode = mode;
  params.ramp_period_seconds = period_seconds;
  params.amplitude = 0.5;
  return params;
}

TEST(AudioSynthesizerTest, SectionSpanningRampUpHitsStartMidEnd) {
  const std::int64_t total = 1000;
  AudioSynthesizer synth(kRate);
  synth.BeginSection(Ramp(1000.0, 5000.0, AudioRampMode::kUp, 0.0), total);

  EXPECT_NEAR(synth.InstantaneousFrequencyHz(0), 1000.0, 1e-6);
  EXPECT_NEAR(synth.InstantaneousFrequencyHz((total - 1) / 2), 3000.0, 5.0);
  EXPECT_NEAR(synth.InstantaneousFrequencyHz(total - 1), 5000.0, 1e-6);
}

TEST(AudioSynthesizerTest, SectionSpanningRampDownReversesEndpoints) {
  const std::int64_t total = 1000;
  AudioSynthesizer synth(kRate);
  synth.BeginSection(Ramp(1000.0, 5000.0, AudioRampMode::kDown, 0.0), total);

  EXPECT_NEAR(synth.InstantaneousFrequencyHz(0), 5000.0, 1e-6);
  EXPECT_NEAR(synth.InstantaneousFrequencyHz(total - 1), 1000.0, 1e-6);
}

TEST(AudioSynthesizerTest, SectionSpanningBounceReturnsToStart) {
  const std::int64_t total = 1001;  // odd => exact midpoint sample exists
  AudioSynthesizer synth(kRate);
  synth.BeginSection(Ramp(1000.0, 5000.0, AudioRampMode::kBounce, 0.0), total);

  EXPECT_NEAR(synth.InstantaneousFrequencyHz(0), 1000.0, 1e-6);
  EXPECT_NEAR(synth.InstantaneousFrequencyHz((total - 1) / 2), 5000.0, 1e-6);
  EXPECT_NEAR(synth.InstantaneousFrequencyHz(total - 1), 1000.0, 1e-6);
}

TEST(AudioSynthesizerTest, PeriodicRampRepeatsAtPeriodBoundary) {
  // 0.01 s period at 48000 Hz => 480 samples per sweep.
  AudioSynthesizer synth(kRate);
  synth.BeginSection(Ramp(1000.0, 5000.0, AudioRampMode::kUp, 0.01), 48000);

  const double at_start = synth.InstantaneousFrequencyHz(0);
  const double at_next_period = synth.InstantaneousFrequencyHz(480);
  const double at_two_periods = synth.InstantaneousFrequencyHz(960);
  EXPECT_NEAR(at_start, 1000.0, 1e-6);
  EXPECT_NEAR(at_next_period, 1000.0, 1e-6);
  EXPECT_NEAR(at_two_periods, 1000.0, 1e-6);

  // Midway through a period sits halfway up the sweep.
  EXPECT_NEAR(synth.InstantaneousFrequencyHz(480 + 240), 3000.0, 20.0);
}

// ---------------------------------------------------------------------------
// Per-section continuity and reset
// ---------------------------------------------------------------------------

TEST(AudioSynthesizerTest, PhaseIsContinuousAcrossFrameBoundary) {
  const int two_frame_samples = 2 * kPalSamplesPerFrame;
  AudioSynthesizer split(kRate);
  split.BeginSection(FixedSine(1234.0, 0.7), two_frame_samples);
  std::vector<std::int32_t> two_frames = split.Synthesize(kPalSamplesPerFrame);
  const std::vector<std::int32_t> second =
      split.Synthesize(kPalSamplesPerFrame);
  two_frames.insert(two_frames.end(), second.begin(), second.end());

  // Generating both frames in one call must be byte-identical to generating
  // them across two calls, proving phase/elapsed state carries across frames.
  AudioSynthesizer whole(kRate);
  whole.BeginSection(FixedSine(1234.0, 0.7), two_frame_samples);
  const std::vector<std::int32_t> single = whole.Synthesize(two_frame_samples);

  EXPECT_EQ(two_frames, single);
}

TEST(AudioSynthesizerTest, ResetRestartsFromPhaseZero) {
  AudioSynthesizer synth(kRate);
  synth.BeginSection(FixedSine(1000.0, 0.6), kPalSamplesPerFrame);

  const std::vector<std::int32_t> first = synth.Synthesize(64);
  synth.Synthesize(500);  // advance further
  synth.Reset();
  const std::vector<std::int32_t> after_reset = synth.Synthesize(64);

  EXPECT_EQ(first, after_reset);
  // Sine starts at phase 0 => first sample is 0.
  EXPECT_EQ(after_reset.front(), 0);
}

TEST(AudioSynthesizerTest, BeginSectionResetsElapsedForRamp) {
  AudioSynthesizer synth(kRate);
  synth.BeginSection(Ramp(1000.0, 5000.0, AudioRampMode::kUp, 0.0), 1000);
  synth.Synthesize(1000);

  // Reconfiguring a new section restarts the sweep from the start frequency.
  synth.BeginSection(Ramp(2000.0, 8000.0, AudioRampMode::kUp, 0.0), 1000);
  EXPECT_NEAR(synth.InstantaneousFrequencyHz(0), 2000.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Waveform shapes (one cycle at a known frequency and rate)
// ---------------------------------------------------------------------------

// 100 Hz at 48000 Hz => exactly 480 samples per cycle.
constexpr int kCycleSamples = 480;

AudioParameters ShapeParams(AudioWaveform waveform) {
  AudioParameters params;
  params.enabled = true;
  params.waveform = waveform;
  params.frequency_hz = 100.0;
  params.amplitude = 1.0;
  return params;
}

TEST(AudioSynthesizerTest, SquareIsBipolarFullSwing) {
  AudioSynthesizer synth(kRate);
  synth.BeginSection(ShapeParams(AudioWaveform::kSquare), kCycleSamples);
  const std::vector<std::int32_t> cycle = synth.Synthesize(kCycleSamples);

  // Every sample sits at one of the two full-scale rails.
  for (const std::int32_t s : cycle) {
    EXPECT_EQ(std::abs(s), AudioSynthesizer::kFullScale);
  }
  // First half positive, second half negative.
  EXPECT_GT(cycle.front(), 0);
  EXPECT_LT(cycle[kCycleSamples * 3 / 4], 0);
}

TEST(AudioSynthesizerTest, SawtoothRampsMonotonicallyThenWraps) {
  AudioSynthesizer synth(kRate);
  synth.BeginSection(ShapeParams(AudioWaveform::kSawtooth), kCycleSamples);
  const std::vector<std::int32_t> cycle = synth.Synthesize(kCycleSamples);

  // Starts near -full, rises monotonically to near +full before the wrap.
  EXPECT_LT(cycle.front(), -AudioSynthesizer::kFullScale + kShapeTol);
  EXPECT_GT(cycle[kCycleSamples - 1], AudioSynthesizer::kFullScale - kShapeTol);
  for (int i = 1; i < kCycleSamples; ++i) {
    EXPECT_GE(cycle[i], cycle[i - 1]);
  }
}

TEST(AudioSynthesizerTest, TriangleIsSymmetric) {
  AudioSynthesizer synth(kRate);
  synth.BeginSection(ShapeParams(AudioWaveform::kTriangle), kCycleSamples);
  const std::vector<std::int32_t> cycle = synth.Synthesize(kCycleSamples);

  // Starts at -full, crosses zero at the quarter point, peaks at +full at the
  // half point, and is symmetric about that peak.
  EXPECT_NEAR(cycle.front(), -AudioSynthesizer::kFullScale, kShapeTol);
  EXPECT_NEAR(cycle[kCycleSamples / 4], 0, kShapeTol);
  EXPECT_NEAR(cycle[kCycleSamples / 2], AudioSynthesizer::kFullScale,
              kShapeTol);
  EXPECT_NEAR(cycle[3 * kCycleSamples / 4], 0, kShapeTol);
  // Symmetry: samples equidistant from the half-cycle peak match.
  const int peak_index = kCycleSamples / 2;
  for (int d = 1; peak_index + d < kCycleSamples; ++d) {
    EXPECT_NEAR(cycle[peak_index - d], cycle[peak_index + d], kShapeTol);
  }
}

TEST(AudioSynthesizerTest, SineMatchesReferenceSamples) {
  AudioSynthesizer synth(kRate);
  synth.BeginSection(ShapeParams(AudioWaveform::kSine), kCycleSamples);
  const std::vector<std::int32_t> cycle = synth.Synthesize(kCycleSamples);

  // Compare against an independent closed-form sine over one 480-sample cycle.
  for (int i = 0; i < kCycleSamples; ++i) {
    const double phase = 2.0 * M_PI * 100.0 * i / kRate;
    const std::int64_t expected = static_cast<std::int64_t>(
        std::lround(std::sin(phase) * AudioSynthesizer::kFullScale));
    EXPECT_NEAR(cycle[i], expected, 1);
  }
}

}  // namespace
}  // namespace videosynth
