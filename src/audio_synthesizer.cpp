/*
 * File:        audio_synthesizer.cpp
 * Module:      audio_synthesizer
 * Purpose:     Synthesises per-section test-tone waveforms as frame-locked
 *              int16 mono PCM samples from AudioParameters.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/audio_synthesizer.h"

#include <algorithm>
#include <cmath>

namespace videosynth {

namespace {

constexpr double kTwoPi = 2.0 * M_PI;

// Linearly interpolate between two frequencies for a normalised position p in
// [0, 1] under the given ramp direction. kUp runs start->end, kDown runs
// end->start, and kBounce runs start->end->start (a triangle over the span).
double RampFrequency(double start_hz, double end_hz, AudioRampMode mode,
                     double p) {
  p = std::clamp(p, 0.0, 1.0);
  switch (mode) {
    case AudioRampMode::kUp:
      return start_hz + (end_hz - start_hz) * p;
    case AudioRampMode::kDown:
      return end_hz + (start_hz - end_hz) * p;
    case AudioRampMode::kBounce: {
      // Triangle: q rises 0->1 over the first half then falls 1->0 over the
      // second half, so the frequency reaches end_hz at the midpoint and
      // returns to start_hz at the span boundary.
      const double q = 1.0 - std::fabs(2.0 * p - 1.0);
      return start_hz + (end_hz - start_hz) * q;
    }
    case AudioRampMode::kUnknown:
      break;
  }
  return start_hz;
}

}  // namespace

AudioSynthesizer::AudioSynthesizer(double sample_rate_hz)
    : sample_rate_hz_(sample_rate_hz) {}

void AudioSynthesizer::BeginSection(const AudioParameters& params,
                                    std::int64_t total_section_samples) {
  params_ = params;
  total_section_samples_ = total_section_samples;
  Reset();
}

void AudioSynthesizer::Reset() {
  phase_ = 0.0;
  elapsed_sample_ = 0;
}

double AudioSynthesizer::InstantaneousFrequencyHz(
    std::int64_t elapsed_sample) const {
  if (!params_.ramp_enabled) {
    return params_.frequency_hz;
  }

  if (params_.ramp_period_seconds > 0.0) {
    // Periodic ramp: one sweep lasts ramp_period_seconds and repeats. Position
    // within the current period is in [0, 1).
    const double period_samples = params_.ramp_period_seconds * sample_rate_hz_;
    if (period_samples <= 0.0) {
      return params_.ramp_start_hz;
    }
    const double phase_in_period =
        std::fmod(static_cast<double>(elapsed_sample), period_samples);
    const double p = phase_in_period / period_samples;
    return RampFrequency(params_.ramp_start_hz, params_.ramp_end_hz,
                         params_.ramp_mode, p);
  }

  // Section-spanning ramp: sweep once linearly across the whole section. Using
  // (total - 1) as the denominator places ramp_end_hz exactly on the section's
  // final sample.
  double p = 0.0;
  if (total_section_samples_ > 1) {
    p = static_cast<double>(elapsed_sample) /
        static_cast<double>(total_section_samples_ - 1);
  }
  return RampFrequency(params_.ramp_start_hz, params_.ramp_end_hz,
                       params_.ramp_mode, p);
}

double AudioSynthesizer::WaveformSample(double phase) const {
  const double amplitude = params_.amplitude;
  // Normalised phase position in [0, 1).
  const double t = phase / kTwoPi;

  switch (params_.waveform) {
    case AudioWaveform::kSine:
      return amplitude * std::sin(phase);
    case AudioWaveform::kSquare:
      // Bipolar full swing: +amplitude for the first half cycle, -amplitude
      // for the second.
      return phase < M_PI ? amplitude : -amplitude;
    case AudioWaveform::kSawtooth:
      // Monotonic ramp from -amplitude up to +amplitude, then wraps.
      return amplitude * (2.0 * t - 1.0);
    case AudioWaveform::kTriangle:
      // Symmetric: -amplitude -> +amplitude -> -amplitude over one cycle.
      return amplitude * (t < 0.5 ? (-1.0 + 4.0 * t) : (3.0 - 4.0 * t));
    case AudioWaveform::kUnknown:
      break;
  }
  return 0.0;
}

std::vector<std::int16_t> AudioSynthesizer::Synthesize(int sample_count) {
  std::vector<std::int16_t> out;
  if (sample_count <= 0) {
    return out;
  }
  out.reserve(static_cast<std::size_t>(sample_count));

  if (!params_.enabled) {
    // Disabled sections emit silence but still advance the elapsed offset so
    // downstream frame-lock accounting stays exact.
    out.assign(static_cast<std::size_t>(sample_count), 0);
    elapsed_sample_ += sample_count;
    return out;
  }

  for (int i = 0; i < sample_count; ++i) {
    const double value = WaveformSample(phase_);
    const std::int64_t scaled = std::llround(value * kFullScale);
    const std::int64_t clamped =
        std::clamp<std::int64_t>(scaled, -kFullScale, kFullScale);
    out.push_back(static_cast<std::int16_t>(clamped));

    // Advance phase using the frequency at this sample position.
    const double freq = InstantaneousFrequencyHz(elapsed_sample_);
    phase_ += kTwoPi * freq / sample_rate_hz_;
    if (phase_ >= kTwoPi) {
      phase_ = std::fmod(phase_, kTwoPi);
    } else if (phase_ < 0.0) {
      // Guard against negative frequencies producing a negative phase.
      phase_ = std::fmod(phase_, kTwoPi);
      if (phase_ < 0.0) {
        phase_ += kTwoPi;
      }
    }
    ++elapsed_sample_;
  }

  return out;
}

}  // namespace videosynth
