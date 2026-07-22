/*
 * File:        programme_status_presenter.h
 * Module:      gui
 * Purpose:     Widget-free encode/decode between the 24-bit programme status
 *              word and its IEC Amendment 2 fields
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "videosynth/model.h"

namespace videosynth::gui {

// Thread-safety: all functions in this module are thread-safe pure functions.

// The user-settable fields of the programme status code, per
// IEC 60856/60857 Amendment 2 Appendix C.1:
//   8 DC/BA X3 X4 X5  — key nibble 8, CX pair, data nibbles, Hamming check.
// X5 is not a field here: it is the extended (8,4) Hamming check over X4 and
// is always computed, never entered.
struct ProgrammeStatusFields {
  bool cx_on = true;              // DC = CX on, BA = CX off (bits 19–12)
  bool disc_size_8_inch = false;  // X31: 0 = 12 inch, 1 = 8 inch
  bool second_side = false;       // X32: 0 = first side, 1 = second side
  bool teletext_present = false;  // X33: 1 = teletext present on the disc
  bool copy_permitted = false;    // X34: 1 = copy permitted
  uint8_t audio_video_mode = 0;   // X4 nibble 0–15 (8 modes defined)
};

// Builds the 24-bit programme status word from the fields, computing the X5
// Hamming check nibble from X4.
uint32_t BuildProgrammeStatusCode(const ProgrammeStatusFields& fields);

// Decodes a 24-bit word into fields. Returns nullopt when the word does not
// carry the programme status structure: key nibble must be 8 and the CX pair
// must be DC or BA. The X5 nibble is ignored (it is derived, and is
// recomputed on build).
std::optional<ProgrammeStatusFields> DecodeProgrammeStatusCode(uint32_t code);

// Formats a 24-bit code as canonical "0x" + 6 upper-case hex digits.
std::string FormatProgrammeStatusHex(uint32_t code);

// Display labels for the 16 X4 audio/video modes, indexed by mode number.
// Amendment 2 defines modes 0–3 and 8; the rest read "Future use". Mode 2 is
// standard-specific: PAL defines "Audio subcarriers off" (IEC 60856 Amd. 2)
// while System M leaves it future-use (IEC 60857 Amd. 2).
std::vector<std::string> AudioVideoModeLabels(Standard standard);

}  // namespace videosynth::gui
