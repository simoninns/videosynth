/*
 * File:        programme_status_presenter.cpp
 * Module:      gui
 * Purpose:     Widget-free encode/decode between the 24-bit programme status
 *              word and its IEC Amendment 2 fields
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "programme_status_presenter.h"

#include <cstdio>

#include "videosynth/status_code_generator.h"

namespace videosynth::gui {

namespace {

// X3 nibble layout, MSB first (IEC 60856/60857 Amendment 2 Appendix C.1):
// X31 disc size, X32 disc side, X33 teletext, X34 copy permission.
constexpr uint8_t kX31DiscSizeBit = 0x08u;
constexpr uint8_t kX32DiscSideBit = 0x04u;
constexpr uint8_t kX33TeletextBit = 0x02u;
constexpr uint8_t kX34CopyBit = 0x01u;

}  // namespace

uint32_t BuildProgrammeStatusCode(const ProgrammeStatusFields& fields) {
  const uint8_t x3 =
      static_cast<uint8_t>((fields.disc_size_8_inch ? kX31DiscSizeBit : 0u) |
                           (fields.second_side ? kX32DiscSideBit : 0u) |
                           (fields.teletext_present ? kX33TeletextBit : 0u) |
                           (fields.copy_permitted ? kX34CopyBit : 0u));
  const uint8_t x4 = fields.audio_video_mode & 0x0Fu;
  const uint8_t x5 = ProgrammeStatusCodeBuilder::ComputeHammingCheck(x4);
  const uint32_t cx_field = ProgrammeStatusCodeBuilder::BuildCxField(
      fields.cx_on ? CxMode::kOn : CxMode::kOff);

  return 0x800000u | cx_field | (static_cast<uint32_t>(x3) << 8) |
         (static_cast<uint32_t>(x4) << 4) | static_cast<uint32_t>(x5);
}

std::optional<ProgrammeStatusFields> DecodeProgrammeStatusCode(uint32_t code) {
  if ((code & 0xF00000u) != 0x800000u) {
    return std::nullopt;  // Key nibble must be 8.
  }
  const uint32_t cx_pair = (code >> 12) & 0xFFu;
  if (cx_pair != 0xDCu && cx_pair != 0xBAu) {
    return std::nullopt;  // CX pair must be DC (on) or BA (off).
  }

  ProgrammeStatusFields fields;
  fields.cx_on = cx_pair == 0xDCu;
  const uint8_t x3 = static_cast<uint8_t>((code >> 8) & 0x0Fu);
  fields.disc_size_8_inch = (x3 & kX31DiscSizeBit) != 0u;
  fields.second_side = (x3 & kX32DiscSideBit) != 0u;
  fields.teletext_present = (x3 & kX33TeletextBit) != 0u;
  fields.copy_permitted = (x3 & kX34CopyBit) != 0u;
  fields.audio_video_mode = static_cast<uint8_t>((code >> 4) & 0x0Fu);
  return fields;
}

std::string FormatProgrammeStatusHex(uint32_t code) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "0x%06X", code & 0xFFFFFFu);
  return buffer;
}

std::vector<std::string> AudioVideoModeLabels(Standard standard) {
  const bool system_m =
      standard == Standard::kNtsc || standard == Standard::kPalM;

  // IEC 60856/60857 Amendment 2 Appendix C.1: X41–X44 audio/video mode table.
  std::vector<std::string> labels(16, "Future use");
  labels[0] = "Standard video, stereo";
  labels[1] = "Standard video, mono";
  // Mode 2: audio channels defined for PAL only; System M keeps the video
  // column standard but leaves the channels future-use.
  labels[2] = system_m ? "Standard video, future use"
                       : "Standard video, audio subcarriers off";
  labels[3] = "Standard video, bilingual";
  labels[8] = "Standard video, mono dump";
  return labels;
}

}  // namespace videosynth::gui
