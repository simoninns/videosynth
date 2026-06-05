/*
 * File:        test_timecode_continuity.cpp
 * Module:      timecode_continuity_tests
 * Purpose:     Unit tests for timecode continuity across chapter boundaries.
 *              Verifies IEC 60856/60857 requirements:
 *              - Timecodes start at programme area beginning (§10.1.3, §10.1.7)
 *              - Timecodes run continuously without resetting (§10.1.5)
 *              - Chapter codes do NOT reset timecode counters (§10.1.5)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cstdint>

#include "videosynth/cav_code_generator.h"
#include "videosynth/clv_code_generator.h"
#include "videosynth/model.h"

namespace videosynth {
namespace {

// ---------------------------------------------------------------------------
// TimecodeContinuityTest — CAV picture number
// ---------------------------------------------------------------------------

TEST(TimecodeContinuityTest, CavPictureNumberStartsAtOneInProgrammeArea) {
  // IEC 60856/60857 §10.1.3: picture number begins at 1 in the programme area.
  CavPictureNumberGenerator gen(1, Standard::kPal);
  EXPECT_EQ(gen.current_value(), 1);
}

TEST(TimecodeContinuityTest, CavPictureNumberIncrementsEachFrame) {
  CavPictureNumberGenerator gen(1, Standard::kPal);
  gen.Advance();
  EXPECT_EQ(gen.current_value(), 2);
  gen.Advance();
  EXPECT_EQ(gen.current_value(), 3);
}

TEST(TimecodeContinuityTest, CavPictureNumberContinuouslyIncrementsOver1000Frames) {
  CavPictureNumberGenerator gen(1, Standard::kPal);
  for (int i = 0; i < 1000; ++i) {
    gen.Advance();
  }
  EXPECT_EQ(gen.current_value(), 1001);
}

TEST(TimecodeContinuityTest, CavPictureNumberIsIndependentOfChapterGenerator) {
  // IEC 60856/60857 §10.1.5: chapter codes do NOT reset timecode counters.
  CavPictureNumberGenerator pic_gen(1, Standard::kPal);
  ChapterNumberGenerator ch_gen(0);

  // Advance both for 500 frames (one chapter's worth of content).
  for (int i = 0; i < 500; ++i) {
    pic_gen.Advance();
    ch_gen.Advance();
  }

  // Reset chapter (simulate entering a new chapter) but NOT picture number.
  ChapterNumberGenerator ch_gen2(1, /*always_stop_bit_one=*/true);

  // Continue advancing picture number only — simulates timecode across chapter.
  for (int i = 0; i < 500; ++i) {
    pic_gen.Advance();
    ch_gen2.Advance();
  }

  // Picture number must be continuous: 1 (start) + 1000 advances = 1001.
  EXPECT_EQ(pic_gen.current_value(), 1001);
  // Chapter generator is independent and starts fresh at chapter 1.
  EXPECT_EQ(ChapterNumberGenerator::DecodeChapterNumber(ch_gen2.CurrentCode()),
            1);
}

TEST(TimecodeContinuityTest, CavPictureNumberResetRestoresStartValue) {
  CavPictureNumberGenerator gen(1, Standard::kPal);
  for (int i = 0; i < 100; ++i) {
    gen.Advance();
  }
  gen.Reset();
  EXPECT_EQ(gen.current_value(), 1);
}

TEST(TimecodeContinuityTest, CavPictureNumberSaturatesAtPalMax) {
  // IEC 60856 §10.1.3: PAL maximum is 99,999.
  CavPictureNumberGenerator gen(CavPictureNumberGenerator::kPalMaxValue,
                                Standard::kPal);
  gen.Advance();
  gen.Advance();
  EXPECT_EQ(gen.current_value(), CavPictureNumberGenerator::kPalMaxValue);
}

TEST(TimecodeContinuityTest, CavPictureNumberSaturatesAtNtscMax) {
  // IEC 60857 §10.1.3: NTSC maximum is 79,999.
  CavPictureNumberGenerator gen(CavPictureNumberGenerator::kNtscMaxValue,
                                Standard::kNtsc);
  gen.Advance();
  EXPECT_EQ(gen.current_value(), CavPictureNumberGenerator::kNtscMaxValue);
}

// ---------------------------------------------------------------------------
// TimecodeContinuityTest — CLV programme time code
// ---------------------------------------------------------------------------

TEST(TimecodeContinuityTest, ProgrammeTimeCodeStartsAtZeroZero) {
  // IEC 60856 §10.1.7: programme time code begins at 0:00.
  ProgrammeTimeCodeGenerator gen(0, 0, Standard::kPal);
  EXPECT_EQ(gen.current_hours(), 0);
  EXPECT_EQ(gen.current_minutes(), 0);
}

TEST(TimecodeContinuityTest, ProgrammeTimeCodePalIncrementsMinutesAt1500Frames) {
  // PAL: 25 fps × 60 s = 1500 frames per minute.
  ProgrammeTimeCodeGenerator gen(0, 0, Standard::kPal);
  for (int i = 0; i < 1500; ++i) {
    gen.Advance();
  }
  EXPECT_EQ(gen.current_hours(), 0);
  EXPECT_EQ(gen.current_minutes(), 1);
}

TEST(TimecodeContinuityTest, ProgrammeTimeCodeNtscIncrementsMinutesAt1800Frames) {
  // NTSC: 30 fps × 60 s = 1800 frames per minute.
  ProgrammeTimeCodeGenerator gen(0, 0, Standard::kNtsc);
  for (int i = 0; i < 1800; ++i) {
    gen.Advance();
  }
  EXPECT_EQ(gen.current_hours(), 0);
  EXPECT_EQ(gen.current_minutes(), 1);
}

TEST(TimecodeContinuityTest, ProgrammeTimeCodeContinuouslyAcrossChapters) {
  // IEC §10.1.5: time code does NOT reset at chapter boundaries.
  ProgrammeTimeCodeGenerator time_gen(0, 0, Standard::kPal);
  ChapterNumberGenerator ch_gen(0);

  // Advance 2000 frames (just over one minute at PAL).
  for (int i = 0; i < 2000; ++i) {
    time_gen.Advance();
    ch_gen.Advance();
  }

  // Change chapter (new ChapterNumberGenerator) — time code must not reset.
  ChapterNumberGenerator ch_gen2(1, /*always_stop_bit_one=*/true);
  for (int i = 0; i < 1500; ++i) {
    time_gen.Advance();
    ch_gen2.Advance();
  }

  // Time code must reflect 3500 total frames = 2 min 20 sec worth at 25fps.
  // 3500 frames / 1500 frames/min = 2 min remainder 500 frames.
  EXPECT_EQ(time_gen.current_hours(), 0);
  EXPECT_EQ(time_gen.current_minutes(), 2);
}

TEST(TimecodeContinuityTest, ProgrammeTimeCodeResetRestoresStart) {
  ProgrammeTimeCodeGenerator gen(1, 30, Standard::kPal);
  for (int i = 0; i < 3000; ++i) {
    gen.Advance();
  }
  gen.Reset();
  EXPECT_EQ(gen.current_hours(), 1);
  EXPECT_EQ(gen.current_minutes(), 30);
  EXPECT_EQ(gen.total_frames(), 0);
}

// ---------------------------------------------------------------------------
// TimecodeContinuityTest — CLV picture number
// ---------------------------------------------------------------------------

TEST(TimecodeContinuityTest, ClvPictureNumberStartsAtZeroZero) {
  // IEC 60856 §10.1.9: CLV picture number starts at seconds=0, frame=0.
  ClvPictureNumberGenerator gen(Standard::kPal);
  EXPECT_EQ(gen.current_seconds(), 0);
  EXPECT_EQ(gen.current_frame(), 0);
}

TEST(TimecodeContinuityTest, ClvPictureNumberPalAdvancesFramePerAdvance) {
  ClvPictureNumberGenerator gen(Standard::kPal);
  gen.Advance();
  EXPECT_EQ(gen.current_frame(), 1);
}

TEST(TimecodeContinuityTest, ClvPictureNumberPalIncrementsSecondsAt25Frames) {
  // PAL: 25 fps.
  ClvPictureNumberGenerator gen(Standard::kPal);
  for (int i = 0; i < 25; ++i) {
    gen.Advance();
  }
  EXPECT_EQ(gen.current_seconds(), 1);
  EXPECT_EQ(gen.current_frame(), 0);
}

TEST(TimecodeContinuityTest, ClvPictureNumberContinuouslyAcrossChapters) {
  // IEC §10.1.5: CLV picture number does NOT reset at chapter boundaries.
  ClvPictureNumberGenerator pic_gen(Standard::kPal);
  ChapterNumberGenerator ch_gen(0);

  // Advance 500 frames (20 seconds at PAL 25fps).
  for (int i = 0; i < 500; ++i) {
    pic_gen.Advance();
    ch_gen.Advance();
  }

  // Switch chapter — CLV picture number must continue.
  ChapterNumberGenerator ch_gen2(1, /*always_stop_bit_one=*/true);
  for (int i = 0; i < 250; ++i) {
    pic_gen.Advance();
    ch_gen2.Advance();
  }

  // Total 750 frames = 30 seconds at 25fps.
  EXPECT_EQ(pic_gen.total_frames(), 750);
  EXPECT_EQ(pic_gen.current_seconds(), 30);
}

TEST(TimecodeContinuityTest, ClvPictureNumberResetRestoresInitialState) {
  ClvPictureNumberGenerator gen(Standard::kPal);
  for (int i = 0; i < 100; ++i) {
    gen.Advance();
  }
  gen.Reset();
  EXPECT_EQ(gen.current_seconds(), 0);
  EXPECT_EQ(gen.current_frame(), 0);
  EXPECT_EQ(gen.total_frames(), 0);
}

TEST(TimecodeContinuityTest, MultipleCodeGeneratorsAreIndependent) {
  // Demonstrate that multiple independent generators can run in parallel
  // without interfering with each other — as required by IEC §10.1.5.
  CavPictureNumberGenerator pic_gen(1, Standard::kPal);
  ChapterNumberGenerator ch_gen(0);
  ProgrammeTimeCodeGenerator time_gen(0, 0, Standard::kPal);

  for (int i = 0; i < 1500; ++i) {
    pic_gen.Advance();
    ch_gen.Advance();
    time_gen.Advance();
  }

  EXPECT_EQ(pic_gen.current_value(), 1501);
  EXPECT_EQ(time_gen.current_minutes(), 1);
  // ChapterNumberGenerator reached stop-bit transition at track 400.
  EXPECT_TRUE(ChapterNumberGenerator::DecodeStopBit(ch_gen.CurrentCode()));
}

}  // namespace
}  // namespace videosynth
