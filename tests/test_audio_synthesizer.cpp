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

// PAL audio rate and samples-per-frame (see timing_constants.h) used by most
// tests; chosen because 44100 divides cleanly for integer-cycle assertions.
constexpr double kPalRate = 44100.0;
constexpr int kPalSamplesPerFrame = 1764;

AudioParameters FixedSine(double frequency_hz, double amplitude) {
  AudioParameters params;
  params.enabled = true;
  params.waveform = AudioWaveform::kSine;
  params.frequency_hz = frequency_hz;
  params.amplitude = amplitude;
  return params;
}

// Peak magnitude across a sample buffer.
int PeakMagnitude(const std::vector<std::int16_t>& samples) {
  int peak = 0;
  for (const std::int16_t s : samples) {
    peak = std::max(peak, std::abs(static_cast<int>(s)));
  }
  return peak;
}

// ---------------------------------------------------------------------------
// Oscillator core
// ---------------------------------------------------------------------------

TEST(AudioSynthesizerTest, ProducesRequestedSampleCount) {
  AudioSynthesizer synth(kPalRate);
  synth.BeginSection(FixedSine(1000.0, 0.5), kPalSamplesPerFrame);

  const std::vector<std::int16_t> frame = synth.Synthesize(kPalSamplesPerFrame);

  EXPECT_EQ(frame.size(), static_cast<std::size_t>(kPalSamplesPerFrame));
}

TEST(AudioSynthesizerTest, PeakAmplitudeRespectsAmplitudeFraction) {
  AudioSynthesizer synth(kPalRate);
  // 1 kHz over one PAL frame (~40 cycles) reaches the peak many times.
  synth.BeginSection(FixedSine(1000.0, 0.5), kPalSamplesPerFrame);

  const std::vector<std::int16_t> frame = synth.Synthesize(kPalSamplesPerFrame);

  const int expected_peak =
      static_cast<int>(std::lround(0.5 * AudioSynthesizer::kFullScale));
  // Naive sine sampling will not land exactly on the crest; allow a small
  // tolerance and require it never exceeds the configured peak.
  EXPECT_LE(PeakMagnitude(frame), expected_peak);
  EXPECT_NEAR(PeakMagnitude(frame), expected_peak, 30);
}

TEST(AudioSynthesizerTest, SineHasNearZeroDcOverIntegerCycles) {
  AudioSynthesizer synth(kPalRate);
  // 100 Hz at 44100 Hz => 441 samples/cycle; 4410 samples == exactly 10 cycles.
  synth.BeginSection(FixedSine(100.0, 0.8), 4410);

  const std::vector<std::int16_t> samples = synth.Synthesize(4410);

  const std::int64_t sum = std::accumulate(
      samples.begin(), samples.end(), static_cast<std::int64_t>(0),
      [](std::int64_t acc, std::int16_t s) { return acc + s; });
  const double mean =
      static_cast<double>(sum) / static_cast<double>(samples.size());
  EXPECT_NEAR(mean, 0.0, 1.0);
}

TEST(AudioSynthesizerTest, DisabledSectionEmitsSilence) {
  AudioParameters params = FixedSine(1000.0, 0.9);
  params.enabled = false;

  AudioSynthesizer synth(kPalRate);
  synth.BeginSection(params, kPalSamplesPerFrame);
  const std::vector<std::int16_t> frame = synth.Synthesize(kPalSamplesPerFrame);

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
  AudioSynthesizer synth(kPalRate);
  synth.BeginSection(Ramp(1000.0, 5000.0, AudioRampMode::kUp, 0.0), total);

  EXPECT_NEAR(synth.InstantaneousFrequencyHz(0), 1000.0, 1e-6);
  EXPECT_NEAR(synth.InstantaneousFrequencyHz((total - 1) / 2), 3000.0, 5.0);
  EXPECT_NEAR(synth.InstantaneousFrequencyHz(total - 1), 5000.0, 1e-6);
}

TEST(AudioSynthesizerTest, SectionSpanningRampDownReversesEndpoints) {
  const std::int64_t total = 1000;
  AudioSynthesizer synth(kPalRate);
  synth.BeginSection(Ramp(1000.0, 5000.0, AudioRampMode::kDown, 0.0), total);

  EXPECT_NEAR(synth.InstantaneousFrequencyHz(0), 5000.0, 1e-6);
  EXPECT_NEAR(synth.InstantaneousFrequencyHz(total - 1), 1000.0, 1e-6);
}

TEST(AudioSynthesizerTest, SectionSpanningBounceReturnsToStart) {
  const std::int64_t total = 1001;  // odd => exact midpoint sample exists
  AudioSynthesizer synth(kPalRate);
  synth.BeginSection(Ramp(1000.0, 5000.0, AudioRampMode::kBounce, 0.0), total);

  EXPECT_NEAR(synth.InstantaneousFrequencyHz(0), 1000.0, 1e-6);
  EXPECT_NEAR(synth.InstantaneousFrequencyHz((total - 1) / 2), 5000.0, 1e-6);
  EXPECT_NEAR(synth.InstantaneousFrequencyHz(total - 1), 1000.0, 1e-6);
}

TEST(AudioSynthesizerTest, PeriodicRampRepeatsAtPeriodBoundary) {
  // 0.01 s period at 44100 Hz => 441 samples per sweep.
  AudioSynthesizer synth(kPalRate);
  synth.BeginSection(Ramp(1000.0, 5000.0, AudioRampMode::kUp, 0.01), 44100);

  const double at_start = synth.InstantaneousFrequencyHz(0);
  const double at_next_period = synth.InstantaneousFrequencyHz(441);
  const double at_two_periods = synth.InstantaneousFrequencyHz(882);
  EXPECT_NEAR(at_start, 1000.0, 1e-6);
  EXPECT_NEAR(at_next_period, 1000.0, 1e-6);
  EXPECT_NEAR(at_two_periods, 1000.0, 1e-6);

  // Midway through a period sits halfway up the sweep.
  EXPECT_NEAR(synth.InstantaneousFrequencyHz(441 + 220), 3000.0, 20.0);
}

// ---------------------------------------------------------------------------
// Per-section continuity and reset
// ---------------------------------------------------------------------------

TEST(AudioSynthesizerTest, PhaseIsContinuousAcrossFrameBoundary) {
  const int two_frame_samples = 2 * kPalSamplesPerFrame;
  AudioSynthesizer split(kPalRate);
  split.BeginSection(FixedSine(1234.0, 0.7), two_frame_samples);
  std::vector<std::int16_t> two_frames = split.Synthesize(kPalSamplesPerFrame);
  const std::vector<std::int16_t> second =
      split.Synthesize(kPalSamplesPerFrame);
  two_frames.insert(two_frames.end(), second.begin(), second.end());

  // Generating both frames in one call must be byte-identical to generating
  // them across two calls, proving phase/elapsed state carries across frames.
  AudioSynthesizer whole(kPalRate);
  whole.BeginSection(FixedSine(1234.0, 0.7), two_frame_samples);
  const std::vector<std::int16_t> single = whole.Synthesize(two_frame_samples);

  EXPECT_EQ(two_frames, single);
}

TEST(AudioSynthesizerTest, ResetRestartsFromPhaseZero) {
  AudioSynthesizer synth(kPalRate);
  synth.BeginSection(FixedSine(1000.0, 0.6), kPalSamplesPerFrame);

  const std::vector<std::int16_t> first = synth.Synthesize(64);
  synth.Synthesize(500);  // advance further
  synth.Reset();
  const std::vector<std::int16_t> after_reset = synth.Synthesize(64);

  EXPECT_EQ(first, after_reset);
  // Sine starts at phase 0 => first sample is 0.
  EXPECT_EQ(after_reset.front(), 0);
}

TEST(AudioSynthesizerTest, BeginSectionResetsElapsedForRamp) {
  AudioSynthesizer synth(kPalRate);
  synth.BeginSection(Ramp(1000.0, 5000.0, AudioRampMode::kUp, 0.0), 1000);
  synth.Synthesize(1000);

  // Reconfiguring a new section restarts the sweep from the start frequency.
  synth.BeginSection(Ramp(2000.0, 8000.0, AudioRampMode::kUp, 0.0), 1000);
  EXPECT_NEAR(synth.InstantaneousFrequencyHz(0), 2000.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Waveform shapes (one cycle at a known frequency and rate)
// ---------------------------------------------------------------------------

// 100 Hz at 44100 Hz => exactly 441 samples per cycle.
constexpr int kCycleSamples = 441;

AudioParameters ShapeParams(AudioWaveform waveform) {
  AudioParameters params;
  params.enabled = true;
  params.waveform = waveform;
  params.frequency_hz = 100.0;
  params.amplitude = 1.0;
  return params;
}

TEST(AudioSynthesizerTest, SquareIsBipolarFullSwing) {
  AudioSynthesizer synth(kPalRate);
  synth.BeginSection(ShapeParams(AudioWaveform::kSquare), kCycleSamples);
  const std::vector<std::int16_t> cycle = synth.Synthesize(kCycleSamples);

  // Every sample sits at one of the two full-scale rails.
  for (const std::int16_t s : cycle) {
    EXPECT_EQ(std::abs(static_cast<int>(s)), AudioSynthesizer::kFullScale);
  }
  // First half positive, second half negative.
  EXPECT_GT(cycle.front(), 0);
  EXPECT_LT(cycle[kCycleSamples * 3 / 4], 0);
}

TEST(AudioSynthesizerTest, SawtoothRampsMonotonicallyThenWraps) {
  AudioSynthesizer synth(kPalRate);
  synth.BeginSection(ShapeParams(AudioWaveform::kSawtooth), kCycleSamples);
  const std::vector<std::int16_t> cycle = synth.Synthesize(kCycleSamples);

  // Starts near -full, rises monotonically to near +full before the wrap.
  EXPECT_LT(cycle.front(), -AudioSynthesizer::kFullScale + 200);
  EXPECT_GT(cycle[kCycleSamples - 1], AudioSynthesizer::kFullScale - 300);
  for (int i = 1; i < kCycleSamples; ++i) {
    EXPECT_GE(cycle[i], cycle[i - 1]);
  }
}

TEST(AudioSynthesizerTest, TriangleIsSymmetric) {
  AudioSynthesizer synth(kPalRate);
  synth.BeginSection(ShapeParams(AudioWaveform::kTriangle), kCycleSamples);
  const std::vector<std::int16_t> cycle = synth.Synthesize(kCycleSamples);

  // Starts at -full, crosses zero at the quarter point, peaks at +full at the
  // half point, and is symmetric about that peak.
  EXPECT_NEAR(cycle.front(), -AudioSynthesizer::kFullScale, 400);
  EXPECT_NEAR(cycle[kCycleSamples / 4], 0, 400);
  EXPECT_NEAR(cycle[kCycleSamples / 2], AudioSynthesizer::kFullScale, 400);
  EXPECT_NEAR(cycle[3 * kCycleSamples / 4], 0, 400);
  // Symmetry: samples equidistant from the half-cycle peak match.
  const int peak_index = kCycleSamples / 2;
  for (int d = 1; peak_index + d < kCycleSamples; ++d) {
    EXPECT_NEAR(cycle[peak_index - d], cycle[peak_index + d], 400);
  }
}

TEST(AudioSynthesizerTest, SineMatchesReferenceSamples) {
  AudioSynthesizer synth(kPalRate);
  synth.BeginSection(ShapeParams(AudioWaveform::kSine), kCycleSamples);
  const std::vector<std::int16_t> cycle = synth.Synthesize(kCycleSamples);

  // Compare against an independent closed-form sine over one 441-sample cycle.
  for (int i = 0; i < kCycleSamples; ++i) {
    const double phase = 2.0 * M_PI * 100.0 * i / kPalRate;
    const int expected = static_cast<int>(
        std::lround(std::sin(phase) * AudioSynthesizer::kFullScale));
    EXPECT_NEAR(cycle[i], expected, 1);
  }
}

}  // namespace
}  // namespace videosynth
