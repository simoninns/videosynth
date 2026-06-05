/*
 * File:        status_code_generator.h
 * Module:      status_code_generator
 * Purpose:     Programme status code builder with IEC Hamming check generation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>

namespace videosynth {

// CX noise reduction mode for the programme status code.
// IEC 60856/60857: DC = CX on, BA = CX off.
enum class CxMode : uint8_t {
  kOn,   // CX noise reduction on:  nibble pair DC (0xD, 0xC) at bits 19–12
  kOff,  // CX noise reduction off: nibble pair BA (0xB, 0xA) at bits 19–12
};

// Audio/video mode for the programme status code X4 nibble.
// IEC 60856/60857 Amendment 2 Appendix C: 8 defined modes (X41–X44).
// Modes 4–7 and 9–15 are reserved for future use.
enum class AudioVideoMode : uint8_t {
  kStandardVideoStereo = 0x0,    // Standard video, stereo audio
  kStandardVideoStereoB = 0x1,   // Standard video, stereo channel 2
  kStandardVideoStereoC = 0x2,   // Standard video, stereo channel 3
  kStandardVideoStereoD = 0x3,   // Standard video, stereo channel 4
  kMonoDump = 0x8,               // Mono dump (bilingual first language)
  // Modes 4–7 and 9–15: future use (valid as raw nibble values)
};

// Builds a 24-bit programme status code from semantic parameters, computing
// the X5 Hamming check nibble automatically.
//
// Format: 8 CX₁ CX₂ X₃ X₄ X₅   (24 bits = 6 nibbles, MSB first)
//   Nibble 0 — bits 23-20: 0x8   (key nibble, bit 23 = 1)
//   Nibble 1 — bits 19-16: 0xD (CX on) or 0xB (CX off)
//   Nibble 2 — bits 15-12: 0xC (CX on) or 0xA (CX off)
//   Nibble 3 — bits 11-8:  X₃ data nibble; LSB (X₃₄) = copy_permitted
//   Nibble 4 — bits 7-4:   X₄ audio/video mode (0–15)
//   Nibble 5 — bits 3-0:   X₅ Hamming check nibble (computed from X₄)
//
// Hamming check X₅ (IEC 60856/60857 Amendment 2 Appendix C):
//   X₅ is the extended (8,4) Hamming check nibble over X₄.
//   For X₄ bits d₁d₂d₃d₄ (MSB-first, d₁ = bit 3 of X₄):
//     X₅₁ = d₁ ⊕ d₂ ⊕ d₄
//     X₅₂ = d₁ ⊕ d₃ ⊕ d₄
//     X₅₃ = d₂ ⊕ d₃ ⊕ d₄
//     X₅₄ = d₁ ⊕ d₂ ⊕ d₃ ⊕ d₄   (overall parity — SECDED extension)
//   X₅ = (X₅₁ << 3) | (X₅₂ << 2) | (X₅₃ << 1) | X₅₄
//
// Amendment 2 changes (relative to original IEC spec):
//   - X₃₄ meaning: 0 = copy prohibited, 1 = copy permitted
//     (original: X₃₄ = FM-FM multiplex indicator)
//   - Audio/video mode table simplified (see AudioVideoMode enum above)
//   - Hamming G/M matrices: UNCHANGED from original spec
//
// Thread-safety: Thread-safe (immutable after construction).
class ProgrammeStatusCodeBuilder {
 public:
  // Constructs a programme status code from semantic parameters.
  //   cx_mode:          CX noise reduction on or off.
  //   copy_permitted:   X₃₄ bit — true = copy permitted, false = prohibited.
  //   audio_video_mode: Raw X₄ nibble (0–15); 8 modes defined by Amendment 2.
  ProgrammeStatusCodeBuilder(CxMode cx_mode, bool copy_permitted,
                             uint8_t audio_video_mode);

  // Returns the 24-bit programme status code including the computed X₅ check.
  uint32_t Build() const;

  // Computes the X₅ Hamming check nibble from X₄ using the IEC Appendix C
  // extended (8,4) Hamming code. x4 is the raw 4-bit nibble.
  static uint8_t ComputeHammingCheck(uint8_t x4);

  // Builds the X₃ nibble from the copy_permitted flag.
  // X₃₄ (LSB of X₃) = copy_permitted; upper bits are 0 (reserved).
  static uint8_t BuildX3(bool copy_permitted);

  // Returns the two-nibble CX pattern at bits 19–12:
  //   CX on:  bits 19-16 = 0xD, bits 15-12 = 0xC  → 0x000DC000 masked
  //   CX off: bits 19-16 = 0xB, bits 15-12 = 0xA  → 0x000BA000 masked
  static uint32_t BuildCxField(CxMode cx_mode);

  // Returns true if mode is one of the 8 IEC Amendment 2 defined audio/video
  // modes (0–3 and 8). Modes 4–7 and 9–15 are future-use.
  static bool IsDefinedAudioVideoMode(uint8_t mode);

 private:
  CxMode cx_mode_;
  bool copy_permitted_;
  uint8_t audio_video_mode_;
};

}  // namespace videosynth
