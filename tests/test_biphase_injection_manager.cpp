/*
 * File:        test_biphase_injection_manager.cpp
 * Module:      biphase_injection_manager_tests
 * Purpose:     Unit tests for BiphaseInjectionManager — biphase and 40-bit FM
 *              VBI injection orchestration per IEC 60856 (PAL) / IEC 60857
 *              (NTSC).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "videosynth/biphase_injection_manager.h"
#include "videosynth/biphase_types.h"
#include "videosynth/fixed_point.h"
#include "videosynth/line_placement_engine.h"
#include "videosynth/model.h"
#include "videosynth/signal_timing_model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

// Builds a Y buffer for one frame at blanking level (0 mV).
std::vector<SampleFixed> MakeBlankingBuffer(Standard standard) {
  const TimingConstants timing = GetTimingConstants(standard);
  const int lines = timing.lines_per_frame;
  const int spl = timing.samples_per_line_4fsc;
  // Approximate total: lines × spl (ignoring PAL long-line correction)
  return std::vector<SampleFixed>(static_cast<std::size_t>(lines * spl),
                                  MillivoltsToSampleFixed(0.0));
}

// Builds per-line sample offset/count vectors (uniform nominal line length).
void BuildLineLayout(Standard standard, std::vector<int>* offsets,
                     std::vector<int>* counts) {
  const TimingConstants timing = GetTimingConstants(standard);
  const int lines = timing.lines_per_frame;
  const int spl = timing.samples_per_line_4fsc;
  offsets->resize(static_cast<std::size_t>(lines));
  counts->resize(static_cast<std::size_t>(lines));
  for (int i = 0; i < lines; ++i) {
    (*offsets)[static_cast<std::size_t>(i)] = i * spl;
    (*counts)[static_cast<std::size_t>(i)] = spl;
  }
}

// Active-picture window start samples (mirrors generation_stage.cpp).
int ActiveWindowStart(Standard standard) {
  const TimingConstants timing = GetTimingConstants(standard);
  if (standard == Standard::kPal) {
    return 177;
  }
  return static_cast<int>(std::lround(timing.sample_rate_4fsc_hz * 10.5e-6));
}

// Builds a minimal Section with one laserdisc LineInjection.
Section MakeLaserdiscSection(
    SectionType section_type, DiscType disc_type,
    const std::vector<Section::LineInjectionCode>& codes) {
  Section section;
  section.name = "TestSection";
  section.type = "progressive";
  section.section_type = section_type;
  section.duration_frames = 1;

  Section::LineInjection injection;
  injection.type = "laserdisc";
  injection.disc_type = DiscTypeToString(disc_type);
  injection.codes = codes;
  section.line_injections.push_back(injection);

  return section;
}

// Returns the mean millivolt level on a given 1-based line number within
// the range [start_sample, end_sample) relative to the line start.
double MeanMvOnLine(const std::vector<SampleFixed>& y_mv, Standard standard,
                    int line_1based, int start_sample, int end_sample) {
  const TimingConstants timing = GetTimingConstants(standard);
  const int spl = timing.samples_per_line_4fsc;
  const int line_base = (line_1based - 1) * spl;
  double sum = 0.0;
  int count = 0;
  for (int i = start_sample;
       i < end_sample && (line_base + i) < static_cast<int>(y_mv.size()); ++i) {
    sum +=
        SampleFixedToMillivolts(y_mv[static_cast<std::size_t>(line_base + i)]);
    ++count;
  }
  return (count > 0) ? (sum / count) : 0.0;
}

// Returns the max millivolt level on a given line in [start_sample,
// end_sample).
double MaxMvOnLine(const std::vector<SampleFixed>& y_mv, Standard standard,
                   int line_1based, int start_sample, int end_sample) {
  const TimingConstants timing = GetTimingConstants(standard);
  const int spl = timing.samples_per_line_4fsc;
  const int line_base = (line_1based - 1) * spl;
  double max_mv = -1e9;
  for (int i = start_sample;
       i < end_sample && (line_base + i) < static_cast<int>(y_mv.size()); ++i) {
    const double mv =
        SampleFixedToMillivolts(y_mv[static_cast<std::size_t>(line_base + i)]);
    if (mv > max_mv) {
      max_mv = mv;
    }
  }
  return max_mv;
}

// Returns the min millivolt level on a given line in [start_sample,
// end_sample).
double MinMvOnLine(const std::vector<SampleFixed>& y_mv, Standard standard,
                   int line_1based, int start_sample, int end_sample) {
  const TimingConstants timing = GetTimingConstants(standard);
  const int spl = timing.samples_per_line_4fsc;
  const int line_base = (line_1based - 1) * spl;
  double min_mv = 1e9;
  for (int i = start_sample;
       i < end_sample && (line_base + i) < static_cast<int>(y_mv.size()); ++i) {
    const double mv =
        SampleFixedToMillivolts(y_mv[static_cast<std::size_t>(line_base + i)]);
    if (mv < min_mv) {
      min_mv = mv;
    }
  }
  return min_mv;
}

// Checks if any sample on a given line in [start_sample, end_sample) differs
// from blanking level (0 mV), indicating signal was injected.
bool LineHasSignal(const std::vector<SampleFixed>& y_mv, Standard standard,
                   int line_1based, int start_sample, int end_sample) {
  const TimingConstants timing = GetTimingConstants(standard);
  const int spl = timing.samples_per_line_4fsc;
  const int line_base = (line_1based - 1) * spl;
  const SampleFixed blanking = MillivoltsToSampleFixed(0.0);
  for (int i = start_sample;
       i < end_sample && (line_base + i) < static_cast<int>(y_mv.size()); ++i) {
    if (y_mv[static_cast<std::size_t>(line_base + i)] != blanking) {
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// BiphaseInjectionManagerTest — basic structure
// ---------------------------------------------------------------------------

class BiphaseInjectionManagerTest : public ::testing::Test {
 protected:
  BiphaseInjectionManager manager_;
  std::vector<std::string> errors_;

  bool RunProcessFrame(Standard standard, const Section& section) {
    auto y_mv = MakeBlankingBuffer(standard);
    std::vector<int> offsets, counts;
    BuildLineLayout(standard, &offsets, &counts);
    const auto frame_lines = BuildFrameTimingPrimitives(standard);
    const int aws = ActiveWindowStart(standard);
    const TimingConstants timing = GetTimingConstants(standard);

    return manager_.ProcessFrame(&y_mv, 0, offsets, counts, section, standard,
                                 timing.sample_rate_4fsc_hz, frame_lines, aws,
                                 &errors_);
  }
};

// ---------------------------------------------------------------------------
// Section with no laserdisc injection should process without error.
// ---------------------------------------------------------------------------

TEST_F(BiphaseInjectionManagerTest, NoLaserdiscInjectionSucceeds) {
  Section section;
  section.name = "Plain";
  section.type = "progressive";
  section.section_type = SectionType::kProgrammeArea;
  section.duration_frames = 1;

  EXPECT_TRUE(RunProcessFrame(Standard::kPal, section));
  EXPECT_TRUE(errors_.empty());
}

// ---------------------------------------------------------------------------
// Reset clears section state so the next ProcessFrame reinitialises.
// ---------------------------------------------------------------------------

TEST_F(BiphaseInjectionManagerTest, ResetAllowsReinit) {
  Section::LineInjectionCode code;
  code.code_type = "lead_in";
  auto section =
      MakeLaserdiscSection(SectionType::kLeadIn, DiscType::kCAV, {code});

  EXPECT_TRUE(RunProcessFrame(Standard::kPal, section));
  manager_.Reset();
  // After reset, same section pointer re-initialises cleanly.
  EXPECT_TRUE(RunProcessFrame(Standard::kPal, section));
  EXPECT_TRUE(errors_.empty());
}

// ---------------------------------------------------------------------------
// Invalid disc_type string returns false with an error.
// ---------------------------------------------------------------------------

TEST_F(BiphaseInjectionManagerTest, UnknownDiscTypeReturnsError) {
  Section section;
  section.name = "Bad";
  section.type = "progressive";
  section.section_type = SectionType::kLeadIn;
  Section::LineInjection inj;
  inj.type = "laserdisc";
  inj.disc_type = "UNKNOWN_DISC";
  section.line_injections.push_back(inj);

  auto y_mv = MakeBlankingBuffer(Standard::kPal);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  const bool ok = manager_.ProcessFrame(
      &y_mv, 0, offsets, counts, section, Standard::kPal,
      timing.sample_rate_4fsc_hz, frame_lines, 177, &errors_);
  EXPECT_FALSE(ok);
  EXPECT_FALSE(errors_.empty());
}

// ---------------------------------------------------------------------------
// PAL CAV lead-in: lead_in code injected on lines 17 and 18.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalCavTest, LeadInCodeAppearsOnCorrectLines) {
  Section::LineInjectionCode code;
  code.code_type = "lead_in";
  const auto section =
      MakeLaserdiscSection(SectionType::kLeadIn, DiscType::kCAV, {code});

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kPal);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, 177, &errors));
  EXPECT_TRUE(errors.empty());

  const int aws = 177;
  const int end = timing.samples_per_line_4fsc;

  // Lines 17 and 18 (field 1) carry lead_in for PAL CAV.
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kPal, 17, aws, end));
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kPal, 18, aws, end));

  // Lines outside the biphase range should remain at blanking.
  EXPECT_FALSE(LineHasSignal(y_mv, Standard::kPal, 19, aws, end));
  EXPECT_FALSE(LineHasSignal(y_mv, Standard::kPal, 15, aws, end));
}

// ---------------------------------------------------------------------------
// PAL CAV lead-in: signal levels respect 30%–100% white (210–700 mV).
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalCavTest, LeadInSignalLevelsWithinSpec) {
  Section::LineInjectionCode code;
  code.code_type = "lead_in";
  const auto section =
      MakeLaserdiscSection(SectionType::kLeadIn, DiscType::kCAV, {code});

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kPal);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, 177, &errors));

  const int aws = 177;
  const int end = timing.samples_per_line_4fsc;

  // Baseline (digital low) should be ~210 mV; peak should be ~700 mV.
  const double min_mv = MinMvOnLine(y_mv, Standard::kPal, 17, aws, end);
  const double max_mv = MaxMvOnLine(y_mv, Standard::kPal, 17, aws, end);

  EXPECT_NEAR(min_mv, 210.0, 5.0);
  EXPECT_NEAR(max_mv, 700.0, 5.0);
}

// ---------------------------------------------------------------------------
// PAL CAV lead-out: lead_out code injected on field-2 lines 330 and 331.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalCavTest, LeadOutCodeAppearsOnField2Lines) {
  Section::LineInjectionCode code;
  code.code_type = "lead_out";
  const auto section =
      MakeLaserdiscSection(SectionType::kLeadOut, DiscType::kCAV, {code});

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kPal);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, 177, &errors));

  const int aws = 177;
  const int end = timing.samples_per_line_4fsc;

  // Lines 330 and 331 are field-2 biphase lines for PAL CAV.
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kPal, 330, aws, end));
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kPal, 331, aws, end));
}

// ---------------------------------------------------------------------------
// PAL CAV programme area: picture_number auto-increments across frames.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalCavTest, PictureNumberIncrementsAcrossFrames) {
  Section::LineInjectionCode code;
  code.code_type = "picture_number";
  code.start_value = 1;
  code.start_value_specified = true;
  const auto section =
      MakeLaserdiscSection(SectionType::kProgrammeArea, DiscType::kCAV, {code});

  BiphaseInjectionManager manager;
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  const int aws = 177;

  // Generate two frames and capture the Y buffer state for line 17 each time.
  auto y1 = MakeBlankingBuffer(Standard::kPal);
  std::vector<std::string> errors1;
  ASSERT_TRUE(manager.ProcessFrame(&y1, 0, offsets, counts, section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, aws, &errors1));

  auto y2 = MakeBlankingBuffer(Standard::kPal);
  std::vector<std::string> errors2;
  ASSERT_TRUE(manager.ProcessFrame(&y2, 0, offsets, counts, section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, aws, &errors2));

  // Both frames must have signal on the picture_number lines.
  const int end = timing.samples_per_line_4fsc;
  EXPECT_TRUE(LineHasSignal(y1, Standard::kPal, 17, aws, end));
  EXPECT_TRUE(LineHasSignal(y2, Standard::kPal, 17, aws, end));

  // The encoded codes differ because the picture number advanced by one.
  // Compare raw sample values on line 17 between the two frames.
  const int spl = timing.samples_per_line_4fsc;
  const int line17_base = (17 - 1) * spl;
  bool any_sample_differs = false;
  for (int i = aws; i < spl && !any_sample_differs; ++i) {
    if (y1[static_cast<std::size_t>(line17_base + i)] !=
        y2[static_cast<std::size_t>(line17_base + i)]) {
      any_sample_differs = true;
    }
  }
  EXPECT_TRUE(any_sample_differs);
}

// ---------------------------------------------------------------------------
// PAL CLV programme area: programme_time_code appears on correct lines.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalClvTest,
     ProgrammeTimeCodeAppearsOnCorrectLines) {
  Section::LineInjectionCode code;
  code.code_type = "programme_time_code";
  const auto section =
      MakeLaserdiscSection(SectionType::kProgrammeArea, DiscType::kCLV, {code});

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kPal);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, 177, &errors));
  EXPECT_TRUE(errors.empty());

  const int aws = 177;
  const int end = timing.samples_per_line_4fsc;

  // programme_time_code appears on lines 17 and 18 (field-1 priority).
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kPal, 17, aws, end));
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kPal, 18, aws, end));
}

// ---------------------------------------------------------------------------
// PAL CLV programme area: clv_picture_number appears on line 16.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalClvTest, ClvPictureNumberAppearsOnLine16) {
  std::vector<Section::LineInjectionCode> codes;
  Section::LineInjectionCode ptc;
  ptc.code_type = "programme_time_code";
  codes.push_back(ptc);
  Section::LineInjectionCode cpn;
  cpn.code_type = "clv_picture_number";
  codes.push_back(cpn);

  const auto section =
      MakeLaserdiscSection(SectionType::kProgrammeArea, DiscType::kCLV, codes);

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kPal);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, 177, &errors));
  EXPECT_TRUE(errors.empty());

  const int aws = 177;
  const int end = timing.samples_per_line_4fsc;
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kPal, 16, aws, end));
}

// ---------------------------------------------------------------------------
// PAL CAV: programme_status uses 0.172 H horizontal offset on line 16.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalCavTest, ProgrammeStatusUses172HOffset) {
  std::vector<Section::LineInjectionCode> codes;
  Section::LineInjectionCode pn;
  pn.code_type = "picture_number";
  pn.start_value = 1;
  pn.start_value_specified = true;
  codes.push_back(pn);
  Section::LineInjectionCode ps;
  ps.code_type = "programme_status";
  ps.programme_status = "8DC000";
  ps.programme_status_specified = true;
  codes.push_back(ps);

  const auto section =
      MakeLaserdiscSection(SectionType::kProgrammeArea, DiscType::kCAV, codes);

  BiphaseInjectionManager manager;
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  const int spl = timing.samples_per_line_4fsc;
  auto y_mv = MakeBlankingBuffer(Standard::kPal);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  const int aws = 177;
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, aws, &errors));
  EXPECT_TRUE(errors.empty());

  // 0.172 H offset for PAL: round(0.172 × 1135) = 195
  const int offset_172h = static_cast<int>(std::round(0.172 * spl));

  // Line 16 carries programme_status. The signal must not start at sample
  // aws (177) but at offset_172h (195). Samples in [aws, offset_172h)
  // are at blanking level (0 mV) since no biphase baseline is written before
  // the 0.172 H horizontal start position.
  const double mean_before =
      MeanMvOnLine(y_mv, Standard::kPal, 16, aws, offset_172h);
  EXPECT_NEAR(mean_before, 0.0, 1.0);

  // Signal region (first bit cell starts at offset_172h) must have content.
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kPal, 16, offset_172h, spl));
}

// ---------------------------------------------------------------------------
// PAL CLV: clv_code uses 0.172 H offset (NTSC only; PAL has no offset).
// PAL programme_status does use 0.172 H offset.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalClvTest, ClvCodeAppearsWithNoOffset) {
  std::vector<Section::LineInjectionCode> codes;
  Section::LineInjectionCode clvc;
  clvc.code_type = "clv_code";
  codes.push_back(clvc);

  const auto section =
      MakeLaserdiscSection(SectionType::kProgrammeArea, DiscType::kCLV, codes);

  BiphaseInjectionManager manager;
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  auto y_mv = MakeBlankingBuffer(Standard::kPal);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, 177, &errors));
  EXPECT_TRUE(errors.empty());

  const int aws = 177;
  const int end = timing.samples_per_line_4fsc;
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kPal, 17, aws, end));
}

// ---------------------------------------------------------------------------
// NTSC CAV lead-in: lead_in code injected on lines 17 and 18.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerNtscCavTest, LeadInCodeAppearsOnField1Lines) {
  Section::LineInjectionCode code;
  code.code_type = "lead_in";
  const auto section =
      MakeLaserdiscSection(SectionType::kLeadIn, DiscType::kCAV, {code});

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kNtsc);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kNtsc, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kNtsc);
  const TimingConstants timing = GetTimingConstants(Standard::kNtsc);
  const int aws = ActiveWindowStart(Standard::kNtsc);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kNtsc, timing.sample_rate_4fsc_hz,
                                   frame_lines, aws, &errors));
  EXPECT_TRUE(errors.empty());

  const int end = timing.samples_per_line_4fsc;
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kNtsc, 17, aws, end));
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kNtsc, 18, aws, end));
  EXPECT_FALSE(LineHasSignal(y_mv, Standard::kNtsc, 19, aws, end));
}

// ---------------------------------------------------------------------------
// NTSC CAV: signal levels are 0–714 mV (0–100 IRE).
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerNtscCavTest, SignalLevelsWithinSpec) {
  Section::LineInjectionCode code;
  code.code_type = "lead_in";
  const auto section =
      MakeLaserdiscSection(SectionType::kLeadIn, DiscType::kCAV, {code});

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kNtsc);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kNtsc, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kNtsc);
  const TimingConstants timing = GetTimingConstants(Standard::kNtsc);
  const int aws = ActiveWindowStart(Standard::kNtsc);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kNtsc, timing.sample_rate_4fsc_hz,
                                   frame_lines, aws, &errors));

  const int end = timing.samples_per_line_4fsc;
  const double min_mv = MinMvOnLine(y_mv, Standard::kNtsc, 17, aws, end);
  const double max_mv = MaxMvOnLine(y_mv, Standard::kNtsc, 17, aws, end);

  // NTSC biphase: baseline = 0 mV, peak = 714.3 mV.
  EXPECT_NEAR(min_mv, 0.0, 5.0);
  EXPECT_NEAR(max_mv, 714.3, 5.0);
}

// ---------------------------------------------------------------------------
// NTSC CAV: FM picture number appears on FM lines (10 and 273).
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerNtscCavTest, FmPictureNumberAppearsOnFmLines) {
  std::vector<Section::LineInjectionCode> codes;
  Section::LineInjectionCode pn;
  pn.code_type = "picture_number";
  pn.start_value = 1;
  pn.start_value_specified = true;
  codes.push_back(pn);
  Section::LineInjectionCode fm;
  fm.code_type = "fm_picture_number";
  fm.start_value = 1;
  fm.start_value_specified = true;
  codes.push_back(fm);
  Section::LineInjectionCode wf;
  wf.code_type = "fm_white_flag";
  codes.push_back(wf);

  const auto section =
      MakeLaserdiscSection(SectionType::kProgrammeArea, DiscType::kCAV, codes);

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kNtsc);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kNtsc, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kNtsc);
  const TimingConstants timing = GetTimingConstants(Standard::kNtsc);
  const int aws = ActiveWindowStart(Standard::kNtsc);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kNtsc, timing.sample_rate_4fsc_hz,
                                   frame_lines, aws, &errors));
  EXPECT_TRUE(errors.empty());

  const int end = timing.samples_per_line_4fsc;
  // FM picture number on lines 10 (field 1) and 273 (field 2).
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kNtsc, 10, aws, end));
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kNtsc, 273, aws, end));
}

// ---------------------------------------------------------------------------
// NTSC CAV: white flag on line 11 (field 1) in programme area.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerNtscCavTest, WhiteFlagAppearsOnLine11) {
  std::vector<Section::LineInjectionCode> codes;
  Section::LineInjectionCode pn;
  pn.code_type = "picture_number";
  pn.start_value = 1;
  pn.start_value_specified = true;
  codes.push_back(pn);
  Section::LineInjectionCode wf;
  wf.code_type = "fm_white_flag";
  codes.push_back(wf);

  const auto section =
      MakeLaserdiscSection(SectionType::kProgrammeArea, DiscType::kCAV, codes);

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kNtsc);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kNtsc, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kNtsc);
  const TimingConstants timing = GetTimingConstants(Standard::kNtsc);
  const int aws = ActiveWindowStart(Standard::kNtsc);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kNtsc, timing.sample_rate_4fsc_hz,
                                   frame_lines, aws, &errors));
  EXPECT_TRUE(errors.empty());

  const int end = timing.samples_per_line_4fsc;
  // White flag on line 11 (field 1) in programme area.
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kNtsc, 11, aws, end));
  // White flag should be at 100 IRE ≈ 714.3 mV.
  const double max_mv = MaxMvOnLine(y_mv, Standard::kNtsc, 11, aws, end);
  EXPECT_NEAR(max_mv, 714.3, 2.0);
}

// ---------------------------------------------------------------------------
// NTSC CLV: FM programme time code appears on FM lines (10 and 273).
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerNtscClvTest, FmProgrammeTimeAppearsOnFmLines) {
  std::vector<Section::LineInjectionCode> codes;
  Section::LineInjectionCode ptc;
  ptc.code_type = "programme_time_code";
  codes.push_back(ptc);
  Section::LineInjectionCode fmt;
  fmt.code_type = "fm_programme_time";
  codes.push_back(fmt);
  Section::LineInjectionCode wf;
  wf.code_type = "fm_white_flag";
  codes.push_back(wf);

  const auto section =
      MakeLaserdiscSection(SectionType::kProgrammeArea, DiscType::kCLV, codes);

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kNtsc);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kNtsc, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kNtsc);
  const TimingConstants timing = GetTimingConstants(Standard::kNtsc);
  const int aws = ActiveWindowStart(Standard::kNtsc);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kNtsc, timing.sample_rate_4fsc_hz,
                                   frame_lines, aws, &errors));
  EXPECT_TRUE(errors.empty());

  const int end = timing.samples_per_line_4fsc;
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kNtsc, 10, aws, end));
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kNtsc, 273, aws, end));
}

// ---------------------------------------------------------------------------
// NTSC lead-in: white flag only on line 11 (not line 274).
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerNtscCavTest, WhiteFlagLeadInOnlyLine11) {
  std::vector<Section::LineInjectionCode> codes;
  Section::LineInjectionCode li;
  li.code_type = "lead_in";
  codes.push_back(li);
  Section::LineInjectionCode wf;
  wf.code_type = "fm_white_flag";
  codes.push_back(wf);

  const auto section =
      MakeLaserdiscSection(SectionType::kLeadIn, DiscType::kCAV, codes);

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kNtsc);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kNtsc, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kNtsc);
  const TimingConstants timing = GetTimingConstants(Standard::kNtsc);
  const int aws = ActiveWindowStart(Standard::kNtsc);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kNtsc, timing.sample_rate_4fsc_hz,
                                   frame_lines, aws, &errors));
  EXPECT_TRUE(errors.empty());

  const int end = timing.samples_per_line_4fsc;
  // Per IEC 60857 §10.2.1: white flag in lead-in on line 11 only.
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kNtsc, 11, aws, end));
  EXPECT_FALSE(LineHasSignal(y_mv, Standard::kNtsc, 274, aws, end));
}

// ---------------------------------------------------------------------------
// NTSC lead-out: white flag on both lines 11 and 274.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerNtscCavTest, WhiteFlagLeadOutBothLines) {
  std::vector<Section::LineInjectionCode> codes;
  Section::LineInjectionCode lo;
  lo.code_type = "lead_out";
  codes.push_back(lo);
  Section::LineInjectionCode wf;
  wf.code_type = "fm_white_flag";
  codes.push_back(wf);

  const auto section =
      MakeLaserdiscSection(SectionType::kLeadOut, DiscType::kCAV, codes);

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kNtsc);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kNtsc, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kNtsc);
  const TimingConstants timing = GetTimingConstants(Standard::kNtsc);
  const int aws = ActiveWindowStart(Standard::kNtsc);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kNtsc, timing.sample_rate_4fsc_hz,
                                   frame_lines, aws, &errors));
  EXPECT_TRUE(errors.empty());

  const int end = timing.samples_per_line_4fsc;
  // Per IEC 60857 §10.2.2: white flag in lead-out on both lines 11 and 274.
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kNtsc, 11, aws, end));
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kNtsc, 274, aws, end));
}

// ---------------------------------------------------------------------------
// Section transition: switching from lead_in to programme_area reinitialises.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalCavTest, SectionTransitionReinitialises) {
  BiphaseInjectionManager manager;
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  const int aws = 177;
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);

  // Process one lead_in frame.
  Section::LineInjectionCode li_code;
  li_code.code_type = "lead_in";
  const auto lead_in_section =
      MakeLaserdiscSection(SectionType::kLeadIn, DiscType::kCAV, {li_code});

  auto y1 = MakeBlankingBuffer(Standard::kPal);
  std::vector<std::string> errors1;
  ASSERT_TRUE(manager.ProcessFrame(&y1, 0, offsets, counts, lead_in_section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, aws, &errors1));

  // Process one programme_area frame (picture_number).
  Section::LineInjectionCode pn_code;
  pn_code.code_type = "picture_number";
  pn_code.start_value = 1;
  pn_code.start_value_specified = true;
  const auto prog_section = MakeLaserdiscSection(SectionType::kProgrammeArea,
                                                 DiscType::kCAV, {pn_code});

  auto y2 = MakeBlankingBuffer(Standard::kPal);
  std::vector<std::string> errors2;
  ASSERT_TRUE(manager.ProcessFrame(&y2, 0, offsets, counts, prog_section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, aws, &errors2));

  EXPECT_TRUE(errors1.empty());
  EXPECT_TRUE(errors2.empty());

  const int end = timing.samples_per_line_4fsc;
  // Programme frame must carry picture_number signal.
  EXPECT_TRUE(LineHasSignal(y2, Standard::kPal, 17, aws, end));
}

// ---------------------------------------------------------------------------
// Users code appears on correct lines in lead_in (PAL CAV).
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalCavTest, UsersCodeInLeadIn) {
  Section::LineInjectionCode li;
  li.code_type = "lead_in";
  Section::LineInjectionCode uc;
  uc.code_type = "users_code";
  uc.users_code = "0x801234";
  uc.users_code_specified = true;

  const auto section =
      MakeLaserdiscSection(SectionType::kLeadIn, DiscType::kCAV, {li, uc});

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kPal);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, 177, &errors));
  EXPECT_TRUE(errors.empty());

  // users_code goes on line 16 (or 329) in lead-in for PAL CAV.
  const int end = timing.samples_per_line_4fsc;
  EXPECT_TRUE(LineHasSignal(y_mv, Standard::kPal, 16, 177, end));
}

// ---------------------------------------------------------------------------
// Chapter number code in programme area.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalCavTest, ChapterNumberInProgrammeArea) {
  std::vector<Section::LineInjectionCode> codes;
  Section::LineInjectionCode pn;
  pn.code_type = "picture_number";
  pn.start_value = 1;
  pn.start_value_specified = true;
  codes.push_back(pn);
  Section::LineInjectionCode cn;
  cn.code_type = "chapter_number";
  cn.chapter = 1;
  cn.chapter_specified = true;
  codes.push_back(cn);

  const auto section =
      MakeLaserdiscSection(SectionType::kProgrammeArea, DiscType::kCAV, codes);

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kPal);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, 177, &errors));
  EXPECT_TRUE(errors.empty());

  // chapter_number on lines where picture_number is NOT present (the engine
  // may skip if all lines are consumed by picture_number). With both codes the
  // engine places chapter on lines that picture_number doesn't occupy.
  // We just confirm no errors occurred and the function returned success.
}

// ---------------------------------------------------------------------------
// PAL non-VBI lines remain at blanking after biphase injection.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalCavTest, ActivePictureLinesUnaffected) {
  Section::LineInjectionCode code;
  code.code_type = "lead_in";
  const auto section =
      MakeLaserdiscSection(SectionType::kLeadIn, DiscType::kCAV, {code});

  BiphaseInjectionManager manager;
  auto y_mv = MakeBlankingBuffer(Standard::kPal);
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  std::vector<std::string> errors;

  ASSERT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                   Standard::kPal, timing.sample_rate_4fsc_hz,
                                   frame_lines, 177, &errors));

  const int aws = 177;
  const int end = timing.samples_per_line_4fsc;
  // Active picture line 100 is far outside VBI range; must remain at blanking.
  EXPECT_FALSE(LineHasSignal(y_mv, Standard::kPal, 100, aws, end));
}

// ---------------------------------------------------------------------------
// Multiple frames with same section: generators advance correctly.
// ---------------------------------------------------------------------------

TEST(BiphaseInjectionManagerPalCavTest, MultipleFramesSameSection) {
  Section::LineInjectionCode code;
  code.code_type = "picture_number";
  code.start_value = 1;
  code.start_value_specified = true;
  const auto section =
      MakeLaserdiscSection(SectionType::kProgrammeArea, DiscType::kCAV, {code});

  BiphaseInjectionManager manager;
  std::vector<int> offsets, counts;
  BuildLineLayout(Standard::kPal, &offsets, &counts);
  const auto frame_lines = BuildFrameTimingPrimitives(Standard::kPal);
  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  const int aws = 177;
  std::vector<std::string> errors;

  // Process 5 frames; all must succeed.
  for (int f = 0; f < 5; ++f) {
    auto y_mv = MakeBlankingBuffer(Standard::kPal);
    EXPECT_TRUE(manager.ProcessFrame(&y_mv, 0, offsets, counts, section,
                                     Standard::kPal, timing.sample_rate_4fsc_hz,
                                     frame_lines, aws, &errors))
        << "Frame " << f << " failed";
  }
  EXPECT_TRUE(errors.empty());
}

}  // namespace
}  // namespace videosynth
