/*
 * File:        test_value_constraints.cpp
 * Module:      value_constraints_tests
 * Purpose:     Comprehensive tests for IEC 60856/60857 code value range
 *              constraints across all biphase and 40-bit FM code types.
 *              Task 6.14: Tests for code value range constraints.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cstdint>

#include "videosynth/cav_code_generator.h"
#include "videosynth/clv_code_generator.h"
#include "videosynth/fm_code_generator.h"
#include "videosynth/model.h"

namespace videosynth {
namespace {

// ---------------------------------------------------------------------------
// ValueConstraintsTest — CAV picture number range (IEC 60856/60857 §10.1.3)
// ---------------------------------------------------------------------------

TEST(ValueConstraintsTest, CavPictureNumberPalRangeIsZeroTo99999) {
  EXPECT_TRUE(
      CavPictureNumberGenerator::IsValidPictureNumber(0, Standard::kPal));
  EXPECT_TRUE(
      CavPictureNumberGenerator::IsValidPictureNumber(99999, Standard::kPal));
  EXPECT_FALSE(
      CavPictureNumberGenerator::IsValidPictureNumber(100000, Standard::kPal));
  EXPECT_FALSE(
      CavPictureNumberGenerator::IsValidPictureNumber(-1, Standard::kPal));
}

TEST(ValueConstraintsTest, CavPictureNumberNtscRangeIsZeroTo79999) {
  // IEC 60857 §10.1.3: NTSC max picture number is 79,999.
  EXPECT_TRUE(
      CavPictureNumberGenerator::IsValidPictureNumber(0, Standard::kNtsc));
  EXPECT_TRUE(
      CavPictureNumberGenerator::IsValidPictureNumber(79999, Standard::kNtsc));
  EXPECT_FALSE(
      CavPictureNumberGenerator::IsValidPictureNumber(80000, Standard::kNtsc));
  EXPECT_FALSE(
      CavPictureNumberGenerator::IsValidPictureNumber(99999, Standard::kNtsc));
}

TEST(ValueConstraintsTest,
     CavPictureNumberEncodingProducesOnlyDecimalDigitNibbles) {
  // IEC 60856 §10.1.3: X₁–X₅ must be BCD decimal digits (0–9 as hex
  // nibbles). EncodePictureNumber uses modular decimal decomposition, so any
  // value in [0, 99999] produces nibbles ≤ 9.
  for (int n : {0, 1, 9999, 10000, 50000, 79999, 99999}) {
    const uint32_t code = CavPictureNumberGenerator::EncodePictureNumber(n);
    // Extract nibbles X₁–X₅ (bits 19-0).
    for (int shift = 0; shift <= 16; shift += 4) {
      const uint8_t nibble = static_cast<uint8_t>((code >> shift) & 0x0Fu);
      EXPECT_LE(nibble, 9u)
          << "Nibble at shift " << shift << " for n=" << n << " is "
          << static_cast<int>(nibble) << " (must be 0-9)";
    }
  }
}

TEST(ValueConstraintsTest, CavPictureNumberPalMaxConstantIs99999) {
  EXPECT_EQ(CavPictureNumberGenerator::kPalMaxValue, 99999);
}

TEST(ValueConstraintsTest, CavPictureNumberNtscMaxConstantIs79999) {
  EXPECT_EQ(CavPictureNumberGenerator::kNtscMaxValue, 79999);
}

TEST(ValueConstraintsTest, CavPictureNumberMaxPictureNumberHelperPal) {
  EXPECT_EQ(CavPictureNumberGenerator::MaxPictureNumber(Standard::kPal), 99999);
}

TEST(ValueConstraintsTest, CavPictureNumberMaxPictureNumberHelperNtsc) {
  EXPECT_EQ(CavPictureNumberGenerator::MaxPictureNumber(Standard::kNtsc),
            79999);
}

// ---------------------------------------------------------------------------
// ValueConstraintsTest — chapter number range (IEC 60856/60857 §10.1.5)
// ---------------------------------------------------------------------------

TEST(ValueConstraintsTest, ChapterNumberRangeIsZeroTo79) {
  // IEC 60856/60857 §10.1.5: chapter number 0–79 only.
  EXPECT_TRUE(ChapterNumberGenerator::IsValidChapterNumber(0));
  EXPECT_TRUE(ChapterNumberGenerator::IsValidChapterNumber(79));
  EXPECT_FALSE(ChapterNumberGenerator::IsValidChapterNumber(80));
  EXPECT_FALSE(ChapterNumberGenerator::IsValidChapterNumber(-1));
}

TEST(ValueConstraintsTest, ChapterMaxConstantIs79) {
  EXPECT_EQ(ChapterNumberGenerator::kMaxChapterNumber, 79);
}

TEST(ValueConstraintsTest, ChapterMinimumLengthConstantIs30) {
  EXPECT_EQ(ChapterNumberGenerator::kMinimumChapterTracks, 30);
}

TEST(ValueConstraintsTest, ChapterStopBitTransitionTrackIs400) {
  // IEC 60856/60857 §10.1.5: stop-bit = 0 for first 400 tracks.
  EXPECT_EQ(ChapterNumberGenerator::kStopBitTransitionTrack, 400);
}

// ---------------------------------------------------------------------------
// ValueConstraintsTest — users_code X₁ range (IEC 60856/60857 §10.1.6)
// ---------------------------------------------------------------------------

TEST(ValueConstraintsTest, UsersCodeX1RangeIsZeroToSeven) {
  // IEC 60856/60857 §10.1.9: X₁ nibble (bits 19-16) must be 0–7.
  // Use canonical D nibble (0xD at bits 15-12) to isolate the X₁ constraint.
  for (int x1 = 0; x1 <= 7; ++x1) {
    const uint32_t code =
        0x800000u | (static_cast<uint32_t>(x1) << 16) | 0x00D000u;
    EXPECT_TRUE(UsersCodeGenerator::IsValidUsersCode(code))
        << "X1=" << x1 << " should be valid";
  }
  for (int x1 = 8; x1 <= 15; ++x1) {
    const uint32_t code =
        0x800000u | (static_cast<uint32_t>(x1) << 16) | 0x00D000u;
    EXPECT_FALSE(UsersCodeGenerator::IsValidUsersCode(code))
        << "X1=" << x1 << " should be invalid";
  }
}

TEST(ValueConstraintsTest, UsersCodeDNibbleMustBeHexD) {
  // IEC 60856/60857 §10.1.9: D nibble (bits 15-12) must be 0xD.
  // X₁=0 used throughout to isolate the D nibble constraint.
  for (int d = 0; d <= 15; ++d) {
    const uint32_t code = 0x800000u | (static_cast<uint32_t>(d) << 12);
    if (d == 0xD) {
      EXPECT_TRUE(UsersCodeGenerator::IsValidUsersCode(code))
          << "D=0xD should be valid";
    } else {
      EXPECT_FALSE(UsersCodeGenerator::IsValidUsersCode(code))
          << "D=0x" << d << " should be invalid";
    }
  }
}

TEST(ValueConstraintsTest, UsersCodeX1ExtractedCorrectly) {
  // Verify that ExtractX1 returns the nibble at bits 19-16.
  // Code format 8X₁DX₃X₄X₅: for 0x83D234, X₁ nibble is at bits 19-16 = 0x3.
  const uint32_t code = 0x83D234u;  // X₁ = 3, D = 0xD
  EXPECT_EQ(UsersCodeGenerator::ExtractX1(code), 3u);
}

// ---------------------------------------------------------------------------
// ValueConstraintsTest — CLV programme time code (IEC 60856 §10.1.7)
// ---------------------------------------------------------------------------

TEST(ValueConstraintsTest, ProgrammeTimeCodeHoursRangeIsZeroTo15) {
  // IEC 60856/60857 §10.1.7: X₁ = hours, 4-bit nibble (0–15).
  EXPECT_TRUE(ProgrammeTimeCodeGenerator::IsValidTimeCode(0, 0));
  EXPECT_TRUE(ProgrammeTimeCodeGenerator::IsValidTimeCode(15, 0));
  EXPECT_FALSE(ProgrammeTimeCodeGenerator::IsValidTimeCode(16, 0));
  EXPECT_FALSE(ProgrammeTimeCodeGenerator::IsValidTimeCode(-1, 0));
}

TEST(ValueConstraintsTest, ProgrammeTimeCodeMinutesRangeIsZeroTo59) {
  // IEC 60856/60857 §10.1.7: X₂X₃ = BCD minutes (0–59).
  // X₂ = tens (0–5), X₃ = units (0–9).
  EXPECT_TRUE(ProgrammeTimeCodeGenerator::IsValidTimeCode(0, 0));
  EXPECT_TRUE(ProgrammeTimeCodeGenerator::IsValidTimeCode(0, 59));
  EXPECT_FALSE(ProgrammeTimeCodeGenerator::IsValidTimeCode(0, 60));
  EXPECT_FALSE(ProgrammeTimeCodeGenerator::IsValidTimeCode(0, -1));
}

TEST(ValueConstraintsTest, ProgrammeTimeCodeMaxHoursConstantIs15) {
  EXPECT_EQ(ProgrammeTimeCodeGenerator::kMaxHours, 15);
}

TEST(ValueConstraintsTest, ProgrammeTimeCodeMaxMinutesConstantIs59) {
  EXPECT_EQ(ProgrammeTimeCodeGenerator::kMaxMinutes, 59);
}

TEST(ValueConstraintsTest, ProgrammeTimeCodeEncodingPreservesBcdMinutes) {
  // IEC §10.1.7: X₂ = minutes tens (0–5), X₃ = minutes units (0–9).
  // For minutes=53: tens nibble should be 5, units nibble should be 3.
  const uint32_t code = ProgrammeTimeCodeGenerator::EncodeTimeCode(0, 53);
  const uint8_t tens = (code >> 4) & 0x0Fu;
  const uint8_t units = (code >> 0) & 0x0Fu;
  EXPECT_EQ(tens, 5u) << "Minutes tens nibble should be 5";
  EXPECT_EQ(units, 3u) << "Minutes units nibble should be 3";
}

TEST(ValueConstraintsTest, ProgrammeTimeCodeEncodingPreservesHoursNibble) {
  // Hours (X₁) is at bits 19-16.
  const uint32_t code = ProgrammeTimeCodeGenerator::EncodeTimeCode(7, 0);
  const uint8_t hours = (code >> 16) & 0x0Fu;
  EXPECT_EQ(hours, 7u) << "Hours nibble should be 7";
}

// ---------------------------------------------------------------------------
// ValueConstraintsTest — CLV picture number (IEC 60856 §10.1.9)
// ---------------------------------------------------------------------------

TEST(ValueConstraintsTest, ClvPictureNumberSecondsRangeIsZeroTo59) {
  // IEC 60856 §10.1.9: seconds 0–59.
  EXPECT_TRUE(ClvPictureNumberGenerator::IsValidClvPictureNumber(0, 0));
  EXPECT_TRUE(ClvPictureNumberGenerator::IsValidClvPictureNumber(59, 0));
  EXPECT_FALSE(ClvPictureNumberGenerator::IsValidClvPictureNumber(60, 0));
  EXPECT_FALSE(ClvPictureNumberGenerator::IsValidClvPictureNumber(-1, 0));
}

TEST(ValueConstraintsTest, ClvPictureNumberFrameRangeIsZeroTo29) {
  // IEC 60856 §10.1.9: frame within second 0–29.
  EXPECT_TRUE(ClvPictureNumberGenerator::IsValidClvPictureNumber(0, 0));
  EXPECT_TRUE(ClvPictureNumberGenerator::IsValidClvPictureNumber(0, 29));
  EXPECT_FALSE(ClvPictureNumberGenerator::IsValidClvPictureNumber(0, 30));
  EXPECT_FALSE(ClvPictureNumberGenerator::IsValidClvPictureNumber(0, -1));
}

TEST(ValueConstraintsTest, ClvPictureNumberX1NibbleRangeIsAtoF) {
  // IEC 60856 §10.1.9: X₁ encodes tens-of-seconds as offset from 0xA.
  // Seconds 0 → X₁=A, seconds 10 → X₁=B, ..., seconds 50 → X₁=F.
  // Verify that all valid seconds produce X₁ in [0xA, 0xF].
  for (int sec = 0; sec <= 59; ++sec) {
    const uint32_t code =
        ClvPictureNumberGenerator::EncodeClvPictureNumber(sec, 0);
    const uint8_t x1 = (code >> 16) & 0x0Fu;
    EXPECT_GE(x1, 0xAu) << "X₁ for seconds=" << sec << " is below 0xA";
    EXPECT_LE(x1, 0xFu) << "X₁ for seconds=" << sec << " is above 0xF";
  }
}

TEST(ValueConstraintsTest, ClvPictureNumberX3NibbleRangeIsZeroToNine) {
  // IEC 60856 §10.1.9: X₃ = seconds units (0–9).
  // Verify seconds 0-59 produce X₃ nibbles in [0, 9].
  for (int sec = 0; sec <= 59; ++sec) {
    const uint32_t code =
        ClvPictureNumberGenerator::EncodeClvPictureNumber(sec, 0);
    const uint8_t x3 = (code >> 8) & 0x0Fu;
    EXPECT_LE(x3, 9u) << "X₃ for seconds=" << sec << " exceeds 9";
  }
}

TEST(ValueConstraintsTest, ClvPictureNumberX4NibbleRangeIsZeroToTwo) {
  // IEC 60856 §10.1.9: X₄ = frame-within-second tens (0–2, since frame 0–29).
  for (int frame = 0; frame <= 29; ++frame) {
    const uint32_t code =
        ClvPictureNumberGenerator::EncodeClvPictureNumber(0, frame);
    const uint8_t x4 = (code >> 4) & 0x0Fu;
    EXPECT_LE(x4, 2u) << "X₄ for frame=" << frame << " exceeds 2";
  }
}

TEST(ValueConstraintsTest, ClvPictureNumberX5NibbleRangeIsZeroToNine) {
  // IEC 60856 §10.1.9: X₅ = frame-within-second units (0–9).
  for (int frame = 0; frame <= 29; ++frame) {
    const uint32_t code =
        ClvPictureNumberGenerator::EncodeClvPictureNumber(0, frame);
    const uint8_t x5 = (code >> 0) & 0x0Fu;
    EXPECT_LE(x5, 9u) << "X₅ for frame=" << frame << " exceeds 9";
  }
}

TEST(ValueConstraintsTest, ClvPictureNumberMaxSecondsConstantIs59) {
  EXPECT_EQ(ClvPictureNumberGenerator::kMaxSeconds, 59);
}

TEST(ValueConstraintsTest, ClvPictureNumberMaxFrameConstantIs29) {
  EXPECT_EQ(ClvPictureNumberGenerator::kMaxFrame, 29);
}

// ---------------------------------------------------------------------------
// ValueConstraintsTest — FM picture number (IEC 60857 Amendment 2 §10.2.3)
// ---------------------------------------------------------------------------

TEST(ValueConstraintsTest, FmPictureNumberRangeIsZeroTo79999) {
  // IEC 60857 Amendment 2 §10.2.3: NTSC FM max is 79,999.
  EXPECT_TRUE(FmPictureNumberGenerator::IsValidValue(0));
  EXPECT_TRUE(FmPictureNumberGenerator::IsValidValue(79999));
  EXPECT_FALSE(FmPictureNumberGenerator::IsValidValue(80000));
  EXPECT_FALSE(FmPictureNumberGenerator::IsValidValue(-1));
}

TEST(ValueConstraintsTest, FmPictureNumberNtscMaxConstantIs79999) {
  EXPECT_EQ(FmPictureNumberGenerator::kNtscMaxValue, 79999);
}

TEST(ValueConstraintsTest, FmPictureNumberEncodingProducesDecimalDigitNibbles) {
  // FM picture number nibbles x1–x5 must each be a decimal digit (0–9).
  for (int n : {0, 1, 12345, 79999}) {
    uint8_t x1, x2, x3, x4, x5;
    FmPictureNumberGenerator::EncodeValue(n, x1, x2, x3, x4, x5);
    EXPECT_LE(x1, 9u) << "x1 exceeds 9 for n=" << n;
    EXPECT_LE(x2, 9u) << "x2 exceeds 9 for n=" << n;
    EXPECT_LE(x3, 9u) << "x3 exceeds 9 for n=" << n;
    EXPECT_LE(x4, 9u) << "x4 exceeds 9 for n=" << n;
    EXPECT_LE(x5, 9u) << "x5 exceeds 9 for n=" << n;
  }
}

TEST(ValueConstraintsTest, FmPictureNumberEncodesZeroCorrectly) {
  uint8_t x1, x2, x3, x4, x5;
  FmPictureNumberGenerator::EncodeValue(0, x1, x2, x3, x4, x5);
  EXPECT_EQ(x1, 0u);
  EXPECT_EQ(x2, 0u);
  EXPECT_EQ(x3, 0u);
  EXPECT_EQ(x4, 0u);
  EXPECT_EQ(x5, 0u);
}

TEST(ValueConstraintsTest, FmPictureNumberEncodesRepresentativeValue) {
  // 79999 = 7×10000 + 9×1000 + 9×100 + 9×10 + 9×1
  uint8_t x1, x2, x3, x4, x5;
  FmPictureNumberGenerator::EncodeValue(79999, x1, x2, x3, x4, x5);
  EXPECT_EQ(x1, 7u);
  EXPECT_EQ(x2, 9u);
  EXPECT_EQ(x3, 9u);
  EXPECT_EQ(x4, 9u);
  EXPECT_EQ(x5, 9u);
}

// ---------------------------------------------------------------------------
// ValueConstraintsTest — frames-per-minute / frames-per-second (CLV timing)
// ---------------------------------------------------------------------------

TEST(ValueConstraintsTest, PalFramesPerMinuteIs1500) {
  // PAL 25 fps × 60 s = 1500 frames/minute.
  EXPECT_EQ(ProgrammeTimeCodeGenerator::FramesPerMinute(Standard::kPal), 1500);
}

TEST(ValueConstraintsTest, NtscFramesPerMinuteIs1800) {
  // NTSC 30 fps × 60 s = 1800 frames/minute.
  EXPECT_EQ(ProgrammeTimeCodeGenerator::FramesPerMinute(Standard::kNtsc), 1800);
}

TEST(ValueConstraintsTest, PalFramesPerSecondIs25) {
  EXPECT_EQ(ClvPictureNumberGenerator::FramesPerSecond(Standard::kPal), 25);
}

TEST(ValueConstraintsTest, NtscFramesPerSecondIs30) {
  EXPECT_EQ(ClvPictureNumberGenerator::FramesPerSecond(Standard::kNtsc), 30);
}

// ---------------------------------------------------------------------------
// ValueConstraintsTest — fixed code values match IEC constants
// ---------------------------------------------------------------------------

TEST(ValueConstraintsTest, LeadInCodeIs0x88FFFF) {
  // IEC 60856/60857 §10.1.1: lead-in code is 88FFFF.
  EXPECT_EQ(LeadInCodeGenerator::kCode, 0x88FFFFu);
}

TEST(ValueConstraintsTest, LeadOutCodeIs0x80EEEE) {
  // IEC 60856/60857 §10.1.2: lead-out code is 80EEEE.
  EXPECT_EQ(LeadOutCodeGenerator::kCode, 0x80EEEEu);
}

TEST(ValueConstraintsTest, PictureStopCodeIs0x82CFFF) {
  // IEC 60856 §10.1.4: picture stop code is 82CFFF.
  EXPECT_EQ(PictureStopCodeGenerator::kCode, 0x82CFFFu);
}

TEST(ValueConstraintsTest, ClvCodeIs0x87FFFF) {
  // IEC 60856 §10.1.8: CLV identifier code is 87FFFF.
  EXPECT_EQ(ClvCodeGenerator::kCode, 0x87FFFFu);
}

// ---------------------------------------------------------------------------
// ValueConstraintsTest — all fixed codes have valid key nibbles (bit 23 = 1)
// ---------------------------------------------------------------------------

TEST(ValueConstraintsTest, AllFixedCodesHaveKeyNibbleWithMsbOne) {
  // IEC requirement: bit 23 (MSB of first nibble) must be 1.
  EXPECT_TRUE((LeadInCodeGenerator::kCode & 0x800000u) != 0);
  EXPECT_TRUE((LeadOutCodeGenerator::kCode & 0x800000u) != 0);
  EXPECT_TRUE((PictureStopCodeGenerator::kCode & 0x800000u) != 0);
  EXPECT_TRUE((ClvCodeGenerator::kCode & 0x800000u) != 0);
}

// ---------------------------------------------------------------------------
// ValueConstraintsTest — FM programme time mode indicator constants
// ---------------------------------------------------------------------------

TEST(ValueConstraintsTest, FmProgrammeTimeModeConstantsMatchIecSpec) {
  // IEC 60857 Appendix G / §10.2: mode indicator X₅ values.
  EXPECT_EQ(FmProgrammeTimeGenerator::kModeLeadIn, 0xAu);
  EXPECT_EQ(FmProgrammeTimeGenerator::kModeTransition, 0xBu);
  EXPECT_EQ(FmProgrammeTimeGenerator::kModePicture, 0xDu);
  EXPECT_EQ(FmProgrammeTimeGenerator::kModeLeadOut, 0xCu);
}

TEST(ValueConstraintsTest, FmProgrammeTimeLeadInDataHasModeLeadIn) {
  FmProgrammeTimeGenerator gen;
  const FmData d = gen.CurrentData(true, SectionType::kLeadIn);
  EXPECT_EQ(d.x5, FmProgrammeTimeGenerator::kModeLeadIn);
}

TEST(ValueConstraintsTest, FmProgrammeTimeProgrammeAreaDataHasModePicture) {
  FmProgrammeTimeGenerator gen;
  const FmData d = gen.CurrentData(true, SectionType::kProgrammeArea);
  EXPECT_EQ(d.x5, FmProgrammeTimeGenerator::kModePicture);
}

TEST(ValueConstraintsTest, FmProgrammeTimeLeadOutDataHasModeLeadOut) {
  FmProgrammeTimeGenerator gen;
  // Advance into programme area to set a frozen value, then emit lead-out.
  gen.Advance(SectionType::kProgrammeArea);
  const FmData d = gen.CurrentData(true, SectionType::kLeadOut);
  EXPECT_EQ(d.x5, FmProgrammeTimeGenerator::kModeLeadOut);
}

}  // namespace
}  // namespace videosynth
