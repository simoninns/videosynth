/*
 * File:        test_line_injection_vits_integration.cpp
 * Module:      line_injection_tests
 * Purpose:     Integration tests for VITS line injection in the generation pipeline.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "videosynth/generation_stage.h"
#include "videosynth/model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::string DefaultBarsExrPath(Standard standard) {
  return (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
          (standard == Standard::kPal
               ? "videosynth-assets/assets/exr/720x576/100_BARS.exr"
               : "videosynth-assets/assets/exr/720x486/100_BARS.exr"))
      .string();
}

// Helper to create a minimal project with VITS line injection
Project CreateProjectWithVits(Standard standard, 
                              const std::string& vits_type, 
                              int target_line) {
  Project project;
  project.cvbs_presets.video_standard_preset = standard;
  project.cvbs_presets.sample_encoding_preset = "CVBS_TPG21_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";

  project.output.video_path = "test_output.composite";
  project.output.metadata_path = "test_output.meta";

  Section section;
  section.name = "TestSection";
  section.type = "progressive";
  section.source = DefaultBarsExrPath(standard);
  section.start_frame = 0;
  section.duration_frames = 1;
  section.duration_frames_all = false;

  Section::LineInjection injection;
  injection.type = "vits";
  injection.target_lines = {target_line};
  injection.vits_type = vits_type;

  section.line_injections.push_back(injection);
  project.sections.push_back(section);

  return project;
}

// Helper to get the sample index for a given line and time
int GetSampleIndexForLine(const std::vector<SampleFixed>& samples,
                          const TimingConstants& timing,
                          int line_number,
                          double time_us) {
  const int samples_per_line = timing.samples_per_line_4fsc;
  const int line_offset = (line_number - 1) * samples_per_line;
  const int time_offset = static_cast<int>(std::lround((time_us * 1e-6) * timing.sample_rate_4fsc_hz));
  return line_offset + time_offset;
}

// Helper to compute mean level over a time window on a specific line
double MeanLevelOnLine(const std::vector<SampleFixed>& samples,
                      const TimingConstants& timing,
                      int line_number,
                      double start_us,
                      double end_us) {
  const int start_idx = GetSampleIndexForLine(samples, timing, line_number, start_us);
  const int end_idx = GetSampleIndexForLine(samples, timing, line_number, end_us);

  if (start_idx >= static_cast<int>(samples.size()) || end_idx > static_cast<int>(samples.size())) {
    return 0.0;
  }

  double sum = 0.0;
  int count = 0;
  for (int i = start_idx; i < end_idx && i < static_cast<int>(samples.size()); ++i) {
    sum += SampleFixedToMillivolts(samples[static_cast<std::size_t>(i)]);
    ++count;
  }

  return count > 0 ? (sum / static_cast<double>(count)) : 0.0;
}

}  // namespace

// Test that VITS line injection is applied to the correct frame line for PAL
TEST(LineInjectionVitsIntegrationTest, PalVits17InjectedOnLine17) {
  Project project = CreateProjectWithVits(Standard::kPal, "vits17", 17);

  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y_samples;
  std::vector<SampleFixed> c_samples;

  EXPECT_TRUE(generation.Generate(project, &y_samples, &c_samples, &errors));
  EXPECT_TRUE(errors.empty());

  // Verify samples were generated
  ASSERT_FALSE(y_samples.empty());

  const TimingConstants timing = GetTimingConstants(Standard::kPal);

  // Line 17 should have VITS content (white reference at 700 mV, 12-22 us)
  const double mean_17 = MeanLevelOnLine(y_samples, timing, 17, 14.0, 20.0);
  EXPECT_NEAR(mean_17, 700.0, 20.0);
}

TEST(LineInjectionVitsIntegrationTest, PalVitsTypesAllRenderable) {
  const std::vector<std::pair<std::string, int>> pal_vits_types = {
      {"vits17", 17},
      {"itu-multiburst", 18},
      {"uk-national", 19},
      {"vits20", 20},
      {"itu-composite", 330},
      {"itu-combination", 331},
  };

  for (const auto& [vits_type, line] : pal_vits_types) {
    Project project = CreateProjectWithVits(Standard::kPal, vits_type, line);

    GenerationStage generation;
    std::vector<std::string> errors;
    std::vector<SampleFixed> y_samples;
    std::vector<SampleFixed> c_samples;

    EXPECT_TRUE(generation.Generate(project, &y_samples, &c_samples, &errors))
        << "Failed for " << vits_type << ": " << errors[0];
    EXPECT_TRUE(errors.empty()) << "Failed for " << vits_type << ": " << errors[0];

    // Verify samples were generated
    ASSERT_FALSE(y_samples.empty()) << "No samples for " << vits_type;
  }
}

TEST(LineInjectionVitsIntegrationTest, NtscVitsTypesAllRenderable) {
  const std::vector<std::pair<std::string, int>> ntsc_vits_types = {
      {"ntc7-composite", 17},
      {"ntc7-combination", 280},
      {"fcc-multiburst", 18},
      {"fcc-composite", 281},
      {"virs", 21},
  };

  for (const auto& [vits_type, line] : ntsc_vits_types) {
    Project project = CreateProjectWithVits(Standard::kNtsc, vits_type, line);

    GenerationStage generation;
    std::vector<std::string> errors;
    std::vector<SampleFixed> y_samples;
    std::vector<SampleFixed> c_samples;

    EXPECT_TRUE(generation.Generate(project, &y_samples, &c_samples, &errors))
        << "Failed for " << vits_type << ": " << errors[0];
    EXPECT_TRUE(errors.empty()) << "Failed for " << vits_type << ": " << errors[0];

    // Verify samples were generated
    ASSERT_FALSE(y_samples.empty()) << "No samples for " << vits_type;
  }
}

// Test that VITS injection only affects the targeted line
TEST(LineInjectionVitsIntegrationTest, VitsOnlyAffectsTargetedLine) {
  Project project = CreateProjectWithVits(Standard::kPal, "vits17", 17);

  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y_samples;
  std::vector<SampleFixed> c_samples;

  EXPECT_TRUE(generation.Generate(project, &y_samples, &c_samples, &errors));
  EXPECT_TRUE(errors.empty());

  const TimingConstants timing = GetTimingConstants(Standard::kPal);

  // Line 17 should have VITS content (white reference)
  const double mean_17 = MeanLevelOnLine(y_samples, timing, 17, 14.0, 20.0);
  EXPECT_NEAR(mean_17, 700.0, 20.0);

  // Line 18 should NOT have the same VITS white reference
  const double mean_18 = MeanLevelOnLine(y_samples, timing, 18, 14.0, 20.0);
  // Line 18 will have active video content (color bars), not 700 mV white
  EXPECT_NE(mean_18, 700.0);
}

// Test that VITS line injection works with multiple lines
TEST(LineInjectionVitsIntegrationTest, MultipleVitsLinesInSameFrame) {
  Project project;
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.cvbs_presets.sample_encoding_preset = "CVBS_TPG21_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";

  project.output.video_path = "test_output.composite";
  project.output.metadata_path = "test_output.meta";

  Section section;
  section.name = "TestSection";
  section.type = "progressive";
  section.source = DefaultBarsExrPath(Standard::kPal);
  section.start_frame = 0;
  section.duration_frames = 1;
  section.duration_frames_all = false;

  // Add multiple VITS lines
  Section::LineInjection injection17;
  injection17.type = "vits";
  injection17.target_lines = {17};
  injection17.vits_type = "vits17";
  section.line_injections.push_back(injection17);

  Section::LineInjection injection18;
  injection18.type = "vits";
  injection18.target_lines = {18};
  injection18.vits_type = "itu-multiburst";
  section.line_injections.push_back(injection18);

  Section::LineInjection injection19;
  injection19.type = "vits";
  injection19.target_lines = {19};
  injection19.vits_type = "uk-national";
  section.line_injections.push_back(injection19);

  project.sections.push_back(section);

  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y_samples;
  std::vector<SampleFixed> c_samples;

  EXPECT_TRUE(generation.Generate(project, &y_samples, &c_samples, &errors));
  EXPECT_TRUE(errors.empty());

  const TimingConstants timing = GetTimingConstants(Standard::kPal);

  // Line 17: vits17 has white reference at 700 mV
  const double mean_17 = MeanLevelOnLine(y_samples, timing, 17, 14.0, 20.0);
  EXPECT_NEAR(mean_17, 700.0, 20.0);

  // Line 18: itu-multiburst has grey pedestal at 350 mV (measure in stable region after reference bars)
  const double mean_18 = MeanLevelOnLine(y_samples, timing, 18, 25.0, 30.0);
  EXPECT_NEAR(mean_18, 350.0, 20.0);

  // Line 19: uk-national has white reference at 700 mV
  const double mean_19 = MeanLevelOnLine(y_samples, timing, 19, 14.0, 20.0);
  EXPECT_NEAR(mean_19, 700.0, 20.0);
}

// Test that VITS and normal active video coexist correctly
TEST(LineInjectionVitsIntegrationTest, VitsCoexistsWithActiveVideo) {
  Project project = CreateProjectWithVits(Standard::kPal, "vits17", 17);

  GenerationStage generation;
  std::vector<std::string> errors;
  std::vector<SampleFixed> y_samples;
  std::vector<SampleFixed> c_samples;

  EXPECT_TRUE(generation.Generate(project, &y_samples, &c_samples, &errors));
  EXPECT_TRUE(errors.empty());

  const TimingConstants timing = GetTimingConstants(Standard::kPal);

  // Line 17 should have VITS content
  const double mean_vits = MeanLevelOnLine(y_samples, timing, 17, 14.0, 20.0);
  EXPECT_NEAR(mean_vits, 700.0, 20.0);

  // Active video lines (e.g., line 100) should have normal test pattern content
  // For 100_BARS, this should be color bars, not VITS
  const double mean_active = MeanLevelOnLine(y_samples, timing, 100, 14.0, 20.0);

  // Active video should have color bar content (not 700 mV white)
  // Color bars have various levels, so we just check it's not the VITS white reference
  // This is a weak check but verifies different content
  EXPECT_FALSE(std::fabs(mean_active - 700.0) < 20.0);
}

}  // namespace videosynth
