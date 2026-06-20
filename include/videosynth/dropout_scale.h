/*
 * File:        dropout_scale.h
 * Module:      dropout_injection
 * Purpose:     Maps dropout scale values (1–20) to internal generation
 *              parameters for random and scratch dropout algorithms.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace videosynth {

// Thread-safety: All functions in this module are thread-safe.
// They are stateless and only operate on their input parameters.
// May be called concurrently from multiple threads.

// Parameters derived from random.scale; computed once per section.
struct RandomDropoutDerivedParams {
  double frequency;  // expected dropout events per frame (Poisson mean)
  int min_duration;  // minimum run length in samples (uniform lower bound)
  int max_duration;  // maximum run length in samples (uniform upper bound)
};

// Parameters derived from scratch.scale; computed once per section.
struct ScratchDropoutDerivedParams {
  int count;              // number of independent scratch events
  int max_dur_frames;     // maximum lifespan of a scratch event in frames
  int max_width_samples;  // maximum width at the scratch peak in samples
};

// Derives random dropout generation parameters from a scale value [1, 20].
// Exponential mapping:
//   frequency(scale)    = 0.5  × 400 ^ ((scale-1)/19)
//   min_duration(scale) = max(1,   round( 5 × 10 ^ ((scale-1)/19)))
//   max_duration(scale) = max(min+1, round(50 ×  8 ^ ((scale-1)/19)))
// Duration is drawn from Uniform(min_duration, max_duration). Frequency grows
// much faster than duration so that high scales produce many short dropouts
// rather than fewer very long ones. max_duration tops out well under one line
// (400 samples at scale 20 vs ~1135 samples/line PAL).
inline RandomDropoutDerivedParams DeriveRandomDropoutParams(int scale) {
  const double t = static_cast<double>(scale - 1) / 19.0;
  const double frequency = 0.5 * std::pow(400.0, t);
  const int min_duration =
      std::max(1, static_cast<int>(std::round(5.0 * std::pow(10.0, t))));
  const int max_duration = std::max(
      min_duration + 1, static_cast<int>(std::round(50.0 * std::pow(8.0, t))));
  return RandomDropoutDerivedParams{frequency, min_duration, max_duration};
}

// Derives scratch dropout generation parameters from a scale value [1, 20].
// Exponential mapping:
//   count(scale)               = max(2, round(2 × 20 ^ ((scale-1)/19)))
//   max_duration_frames(scale) = max(1, round(2 × 250 ^ ((scale-1)/19)))
//   max_width_samples(scale)   = max(1, round(5 × 400 ^ ((scale-1)/19)))
inline ScratchDropoutDerivedParams DeriveScratchDropoutParams(int scale) {
  const double t = static_cast<double>(scale - 1) / 19.0;
  const int count =
      std::max(2, static_cast<int>(std::round(2.0 * std::pow(20.0, t))));
  const int max_dur_frames =
      std::max(1, static_cast<int>(std::round(2.0 * std::pow(250.0, t))));
  const int max_width_samples =
      std::max(1, static_cast<int>(std::round(5.0 * std::pow(400.0, t))));
  return ScratchDropoutDerivedParams{count, max_dur_frames, max_width_samples};
}

}  // namespace videosynth
