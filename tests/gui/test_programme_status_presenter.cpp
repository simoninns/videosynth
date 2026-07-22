/*
 * File:        test_programme_status_presenter.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for the programme status field encode/decode against
 *              IEC 60856/60857 Amendment 2 Appendix C.1
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "programme_status_presenter.h"
#include "videosynth/model.h"
#include "videosynth/status_code_generator.h"

namespace videosynth::gui {
namespace {

TEST(ProgrammeStatusPresenterTest, BuildDefaultsProducesCxOnAllClearWord) {
  // CX on, 12-inch first side, no teletext, copy prohibited, stereo:
  // 8 DC 0 0 0 — the canonical example word.
  const ProgrammeStatusFields fields;
  EXPECT_EQ(BuildProgrammeStatusCode(fields), 0x8DC000u);
}

TEST(ProgrammeStatusPresenterTest, BuildCxOffEncodesBaNibblePair) {
  ProgrammeStatusFields fields;
  fields.cx_on = false;
  EXPECT_EQ(BuildProgrammeStatusCode(fields), 0x8BA000u);
}

TEST(ProgrammeStatusPresenterTest, BuildX3FlagsSetSpecBitPositions) {
  // X31 disc size = bit 11, X32 disc side = bit 10, X33 teletext = bit 9,
  // X34 copy = bit 8 (MSB-first within the X3 nibble).
  ProgrammeStatusFields fields;
  fields.disc_size_8_inch = true;
  EXPECT_EQ(BuildProgrammeStatusCode(fields) & 0x000F00u, 0x000800u);

  fields = {};
  fields.second_side = true;
  EXPECT_EQ(BuildProgrammeStatusCode(fields) & 0x000F00u, 0x000400u);

  fields = {};
  fields.teletext_present = true;
  EXPECT_EQ(BuildProgrammeStatusCode(fields) & 0x000F00u, 0x000200u);

  fields = {};
  fields.copy_permitted = true;
  EXPECT_EQ(BuildProgrammeStatusCode(fields) & 0x000F00u, 0x000100u);
}

TEST(ProgrammeStatusPresenterTest, BuildX4ModeAppendsHammingCheckNibble) {
  ProgrammeStatusFields fields;
  fields.audio_video_mode = 0x8u;  // Mono dump
  const uint32_t code = BuildProgrammeStatusCode(fields);
  EXPECT_EQ((code >> 4) & 0x0Fu, 0x8u);
  EXPECT_EQ(code & 0x0Fu,
            ProgrammeStatusCodeBuilder::ComputeHammingCheck(0x8u));
}

TEST(ProgrammeStatusPresenterTest, BuildMatchesProgrammeStatusCodeBuilder) {
  // The presenter must agree with the runtime builder wherever their inputs
  // overlap (X31–X33 clear, since the builder only carries X34).
  for (const bool cx_on : {false, true}) {
    for (const bool copy : {false, true}) {
      for (uint8_t mode = 0; mode < 16; ++mode) {
        ProgrammeStatusFields fields;
        fields.cx_on = cx_on;
        fields.copy_permitted = copy;
        fields.audio_video_mode = mode;
        const ProgrammeStatusCodeBuilder builder(
            cx_on ? CxMode::kOn : CxMode::kOff, copy, mode);
        EXPECT_EQ(BuildProgrammeStatusCode(fields), builder.Build())
            << "cx_on=" << cx_on << " copy=" << copy
            << " mode=" << static_cast<int>(mode);
      }
    }
  }
}

TEST(ProgrammeStatusPresenterTest, DecodeRoundTripsAllFieldCombinations) {
  for (uint32_t bits = 0; bits < 32; ++bits) {
    for (uint8_t mode = 0; mode < 16; ++mode) {
      ProgrammeStatusFields fields;
      fields.cx_on = (bits & 0x10u) != 0u;
      fields.disc_size_8_inch = (bits & 0x08u) != 0u;
      fields.second_side = (bits & 0x04u) != 0u;
      fields.teletext_present = (bits & 0x02u) != 0u;
      fields.copy_permitted = (bits & 0x01u) != 0u;
      fields.audio_video_mode = mode;

      const auto decoded =
          DecodeProgrammeStatusCode(BuildProgrammeStatusCode(fields));
      ASSERT_TRUE(decoded.has_value());
      const ProgrammeStatusFields round_trip =
          decoded.value_or(ProgrammeStatusFields{});
      EXPECT_EQ(round_trip.cx_on, fields.cx_on);
      EXPECT_EQ(round_trip.disc_size_8_inch, fields.disc_size_8_inch);
      EXPECT_EQ(round_trip.second_side, fields.second_side);
      EXPECT_EQ(round_trip.teletext_present, fields.teletext_present);
      EXPECT_EQ(round_trip.copy_permitted, fields.copy_permitted);
      EXPECT_EQ(round_trip.audio_video_mode, fields.audio_video_mode);
    }
  }
}

TEST(ProgrammeStatusPresenterTest, DecodeRejectsWrongKeyNibble) {
  EXPECT_FALSE(DecodeProgrammeStatusCode(0x7DC000u).has_value());
  EXPECT_FALSE(DecodeProgrammeStatusCode(0x9DC000u).has_value());
}

TEST(ProgrammeStatusPresenterTest, DecodeRejectsNonCxNibblePairs) {
  // Only DC (CX on) and BA (CX off) mark a programme status word; other
  // biphase codes (e.g. picture numbers, user codes) must not decode.
  EXPECT_FALSE(DecodeProgrammeStatusCode(0x880000u).has_value());
  EXPECT_FALSE(DecodeProgrammeStatusCode(0x8DA000u).has_value());
  EXPECT_FALSE(DecodeProgrammeStatusCode(0x8BC000u).has_value());
}

TEST(ProgrammeStatusPresenterTest, DecodeIgnoresStaleHammingNibble) {
  // X5 is derived from X4; a stale check nibble must not block decoding, and
  // rebuilding must correct it.
  const auto decoded = DecodeProgrammeStatusCode(0x8DC08Fu);  // X5 wrong
  ASSERT_TRUE(decoded.has_value());
  const ProgrammeStatusFields fields =
      decoded.value_or(ProgrammeStatusFields{});
  EXPECT_EQ(fields.audio_video_mode, 0x8u);
  EXPECT_EQ(BuildProgrammeStatusCode(fields) & 0x0Fu,
            ProgrammeStatusCodeBuilder::ComputeHammingCheck(0x8u));
}

TEST(ProgrammeStatusPresenterTest, FormatHexIsCanonicalSixDigitForm) {
  EXPECT_EQ(FormatProgrammeStatusHex(0x8DC000u), "0x8DC000");
  EXPECT_EQ(FormatProgrammeStatusHex(0x8BA10Eu), "0x8BA10E");
}

TEST(ProgrammeStatusPresenterTest, AudioVideoModeLabelsFollowAmendment2) {
  const std::vector<std::string> pal = AudioVideoModeLabels(Standard::kPal);
  ASSERT_EQ(pal.size(), 16u);
  EXPECT_EQ(pal[0], "Standard video, stereo");
  EXPECT_EQ(pal[1], "Standard video, mono");
  EXPECT_EQ(pal[2], "Standard video, audio subcarriers off");
  EXPECT_EQ(pal[3], "Standard video, bilingual");
  EXPECT_EQ(pal[8], "Standard video, mono dump");
  EXPECT_EQ(pal[4], "Future use");
  EXPECT_EQ(pal[15], "Future use");
}

TEST(ProgrammeStatusPresenterTest, AudioVideoModeLabelsMode2SystemMFutureUse) {
  // IEC 60857 Amendment 2 leaves mode 2's audio channels future-use.
  for (const Standard standard : {Standard::kNtsc, Standard::kPalM}) {
    const std::vector<std::string> labels = AudioVideoModeLabels(standard);
    ASSERT_EQ(labels.size(), 16u);
    EXPECT_EQ(labels[2], "Standard video, future use");
  }
}

}  // namespace
}  // namespace videosynth::gui
