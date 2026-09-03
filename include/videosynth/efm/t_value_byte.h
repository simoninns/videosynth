/*
 * File:        t_value_byte.h
 * Module:      efm
 * Purpose:     Byte layout of the EFM extension binary stream: one pit/land
 *              run length packed with the producer's doubt about it.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>

namespace videosynth::efm {

// CVBS File Format Specification, extensions/efm-extension-format.md, "Binary
// Data File": every byte of `<basename>.efm` carries one t-value in bits 3-0
// and the producer's doubt about that t-value in bits 7-4.
//
//     bit  7 6 5 4   3 2 1 0
//           doubt     t-value
//
// Doubt is the inverse of confidence: 0 means fully trusted, 15 means
// positively distrusted (an erasure candidate for downstream error
// correction), and intermediate values are ordinal only. A fully trusted
// t-value therefore packs to its own plain value, so a stream with nothing to
// doubt is simply the sequence of raw run lengths.
inline constexpr std::uint8_t kTValueMask = 0x0FU;
inline constexpr unsigned kDoubtShift = 4U;
inline constexpr std::uint8_t kMaxDoubt = 0x0FU;

// The doubt of a fully trusted t-value, i.e. maximum confidence.
inline constexpr std::uint8_t kNoDoubt = 0x00U;

// The doubt videosynth attaches to the t-values it writes. The channel stream
// is synthesised rather than recovered from a signal, so every run length is
// exact by construction and carries no doubt at all.
//
// If a future feature ever emits t-values that are *not* certain — a modelled
// readout error, a degraded or dropout-affected EFM stream — it must pack the
// doubt it actually has for that value instead of this constant, rather than
// widening the constant itself.
inline constexpr std::uint8_t kSynthesisedDoubt = kNoDoubt;

// The stream byte carrying `t_value` at `doubt`. Both fields are truncated to
// their four bits, so an out-of-range argument cannot corrupt the other field.
constexpr std::uint8_t PackTValueByte(std::uint8_t t_value,
                                      std::uint8_t doubt) {
  return static_cast<std::uint8_t>(
      static_cast<unsigned>(doubt & kMaxDoubt) << kDoubtShift |
      static_cast<unsigned>(t_value & kTValueMask));
}

// The t-value carried by a stream byte.
constexpr std::uint8_t TValueOfByte(std::uint8_t byte) {
  return static_cast<std::uint8_t>(byte & kTValueMask);
}

// The doubt carried by a stream byte.
constexpr std::uint8_t DoubtOfByte(std::uint8_t byte) {
  return static_cast<std::uint8_t>(byte >> kDoubtShift);
}

}  // namespace videosynth::efm
