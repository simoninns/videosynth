/*
 * File:        status_code_generator.cpp
 * Module:      status_code_generator
 * Purpose:     Programme status code builder with IEC Hamming check generation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/status_code_generator.h"

#include <cstdint>

namespace videosynth {

ProgrammeStatusCodeBuilder::ProgrammeStatusCodeBuilder(CxMode cx_mode,
                                                       bool copy_permitted,
                                                       uint8_t audio_video_mode)
    : cx_mode_(cx_mode),
      copy_permitted_(copy_permitted),
      audio_video_mode_(audio_video_mode & 0x0Fu) {}

uint32_t ProgrammeStatusCodeBuilder::Build() const {
  const uint8_t x3 = BuildX3(copy_permitted_);
  const uint8_t x4 = audio_video_mode_ & 0x0Fu;
  const uint8_t x5 = ComputeHammingCheck(x4);
  const uint32_t cx_field = BuildCxField(cx_mode_);

  return 0x800000u | cx_field | (static_cast<uint32_t>(x3) << 8) |
         (static_cast<uint32_t>(x4) << 4) | static_cast<uint32_t>(x5);
}

// IEC 60856/60857 Amendment 2 Appendix C extended (8,4) Hamming check.
// For X₄ bits d₁d₂d₃d₄ (MSB-first, d₁ = bit 3):
//   X₅₁ = d₁ ⊕ d₂ ⊕ d₄
//   X₅₂ = d₁ ⊕ d₃ ⊕ d₄
//   X₅₃ = d₂ ⊕ d₃ ⊕ d₄
//   X₅₄ = d₁ ⊕ d₂ ⊕ d₃ ⊕ d₄  (overall parity)
// static
uint8_t ProgrammeStatusCodeBuilder::ComputeHammingCheck(uint8_t x4) {
  const bool d1 = (x4 & 0x08u) != 0u;
  const bool d2 = (x4 & 0x04u) != 0u;
  const bool d3 = (x4 & 0x02u) != 0u;
  const bool d4 = (x4 & 0x01u) != 0u;

  const bool p1 = d1 ^ d2 ^ d4;
  const bool p2 = d1 ^ d3 ^ d4;
  const bool p3 = d2 ^ d3 ^ d4;
  const bool p4 = d1 ^ d2 ^ d3 ^ d4;

  return static_cast<uint8_t>((p1 ? 0x08u : 0u) | (p2 ? 0x04u : 0u) |
                              (p3 ? 0x02u : 0u) | (p4 ? 0x01u : 0u));
}

// static
uint8_t ProgrammeStatusCodeBuilder::BuildX3(bool copy_permitted) {
  return copy_permitted ? 0x01u : 0x00u;
}

// static
uint32_t ProgrammeStatusCodeBuilder::BuildCxField(CxMode cx_mode) {
  if (cx_mode == CxMode::kOn) {
    // DC: nibble 1 = 0xD (bits 19-16), nibble 2 = 0xC (bits 15-12)
    return (0x000Du << 16) | (0x000Cu << 12);
  }
  // BA: nibble 1 = 0xB (bits 19-16), nibble 2 = 0xA (bits 15-12)
  return (0x000Bu << 16) | (0x000Au << 12);
}

// static
bool ProgrammeStatusCodeBuilder::IsDefinedAudioVideoMode(uint8_t mode) {
  return mode <= 3u || mode == 8u;
}

}  // namespace videosynth
