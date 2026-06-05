/*
 * File:        fm_encoder.h
 * Module:      fm_encoder
 * Purpose:     40-bit FM coded signal generation for NTSC LaserDisc VBI
 *              injection per IEC 60857 §10.2, Figure 13.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "videosynth/fixed_point.h"
#include "videosynth/model.h"

namespace videosynth {

// Data payload for a 40-bit FM coded signal (IEC 60857 §10.2, Figure 13).
//
// The five nibbles X1–X5 carry 20 data bits placed at specific bit positions:
//   fm_picture_number (CAV): X1–X5 are decimal digits of the picture number.
//   fm_programme_time (CLV): X1–X2 = BCD minutes, X3–X4 = BCD seconds,
//                             X5 = mode indicator (0xA=lead-in, 0xB=transition,
//                             0xD=picture, 0xC=lead-out).
//
// Each nibble occupies 4 bit cells transmitted LSB first:
//   X5 → bits [12-15], X4 → bits [16-19], X3 → bits [20-23],
//   X2 → bits [24-27], X1 → bits [28-31]  (0-indexed from bit 1).
struct FmData {
  bool field_one;  // true = first field (bit 5 logic 1), false = second field
  uint8_t x1;      // data nibble 1 — MSNibble of the data word (bits 28-31)
  uint8_t x2;      // data nibble 2 (bits 24-27)
  uint8_t x3;      // data nibble 3 (bits 20-23)
  uint8_t x4;      // data nibble 4 (bits 16-19)
  uint8_t x5;      // data nibble 5 — LSNibble of the data word (bits 12-15)
};

// Encodes 40-bit FM coded signals for NTSC LaserDisc VBI injection.
//
// Bit layout (0-indexed, MSB = index 0):
//   [0-3]   Clock sync:             0011
//   [4]     Video field indicator:  1 = first field, 0 = second field
//   [5-11]  Leading recognition:    1110010
//   [12-15] Data nibble X5 (LSB first)
//   [16-19] Data nibble X4 (LSB first)
//   [20-23] Data nibble X3 (LSB first)
//   [24-27] Data nibble X2 (LSB first)
//   [28-31] Data nibble X1 (LSB first)
//   [32]    Parity: odd parity over bits [0-31]
//   [33-39] Trailing recognition:   0001101
//
// Encoding rules (same as 24-bit biphase, IEC 60857 §10.2):
//   - Positive (rising) transition at center of bit cell = logic '1'
//   - Negative (falling) transition at center of bit cell = logic '0'
//   - Inter-bit transitions occur between consecutive identical bits
//   - Transition time: 135 ns ± 15 ns (10%-90%), S-curve shaped
//   - Bit cell duration: 2.0 µs ± 0.01 µs
//
// White flag:
//   - Constant 100 IRE level across the full video line (not modulated)
//   - Indicates the start of a new picture field during the active programme
//
// Thread-safety: FmEncoder is immutable after construction and may be called
// concurrently from multiple threads.
class FmEncoder {
 public:
  // Constructs an FmEncoder for the given sample rate.
  //
  // Args:
  //   sample_rate_hz:         Output sample rate in Hz (e.g. 14318180 for NTSC
  //                           4fsc).
  //   bit_cell_duration_us:   Bit cell duration in microseconds (default 2.0).
  //   transition_duration_ns: 10%-90% transition time in nanoseconds
  //                            (default 135.0 for 40-bit FM).
  explicit FmEncoder(double sample_rate_hz, double bit_cell_duration_us = 2.0,
                     double transition_duration_ns = 135.0);

  // Returns samples for the raw 40-bit FM waveform (no line padding).
  //
  // The returned vector length is 40 * bit_cell_samples().
  // Bits are transmitted according to the IEC 60857 §10.2 Figure 13 pattern.
  //
  // Args:
  //   data:               FM payload — field indicator and data nibbles X1-X5.
  //   baseline_level_mv:  Signal low level in millivolts.
  //   peak_level_mv:      Signal high level in millivolts.
  std::vector<SampleFixed> Generate40BitWaveform(const FmData& data,
                                                 double baseline_level_mv,
                                                 double peak_level_mv) const;

  // Returns samples for a full video line with the 40-bit FM signal at the
  // start. The remainder of the buffer is filled with baseline_level_mv.
  //
  // The returned vector length equals
  // GetTimingConstants(standard).samples_per_line_4fsc.
  //
  // Args:
  //   data:               FM payload — field indicator and data nibbles X1-X5.
  //   standard:           PAL or NTSC (determines line buffer length).
  //   baseline_level_mv:  Signal low level in millivolts.
  //   peak_level_mv:      Signal high level in millivolts.
  std::vector<SampleFixed> Generate40BitCode(const FmData& data,
                                             Standard standard,
                                             double baseline_level_mv,
                                             double peak_level_mv) const;

  // Returns samples for a full video line at constant 100 IRE (white flag).
  //
  // The entire line is filled with peak_level_mv per IEC 60857 §10.2.1.
  // The returned vector length equals
  // GetTimingConstants(standard).samples_per_line_4fsc.
  //
  // Args:
  //   standard:       PAL or NTSC (determines line buffer length).
  //   peak_level_mv:  100 IRE level in millivolts (e.g. 714.3 mV for NTSC).
  std::vector<SampleFixed> GenerateWhiteFlag(Standard standard,
                                             double peak_level_mv) const;

  // Number of samples per bit cell at the configured sample rate.
  int bit_cell_samples() const { return bit_cell_samples_; }

  // Number of samples spanning the full 0%-100% S-curve transition.
  // The 10%-90% measured transition time equals transition_duration_ns.
  int ramp_samples() const { return ramp_samples_; }

  // Builds the 40-bit boolean pattern for the given FmData payload.
  // Exposed for testing; callers should not normally need this directly.
  static std::array<bool, 40> BuildBitPattern(const FmData& data);

 private:
  int ramp_samples_;
  int bit_cell_samples_;

  // Applies a single shaped step transition centred at center_sample.
  // rising == true:  baseline_level_mv → peak_level_mv
  // rising == false: peak_level_mv → baseline_level_mv
  void ApplyStepTransition(std::vector<double>& samples, int center_sample,
                           bool rising, double baseline_level_mv,
                           double peak_level_mv) const;

  // Encodes `bits` as a Manchester waveform with IEC-compliant inter-bit
  // transitions. Returns 40 * bit_cell_samples() samples.
  std::vector<SampleFixed> GenerateBitsManchester(
      const std::array<bool, 40>& bits, double baseline_level_mv,
      double peak_level_mv) const;
};

}  // namespace videosynth
