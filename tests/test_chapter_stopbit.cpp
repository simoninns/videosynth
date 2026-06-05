/*
 * File:        test_chapter_stopbit.cpp
 * Module:      chapter_stopbit_tests
 * Purpose:     Unit tests for chapter stop-bit state tracking.
 *              Verifies IEC 60856/60857 §10.1.5 chapter stop-bit rules:
 *              - Stop-bit = 0 for first 400 tracks
 *              - Stop-bit = 1 for all subsequent tracks
 *              - First chapter after lead-in always has stop-bit = 1
 *              - Short chapters (< 800 tracks) always use stop-bit = 1
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cstdint>

#include "videosynth/cav_code_generator.h"

namespace videosynth {
namespace {

// ---------------------------------------------------------------------------
// ChapterStopBitTest — normal chapter (stop-bit 0 then 1)
// ---------------------------------------------------------------------------

TEST(ChapterStopBitTest, InitialStopBitIsZeroForNormalChapter) {
  // IEC 60856/60857 §10.1.5: stop-bit = 0 for first 400 tracks.
  ChapterNumberGenerator gen(0, /*always_stop_bit_one=*/false);
  EXPECT_FALSE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()));
}

TEST(ChapterStopBitTest, StopBitRemainsZeroUpToTrack399) {
  ChapterNumberGenerator gen(0, /*always_stop_bit_one=*/false);
  for (int i = 0; i < 399; ++i) {
    gen.Advance();
    EXPECT_FALSE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()))
        << "Expected stop-bit = 0 at track " << (i + 1);
  }
}

TEST(ChapterStopBitTest, StopBitBecomesOneAtTrack400) {
  // IEC 60856/60857 §10.1.5: stop-bit transitions to 1 at the 400th track.
  ChapterNumberGenerator gen(0, /*always_stop_bit_one=*/false);
  for (int i = 0; i < 400; ++i) {
    gen.Advance();
  }
  EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()));
}

TEST(ChapterStopBitTest, StopBitRemainsOneAfterTrack400) {
  ChapterNumberGenerator gen(0, /*always_stop_bit_one=*/false);
  for (int i = 0; i < 500; ++i) {
    gen.Advance();
  }
  for (int i = 500; i < 600; ++i) {
    gen.Advance();
    EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()))
        << "Expected stop-bit = 1 at track " << (i + 1);
  }
}

TEST(ChapterStopBitTest, TrackCountBoundaryIsExactly400) {
  // Verify the exact transition: track 399 = 0, track 400 = 1.
  ChapterNumberGenerator gen(0, /*always_stop_bit_one=*/false);

  // Advance to just before the transition.
  for (int i = 0; i < 399; ++i) {
    gen.Advance();
  }
  EXPECT_FALSE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()))
      << "Track 399: expected stop-bit = 0";

  gen.Advance();
  EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()))
      << "Track 400: expected stop-bit = 1";
}

// ---------------------------------------------------------------------------
// ChapterStopBitTest — always_stop_bit_one flag
// ---------------------------------------------------------------------------

TEST(ChapterStopBitTest, AlwaysStopBitOneStartsWithStopBitOne) {
  // IEC §10.1.5: first chapter after lead-in MUST have stop-bit = 1.
  ChapterNumberGenerator gen(0, /*always_stop_bit_one=*/true);
  EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()));
}

TEST(ChapterStopBitTest, AlwaysStopBitOneRemainsOneBeforeTrack400) {
  ChapterNumberGenerator gen(0, /*always_stop_bit_one=*/true);
  for (int i = 0; i < 300; ++i) {
    gen.Advance();
    EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()))
        << "Expected stop-bit = 1 at track " << (i + 1);
  }
}

TEST(ChapterStopBitTest, AlwaysStopBitOneRemainsOneAfterTrack400) {
  ChapterNumberGenerator gen(0, /*always_stop_bit_one=*/true);
  for (int i = 0; i < 600; ++i) {
    gen.Advance();
    EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()))
        << "Expected stop-bit = 1 at track " << (i + 1);
  }
}

// ---------------------------------------------------------------------------
// ChapterStopBitTest — Reset() restores state
// ---------------------------------------------------------------------------

TEST(ChapterStopBitTest, ResetRestoresStopBitToZero) {
  ChapterNumberGenerator gen(0, /*always_stop_bit_one=*/false);
  // Advance past the transition point.
  for (int i = 0; i < 500; ++i) {
    gen.Advance();
  }
  EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()));

  gen.Reset();
  EXPECT_FALSE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()));
}

TEST(ChapterStopBitTest, ResetDoesNotChangeAlwaysStopBitOneFlag) {
  ChapterNumberGenerator gen(0, /*always_stop_bit_one=*/true);
  for (int i = 0; i < 500; ++i) {
    gen.Advance();
  }
  gen.Reset();
  // With always_stop_bit_one=true, stop-bit remains 1 after Reset().
  EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()));
}

// ---------------------------------------------------------------------------
// ChapterStopBitTest — encoding/decoding consistency
// ---------------------------------------------------------------------------

TEST(ChapterStopBitTest, EncodeDecodeChapterNumberRoundtrip) {
  for (int chapter = 0; chapter <= ChapterNumberGenerator::kMaxChapterNumber;
       chapter += 5) {
    const uint32_t code_with_0 =
        ChapterNumberGenerator::EncodeChapterCode(chapter, false);
    const uint32_t code_with_1 =
        ChapterNumberGenerator::EncodeChapterCode(chapter, true);

    EXPECT_EQ(ChapterNumberGenerator::DecodeChapterNumber(code_with_0), chapter)
        << "Chapter " << chapter << " with stop-bit 0";
    EXPECT_EQ(ChapterNumberGenerator::DecodeChapterNumber(code_with_1), chapter)
        << "Chapter " << chapter << " with stop-bit 1";
    EXPECT_FALSE(ChapterNumberGenerator::DecodeStopBit(code_with_0))
        << "Chapter " << chapter << ": expected stop-bit 0";
    EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(code_with_1))
        << "Chapter " << chapter << ": expected stop-bit 1";
  }
}

TEST(ChapterStopBitTest, ChapterCodeKeyNibbleIsAlwaysValid) {
  // IEC 60856/60857: bit 23 of the 24-bit code must be 1.
  ChapterNumberGenerator gen(0, false);
  for (int i = 0; i < 600; ++i) {
    EXPECT_NE(gen.CurrentCode() & 0x800000u, 0u)
        << "Key nibble invalid at track " << i;
    gen.Advance();
  }
}

TEST(ChapterStopBitTest, StopBitTransitionConstantIs400) {
  EXPECT_EQ(ChapterNumberGenerator::kStopBitTransitionTrack, 400);
}

TEST(ChapterStopBitTest, MinimumChapterTracksConstantIs30) {
  EXPECT_EQ(ChapterNumberGenerator::kMinimumChapterTracks, 30);
}

TEST(ChapterStopBitTest, MaxChapterNumberConstantIs79) {
  EXPECT_EQ(ChapterNumberGenerator::kMaxChapterNumber, 79);
}

// ---------------------------------------------------------------------------
// ChapterStopBitTest — multi-chapter state tracking
// ---------------------------------------------------------------------------

TEST(ChapterStopBitTest, TwoConsecutiveChaptersHaveIndependentStopBitCounters) {
  // The first chapter (always_stop_bit_one=true, per IEC) starts with 1.
  ChapterNumberGenerator first_ch(0, /*always_stop_bit_one=*/true);
  for (int i = 0; i < 600; ++i) {
    first_ch.Advance();
  }
  // First chapter: stop-bit always 1.
  EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(first_ch.CurrentCode()));

  // Second chapter: starts a fresh counter with stop-bit = 0 initially.
  ChapterNumberGenerator second_ch(1, /*always_stop_bit_one=*/false);
  EXPECT_FALSE(
      ChapterNumberGenerator::DecodeStopBit(second_ch.CurrentCode()));

  // After 400 tracks, second chapter transitions to stop-bit = 1.
  for (int i = 0; i < 400; ++i) {
    second_ch.Advance();
  }
  EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(second_ch.CurrentCode()));
}

TEST(ChapterStopBitTest, ChapterNumberIsPreservedAcrossStopBitTransition) {
  constexpr int kChapter = 42;
  ChapterNumberGenerator gen(kChapter, /*always_stop_bit_one=*/false);

  EXPECT_EQ(ChapterNumberGenerator::DecodeChapterNumber(gen.CurrentCode()),
            kChapter);

  for (int i = 0; i < 500; ++i) {
    gen.Advance();
  }

  EXPECT_EQ(ChapterNumberGenerator::DecodeChapterNumber(gen.CurrentCode()),
            kChapter);
  EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(gen.CurrentCode()));
}

}  // namespace
}  // namespace videosynth
