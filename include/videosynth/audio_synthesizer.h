/*
 * File:        audio_synthesizer.h
 * Module:      audio_synthesizer
 * Purpose:     Synthesises per-section test-tone waveforms as frame-locked
 *              24-bit mono PCM samples from AudioParameters.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <vector>

#include "videosynth/model.h"

namespace videosynth {

// Generates synthetic test-tone audio for a single channel of a single section
// as 24-bit signed mono PCM samples (carried in the low 24 bits of int32).
//
// The oscillator uses an instantaneous-frequency model: for each output sample
// the current phase is read, then advanced by 2*pi*f(t)/fs, where f(t) is the
// section's frequency trajectory (fixed tone, section-spanning ramp, or
// periodic ramp). Accumulating phase from the instantaneous frequency keeps
// swept tones continuous across the frames within a section.
//
// Waveforms are naive (non-band-limited): sine, square, sawtooth, and triangle
// are generated directly from the phase without anti-aliasing. This is
// acceptable for a test-signal generator; a future task could add
// PolyBLEP/oversampled anti-aliasing if spectral purity becomes a requirement.
//
// Per-section state: the synthesiser carries phase and an elapsed-sample
// offset across successive Synthesize() calls (i.e. across the frames within a
// section). Call BeginSection() when a new section starts, or Reset() to
// restart the current section's trajectory from phase 0. This parallels
// BiphaseInjectionManager::Reset().
//
// Thread-safety: AudioSynthesizer is NOT thread-safe. Its phase and
// elapsed-sample state is mutated by Synthesize(); it must not be called
// concurrently from multiple threads.
class AudioSynthesizer {
 public:
  // sample_rate_hz: authoritative audio sample rate (may be non-integer, e.g.
  //   44,100,000/1001 for System M). Fixed for the lifetime of the object
  //   because it depends only on the video standard.
  explicit AudioSynthesizer(double sample_rate_hz);

  // Configure the synthesiser for a new section and reset phase and
  // elapsed-sample state to the section start (phase 0).
  //
  // Args:
  //   params:                AudioParameters for the section.
  //   total_section_samples: Total audio samples spanning the whole section
  //     (frames * samples_per_frame). Required so a section-spanning ramp
  //     (ramp_period_seconds == 0) can normalise its sweep across the section.
  void BeginSection(const AudioParameters& params,
                    std::int64_t total_section_samples);

  // Produce the next sample_count mono samples for the current section,
  // advancing the internal phase and elapsed-sample offset. Returns 24-bit
  // samples (in int32) in [-kFullScale, +kFullScale]. When params.enabled ==
  // false the returned samples are all zero (silence).
  std::vector<std::int32_t> Synthesize(int sample_count);

  // Restart the current section's trajectory: phase and elapsed-sample offset
  // return to 0 without changing the configured parameters.
  void Reset();

  // Instantaneous frequency (Hz) at a given elapsed-sample offset within the
  // section, following the configured fixed/ramp trajectory. Exposed for
  // testing the frequency trajectories independently of phase accumulation.
  double InstantaneousFrequencyHz(std::int64_t elapsed_sample) const;

  // Full-scale peak used to map a normalised [-1, 1] waveform to 24-bit PCM.
  // Chosen symmetric (2^23 - 1 = 8388607) so positive and negative peaks map to
  // equal magnitudes.
  static constexpr std::int32_t kFullScale = 8388607;

 private:
  // Evaluate the naive waveform for the current shape at phase in [0, 2*pi),
  // scaled by amplitude, returning a value in [-amplitude, +amplitude].
  double WaveformSample(double phase) const;

  double sample_rate_hz_;
  AudioParameters params_ = {};
  std::int64_t total_section_samples_ = 0;

  // Accumulated oscillator phase in radians, wrapped to [0, 2*pi).
  double phase_ = 0.0;
  // Number of samples emitted since the section (or last Reset()) started.
  std::int64_t elapsed_sample_ = 0;
};

}  // namespace videosynth
