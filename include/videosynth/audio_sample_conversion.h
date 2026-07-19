/*
 * File:        audio_sample_conversion.h
 * Module:      audio_sample_conversion
 * Purpose:     Converts 24-bit synthesised PCM samples to the 16-bit sample
 *              domain used by LaserDisc digital audio (EFM).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <vector>

namespace videosynth {

// Thread-safety: the functions in this module are thread-safe. They are
// stateless and operate only on their arguments.

// IEC 60908-1999 clause 12: compact-disc audio samples are 16-bit two's
// complement values. AudioSynthesizer emits 24-bit samples (carried in int32,
// symmetric about zero at +/-8388607), so the EFM path scales by 2^8.
inline constexpr int kAudio24To16ShiftBits = 8;
inline constexpr std::int32_t kAudio24To16Divisor = std::int32_t{1}
                                                    << kAudio24To16ShiftBits;

// Converts one 24-bit sample to 16-bit, rounding to nearest (ties away from
// zero) and saturating at the 16-bit end points. Rounding is symmetric about
// zero, so a symmetric input waveform stays symmetric everywhere below
// saturation. Both 24-bit peaks round to magnitude 32768: the positive peak
// therefore saturates to 32767 and the negative peak lands on -32768.
inline std::int16_t ConvertSample24To16(std::int32_t sample_24bit) {
  const std::int64_t magnitude = static_cast<std::int64_t>(sample_24bit) < 0
                                     ? -static_cast<std::int64_t>(sample_24bit)
                                     : static_cast<std::int64_t>(sample_24bit);
  const std::int64_t rounded =
      (magnitude + kAudio24To16Divisor / 2) / kAudio24To16Divisor;
  const std::int64_t signed_value = sample_24bit < 0 ? -rounded : rounded;
  if (signed_value > 32767) {
    return 32767;
  }
  if (signed_value < -32768) {
    return -32768;
  }
  return static_cast<std::int16_t>(signed_value);
}

// Converts a block of 24-bit samples to 16-bit, preserving order.
inline std::vector<std::int16_t> ConvertSamples24To16(
    const std::vector<std::int32_t>& samples_24bit) {
  std::vector<std::int16_t> converted;
  converted.reserve(samples_24bit.size());
  for (const std::int32_t sample : samples_24bit) {
    converted.push_back(ConvertSample24To16(sample));
  }
  return converted;
}

}  // namespace videosynth
