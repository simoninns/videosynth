/*
 * File:        test_frame_enrichment.cpp
 * Module:      frame_enrichment_tests
 * Purpose:     Verifies that the two-pass frame processing (ResolveFrame +
 *              InjectResolvedVbiLines) reproduces the sequential ProcessFrame
 *              path exactly, and that enriched per-frame values (picture
 *              numbers, timecodes, colour phase indices) match the values
 *              previously produced frame-by-frame.
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
#include "videosynth/clv_code_generator.h"
#include "videosynth/fixed_point.h"
#include "videosynth/frame_enrichment.h"
#include "videosynth/model.h"
#include "videosynth/signal_timing_model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {
namespace {

// ---------------------------------------------------------------------------
// Test helpers (mirroring test_biphase_injection_manager.cpp conventions)
// ---------------------------------------------------------------------------

// Builds a Y buffer for one frame at blanking level (0 mV).
std::vector<SampleFixed> MakeBlankingBuffer(Standard standard) {
  const TimingConstants timing = GetTimingConstants(standard);
  const int lines = timing.lines_per_frame;
  const int spl = timing.samples_per_line_4fsc;
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

// Active-picture window end samples, exclusive (mirrors generation_stage.cpp).
int ActiveWindowEnd(Standard standard) {
  const TimingConstants timing = GetTimingConstants(standard);
  if (standard == Standard::kPal) {
    return 177 +
           static_cast<int>(std::lround(timing.sample_rate_4fsc_hz * 52.0e-6));
  }
  return static_cast<int>(std::lround(timing.sample_rate_4fsc_hz * 62.5e-6));
}

// Builds a minimal Section with one laserdisc LineInjection.
Section MakeLaserdiscSection(
    const std::string& name, SectionType section_type, DiscType disc_type,
    const std::vector<Section::LineInjectionCode>& codes) {
  Section section;
  section.name = name;
  section.type = "progressive";
  section.section_type = section_type;
  section.duration_frames = 1;

  // disc_type is now a project-level decision applied to the manager via
  // SetProjectDiscType(); the section injection only carries codes. The
  // argument is retained so call sites still document the intended disc format.
  (void)disc_type;
  Section::LineInjection injection;
  injection.type = "laserdisc";
  injection.codes = codes;
  section.line_injections.push_back(injection);

  return section;
}

Section::LineInjectionCode MakeCode(const std::string& code_type,
                                    int start_value = 0,
                                    bool start_specified = false) {
  Section::LineInjectionCode code;
  code.code_type = code_type;
  code.start_value = start_value;
  code.start_value_specified = start_specified;
  return code;
}

// Runs frame_count frames of the given sections (one section per span entry)
// through two independent managers: one using the sequential ProcessFrame
// path, one using ResolveFrame + InjectResolvedVbiLines. Asserts the injected
// frame buffers are identical sample-for-sample, and returns the resolved
// enrichments for further value assertions.
std::vector<FrameEnrichment> AssertTwoPassMatchesSequential(
    Standard standard, DiscType disc_type,
    const std::vector<const Section*>& frame_sections) {
  const TimingConstants timing = GetTimingConstants(standard);
  std::vector<int> offsets;
  std::vector<int> counts;
  BuildLineLayout(standard, &offsets, &counts);
  const std::vector<LineTimingPrimitive> frame_lines =
      BuildFrameTimingPrimitives(standard);
  const int window_start = ActiveWindowStart(standard);
  const int window_end = ActiveWindowEnd(standard);

  BiphaseInjectionManager sequential_manager;
  BiphaseInjectionManager two_pass_manager;
  // disc_type is now a project-level input supplied to the manager once per
  // generation pass rather than derived from each section.
  sequential_manager.SetProjectDiscType(disc_type);
  two_pass_manager.SetProjectDiscType(disc_type);
  std::vector<FrameEnrichment> enrichments;
  enrichments.reserve(frame_sections.size());

  for (std::size_t frame = 0; frame < frame_sections.size(); ++frame) {
    const Section& section = *frame_sections[frame];
    std::vector<std::string> errors;

    std::vector<SampleFixed> sequential_buffer = MakeBlankingBuffer(standard);
    EXPECT_TRUE(sequential_manager.ProcessFrame(
        &sequential_buffer, 0, offsets, counts, section, standard,
        timing.sample_rate_4fsc_hz, frame_lines, window_start, window_end,
        &errors))
        << "ProcessFrame failed at frame " << frame;

    FrameEnrichment enrichment;
    EXPECT_TRUE(two_pass_manager.ResolveFrame(
        section, standard, timing.sample_rate_4fsc_hz, window_start,
        &enrichment, &errors))
        << "ResolveFrame failed at frame " << frame;

    std::vector<SampleFixed> two_pass_buffer = MakeBlankingBuffer(standard);
    InjectResolvedVbiLines(enrichment, &two_pass_buffer, 0, offsets, counts,
                           standard, timing.sample_rate_4fsc_hz, window_end);

    EXPECT_EQ(sequential_buffer, two_pass_buffer)
        << "Injected samples diverge at frame " << frame;

    // The context snapshots must also agree (OSD tokens depend on them).
    const PerFrameContext& sequential_context =
        sequential_manager.GetLastFrameContext();
    EXPECT_EQ(sequential_context.picture_number,
              enrichment.context.picture_number)
        << "picture_number diverges at frame " << frame;
    EXPECT_EQ(sequential_context.colour_frame_index,
              enrichment.context.colour_frame_index)
        << "colour_frame_index diverges at frame " << frame;
    EXPECT_EQ(sequential_context.biphase_words,
              enrichment.context.biphase_words)
        << "biphase_words diverge at frame " << frame;

    enrichments.push_back(enrichment);
  }

  return enrichments;
}

// ---------------------------------------------------------------------------
// Two-pass equivalence against the sequential ProcessFrame path
// ---------------------------------------------------------------------------

TEST(FrameEnrichmentTest, TwoPassMatchesSequentialForPalCavProgramme) {
  Section::LineInjectionCode chapter = MakeCode("chapter_number");
  chapter.chapter = 3;
  const Section programme = MakeLaserdiscSection(
      "Programme", SectionType::kProgrammeArea, DiscType::kCAV,
      {MakeCode("picture_number", 1, true), MakeCode("picture_stop"), chapter});
  std::vector<const Section*> frames(8, &programme);
  AssertTwoPassMatchesSequential(Standard::kPal, DiscType::kCAV, frames);
}

TEST(FrameEnrichmentTest, TwoPassMatchesSequentialForPalClvProgramme) {
  const Section programme = MakeLaserdiscSection(
      "Programme", SectionType::kProgrammeArea, DiscType::kCLV,
      {MakeCode("programme_time_code"), MakeCode("clv_picture_number")});
  std::vector<const Section*> frames(6, &programme);
  AssertTwoPassMatchesSequential(Standard::kPal, DiscType::kCLV, frames);
}

TEST(FrameEnrichmentTest, TwoPassMatchesSequentialForNtscCavWithFmCodes) {
  const Section programme = MakeLaserdiscSection(
      "Programme", SectionType::kProgrammeArea, DiscType::kCAV,
      {MakeCode("picture_number", 1, true),
       MakeCode("fm_picture_number", 1, true), MakeCode("fm_white_flag")});
  std::vector<const Section*> frames(6, &programme);
  AssertTwoPassMatchesSequential(Standard::kNtsc, DiscType::kCAV, frames);
}

TEST(FrameEnrichmentTest, TwoPassMatchesSequentialAcrossSectionTransitions) {
  const Section lead_in = MakeLaserdiscSection(
      "LeadIn", SectionType::kLeadIn, DiscType::kCAV, {MakeCode("lead_in")});
  const Section programme = MakeLaserdiscSection(
      "Programme", SectionType::kProgrammeArea, DiscType::kCAV,
      {MakeCode("picture_number", 100, true)});
  const Section lead_out = MakeLaserdiscSection(
      "LeadOut", SectionType::kLeadOut, DiscType::kCAV, {MakeCode("lead_out")});
  std::vector<const Section*> frames;
  for (int i = 0; i < 3; ++i) frames.push_back(&lead_in);
  for (int i = 0; i < 4; ++i) frames.push_back(&programme);
  for (int i = 0; i < 3; ++i) frames.push_back(&lead_out);
  AssertTwoPassMatchesSequential(Standard::kPal, DiscType::kCAV, frames);
}

TEST(FrameEnrichmentTest, TwoPassMatchesSequentialWithoutLaserdisc) {
  Section plain;
  plain.name = "Plain";
  plain.type = "progressive";
  plain.section_type = SectionType::kProgrammeArea;
  plain.duration_frames = 1;

  std::vector<const Section*> frames(5, &plain);
  const std::vector<FrameEnrichment> enrichments =
      AssertTwoPassMatchesSequential(Standard::kPal, DiscType::kUnknown,
                                     frames);

  for (std::size_t frame = 0; frame < enrichments.size(); ++frame) {
    EXPECT_TRUE(enrichments[frame].vbi_lines.empty());
    EXPECT_EQ(enrichments[frame].context.colour_frame_index,
              static_cast<int>(frame % 4U));
  }
}

// ---------------------------------------------------------------------------
// Enriched value sequences (picture numbers, timecodes, phase indices)
// ---------------------------------------------------------------------------

TEST(FrameEnrichmentTest, EnrichedPictureNumbersAdvanceSequentially) {
  const Section programme = MakeLaserdiscSection(
      "Programme", SectionType::kProgrammeArea, DiscType::kCAV,
      {MakeCode("picture_number", 100, true)});
  std::vector<const Section*> frames(5, &programme);

  const std::vector<FrameEnrichment> enrichments =
      AssertTwoPassMatchesSequential(Standard::kPal, DiscType::kCAV, frames);

  for (std::size_t frame = 0; frame < enrichments.size(); ++frame) {
    const int expected_pn = 100 + static_cast<int>(frame);
    EXPECT_EQ(enrichments[frame].context.picture_number, expected_pn);
    // PAL colour phase: (PN - 1) % 4.
    EXPECT_EQ(enrichments[frame].context.colour_frame_index,
              (expected_pn - 1) % 4);
  }
}

TEST(FrameEnrichmentTest, EnrichedClvTimecodesMatchGeneratorSequence) {
  const Section programme =
      MakeLaserdiscSection("Programme", SectionType::kProgrammeArea,
                           DiscType::kCLV, {MakeCode("programme_time_code")});
  std::vector<const Section*> frames(4, &programme);

  const std::vector<FrameEnrichment> enrichments =
      AssertTwoPassMatchesSequential(Standard::kPal, DiscType::kCLV, frames);

  // An independent reference generator reproduces the expected timecode words.
  ProgrammeTimeCodeGenerator reference(0, 0, Standard::kPal);
  for (std::size_t frame = 0; frame < enrichments.size(); ++frame) {
    const uint32_t expected_code = reference.CurrentCode();
    bool found_time_code_line = false;
    for (const VbiLineInjection& injection : enrichments[frame].vbi_lines) {
      if (injection.kind == VbiLineInjection::Kind::kBiphase24 &&
          injection.biphase_code == expected_code) {
        found_time_code_line = true;
        break;
      }
    }
    EXPECT_TRUE(found_time_code_line)
        << "Frame " << frame << " enrichment lacks the expected "
        << "programme_time_code word.";
    reference.Advance();
  }
}

TEST(FrameEnrichmentTest, NtscColourFrameIndexAlternates) {
  const Section programme = MakeLaserdiscSection(
      "Programme", SectionType::kProgrammeArea, DiscType::kCAV,
      {MakeCode("picture_number", 1, true)});
  std::vector<const Section*> frames(4, &programme);

  const std::vector<FrameEnrichment> enrichments =
      AssertTwoPassMatchesSequential(Standard::kNtsc, DiscType::kCAV, frames);

  for (std::size_t frame = 0; frame < enrichments.size(); ++frame) {
    EXPECT_EQ(enrichments[frame].context.colour_frame_index,
              static_cast<int>(frame % 2U));
  }
}

}  // namespace
}  // namespace videosynth
