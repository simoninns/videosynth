/*
 * File:        test_fixture_projects.cpp
 * Module:      fixture_project_tests
 * Purpose:     Verifies PAL/NTSC fixture projects through full generation and
 * output checks.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "videosynth/fixed_point.h"
#include "videosynth/generation_stage.h"
#include "videosynth/output_stage.h"
#include "videosynth/progressive_frame_source.h"
#include "videosynth/progressive_frame_source_probe.h"
#include "videosynth/project_validator.h"
#include "videosynth/signal_shaping.h"
#include "videosynth/timing_constants.h"
#include "videosynth/yaml_project_parser.h"

namespace videosynth {
namespace {

std::string FixturePath(const std::string& fixture_name) {
  return std::string(VIDEOSYNTH_SOURCE_DIR) + "/tests/projects/" + fixture_name;
}

std::filesystem::path ResolveFixtureOutputPath(
    const std::string& configured_path) {
  const std::filesystem::path path(configured_path);
  if (path.is_absolute()) {
    return path;
  }
  return std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) / path;
}

void ResolveProgressiveSourcePaths(Project* project) {
  if (project == nullptr) {
    return;
  }

  for (Section& section : project->sections) {
    if (section.type != "progressive" || section.source.empty()) {
      continue;
    }

    const std::filesystem::path source_path(section.source);
    if (!source_path.is_absolute()) {
      section.source =
          (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) / source_path).string();
    }
  }
}

std::size_t ExpectedProgressiveExrSectionCount(Standard standard) {
  switch (standard) {
    case Standard::kPal:
      return 20U;
    case Standard::kNtsc:
      return 19U;
    default:
      return 0U;
  }
}

std::size_t ExpectedProgressiveExrFrameCount(Standard standard) {
  return ExpectedProgressiveExrSectionCount(standard) * 8U;
}

struct ExpectedVitsInjection {
  int target_line;
  std::string vits_type;
};

struct ExpectedVitsFixture {
  std::string fixture_name;
  Standard standard;
  std::vector<ExpectedVitsInjection> injections;
};

void ExpectVitsFixtureProject(const ExpectedVitsFixture& expected) {
  YamlProjectParser parser;
  ProjectValidator validator;
  GenerationStage generation;

  const ParseResult parsed =
      parser.ParseFile(FixturePath(expected.fixture_name));
  ASSERT_TRUE(parsed.ok) << expected.fixture_name;

  Project project = parsed.project;
  ResolveProgressiveSourcePaths(&project);
  ASSERT_TRUE(validator.Validate(project).is_valid) << expected.fixture_name;

  ASSERT_EQ(project.cvbs_presets.video_standard_preset, expected.standard);
  ASSERT_EQ(project.sections.size(), 1U) << expected.fixture_name;

  const Section& section = project.sections[0];
  ASSERT_EQ(section.type, "progressive");
  ASSERT_EQ(section.duration_frames, 1);
  ASSERT_FALSE(section.duration_frames_all);
  ASSERT_EQ(section.line_injections.size(), expected.injections.size())
      << expected.fixture_name;

  for (std::size_t index = 0; index < expected.injections.size(); ++index) {
    const Section::LineInjection& injection = section.line_injections[index];
    const ExpectedVitsInjection& expected_injection =
        expected.injections[index];
    EXPECT_EQ(injection.type, "vits") << expected.fixture_name;
    ASSERT_EQ(injection.target_lines.size(), 1U) << expected.fixture_name;
    EXPECT_EQ(injection.target_lines[0], expected_injection.target_line)
        << expected.fixture_name;
    EXPECT_EQ(injection.vits_type, expected_injection.vits_type)
        << expected.fixture_name;
  }

  std::vector<SampleFixed> y_mv;
  std::vector<SampleFixed> c_mv;
  std::vector<std::string> generation_errors;
  ASSERT_TRUE(generation.Generate(project, &y_mv, &c_mv, &generation_errors))
      << expected.fixture_name;

  const std::size_t frame_span = static_cast<std::size_t>(
      SamplesPerFrame4fsc(project.cvbs_presets.video_standard_preset));
  EXPECT_EQ(y_mv.size(), frame_span) << expected.fixture_name;
  EXPECT_EQ(c_mv.size(), y_mv.size()) << expected.fixture_name;
}

bool QueryCvbsMetadataFrameCount(const std::filesystem::path& path,
                                 int64_t* frame_count) {
  if (frame_count == nullptr) {
    return false;
  }

  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    sqlite3_close(db);
    return false;
  }

  const char* query_sql =
      "SELECT number_of_sequential_frames FROM cvbs_file LIMIT 1;";
  sqlite3_stmt* stmt = nullptr;

  if (sqlite3_prepare_v2(db, query_sql, -1, &stmt, nullptr) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return false;
  }

  bool result = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    *frame_count = sqlite3_column_int64(stmt, 0);
    result = true;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return result;
}

bool QueryCvbsMetadataEncodingAndFrameCount(const std::filesystem::path& path,
                                            std::string* sample_encoding_preset,
                                            int64_t* frame_count) {
  if (sample_encoding_preset == nullptr || frame_count == nullptr) {
    return false;
  }

  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    sqlite3_close(db);
    return false;
  }

  const char* query_sql =
      "SELECT sample_encoding_preset, number_of_sequential_frames FROM "
      "cvbs_file LIMIT 1;";
  sqlite3_stmt* stmt = nullptr;

  if (sqlite3_prepare_v2(db, query_sql, -1, &stmt, nullptr) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return false;
  }

  bool result = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* encoding =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (encoding != nullptr) {
      *sample_encoding_preset = encoding;
      *frame_count = sqlite3_column_int64(stmt, 1);
      result = true;
    }
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return result;
}

TEST(ProjectFixturesTest, ProgressiveExrFixturesParseAndValidate) {
  YamlProjectParser parser;
  ProgressiveFrameSourceProbe progressive_frame_source_probe;
  ProjectValidator validator(&progressive_frame_source_probe);

  const std::vector<std::string> fixtures = {
      "general/pal_progressive_exr.yaml", "general/ntsc_progressive_exr.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;

    Project project = parsed.project;
    ResolveProgressiveSourcePaths(&project);
    const ValidationResult validation = validator.Validate(project);
    ASSERT_TRUE(validation.is_valid) << fixture;

    const std::size_t expected_section_count =
        ExpectedProgressiveExrSectionCount(
            project.cvbs_presets.video_standard_preset);
    ASSERT_EQ(project.sections.size(), expected_section_count);
    for (const Section& section : project.sections) {
      EXPECT_EQ(section.type, "progressive");
      EXPECT_EQ(section.duration_frames, 8);
      EXPECT_FALSE(section.duration_frames_all);
    }
  }
}

TEST(ProjectFixturesTest, ProgressiveMkvFixturesParseAndValidate) {
  YamlProjectParser parser;
  ProgressiveFrameSourceProbe progressive_frame_source_probe;
  ProjectValidator validator(&progressive_frame_source_probe);

  const std::vector<std::string> fixtures = {
      "general/pal_progressive_mkv.yaml", "general/ntsc_progressive_mkv.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;

    Project project = parsed.project;
    ResolveProgressiveSourcePaths(&project);
    const ValidationResult validation = validator.Validate(project);
    ASSERT_TRUE(validation.is_valid) << fixture;

    ASSERT_EQ(project.sections.size(), 2U);
    for (const Section& section : project.sections) {
      EXPECT_EQ(section.type, "progressive");
      EXPECT_TRUE(section.duration_frames_all);
      EXPECT_EQ(section.duration_frames, 0);
    }
  }
}

TEST(ProjectFixturesTest,
     ProgressiveExrFixturesGenerateCompositeOutputWithExpectedFrameCount) {
  YamlProjectParser parser;
  ProjectValidator validator;
  GenerationStage generation;
  OutputStage output;

  const std::vector<std::string> fixtures = {
      "general/pal_progressive_exr.yaml", "general/ntsc_progressive_exr.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;
    Project project = parsed.project;
    project.output.video_path =
        ResolveFixtureOutputPath(project.output.video_path).string();
    project.output.metadata_path =
        ResolveFixtureOutputPath(project.output.metadata_path).string();
    ResolveProgressiveSourcePaths(&project);
    ASSERT_TRUE(validator.Validate(project).is_valid) << fixture;

    std::vector<SampleFixed> y_mv;
    std::vector<SampleFixed> c_mv;
    std::vector<std::string> generation_errors;
    ASSERT_TRUE(generation.Generate(project, &y_mv, &c_mv, &generation_errors))
        << fixture;

    const std::size_t frame_span = static_cast<std::size_t>(
        SamplesPerFrame4fsc(project.cvbs_presets.video_standard_preset));
    const std::size_t expected_frame_count = ExpectedProgressiveExrFrameCount(
        project.cvbs_presets.video_standard_preset);
    ASSERT_EQ(y_mv.size(), frame_span * expected_frame_count) << fixture;
    ASSERT_EQ(c_mv.size(), y_mv.size()) << fixture;

    const std::filesystem::path output_path = project.output.video_path;
    const std::filesystem::path metadata_path = project.output.metadata_path;
    std::filesystem::create_directories(output_path.parent_path());
    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);

    std::vector<std::string> output_errors;
    ASSERT_TRUE(output.Write(project, y_mv, c_mv, &output_errors)) << fixture;

    int64_t frame_count_from_metadata = 0;
    ASSERT_TRUE(
        QueryCvbsMetadataFrameCount(metadata_path, &frame_count_from_metadata))
        << fixture;
    EXPECT_EQ(frame_count_from_metadata,
              static_cast<int64_t>(expected_frame_count))
        << fixture;

    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);
  }
}

TEST(ProjectFixturesTest,
     ProgressiveMkvFixturesGenerateCompositeOutputForFullSourceLength) {
  YamlProjectParser parser;
  ProgressiveFrameSourceProbe progressive_frame_source_probe;
  ProjectValidator validator(&progressive_frame_source_probe);
  GenerationStage generation;
  OutputStage output;
  ProgressiveFrameSource progressive_source;

  const std::vector<std::string> fixtures = {
      "general/pal_progressive_mkv.yaml", "general/ntsc_progressive_mkv.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;
    Project project = parsed.project;
    project.output.video_path =
        ResolveFixtureOutputPath(project.output.video_path).string();
    project.output.metadata_path =
        ResolveFixtureOutputPath(project.output.metadata_path).string();
    ResolveProgressiveSourcePaths(&project);
    ASSERT_TRUE(validator.Validate(project).is_valid) << fixture;

    int expected_source_frames = 0;
    for (const Section& section : project.sections) {
      int section_source_frames = 0;
      std::string count_error;
      ASSERT_TRUE(progressive_source.ResolveFrameCount(
          section, project.cvbs_presets.video_standard_preset,
          &section_source_frames, &count_error))
          << fixture << ": " << count_error;
      ASSERT_GT(section_source_frames, 0) << fixture;
      expected_source_frames += section_source_frames;
    }

    std::vector<SampleFixed> y_mv;
    std::vector<SampleFixed> c_mv;
    std::vector<std::string> generation_errors;
    ASSERT_TRUE(generation.Generate(project, &y_mv, &c_mv, &generation_errors))
        << fixture;

    const std::size_t frame_span = static_cast<std::size_t>(
        SamplesPerFrame4fsc(project.cvbs_presets.video_standard_preset));
    ASSERT_EQ(y_mv.size(),
              frame_span * static_cast<std::size_t>(expected_source_frames))
        << fixture;
    ASSERT_EQ(c_mv.size(), y_mv.size()) << fixture;

    const std::filesystem::path output_path = project.output.video_path;
    const std::filesystem::path metadata_path = project.output.metadata_path;
    std::filesystem::create_directories(output_path.parent_path());
    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);

    std::vector<std::string> output_errors;
    ASSERT_TRUE(output.Write(project, y_mv, c_mv, &output_errors)) << fixture;

    int64_t frame_count_from_metadata = 0;
    ASSERT_TRUE(
        QueryCvbsMetadataFrameCount(metadata_path, &frame_count_from_metadata))
        << fixture;
    EXPECT_EQ(frame_count_from_metadata, expected_source_frames) << fixture;

    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);
  }
}

TEST(ProjectFixturesTest, VitsFixtureProjectsParseValidateAndGenerate) {
  ExpectVitsFixtureProject(ExpectedVitsFixture{
      .fixture_name = "general/pal_vits.yaml",
      .standard = Standard::kPal,
      .injections =
          {
              {17, "vits17"},
              {18, "itu-multiburst"},
              {19, "uk-national"},
              {20, "vits20"},
              {330, "itu-composite"},
              {331, "itu-combination"},
          },
  });

  ExpectVitsFixtureProject(ExpectedVitsFixture{
      .fixture_name = "general/ntsc_vits.yaml",
      .standard = Standard::kNtsc,
      .injections =
          {
              {17, "ntc7-composite"},
              {18, "fcc-multiburst"},
              {21, "virs"},
              {280, "ntc7-combination"},
              {281, "fcc-composite"},
          },
  });
}

TEST(ProjectFixturesTest, FixtureProjectsCoverSupportedOutputEncodingFamilies) {
  YamlProjectParser parser;
  ProjectValidator validator;
  GenerationStage generation;
  OutputStage output;

  const std::vector<std::string> fixtures = {
      "general/pal_progressive_exr.yaml", "general/ntsc_progressive_exr.yaml"};
  const std::vector<std::string> output_presets = {
      "CVBS_U10_4FSC", "CVBS_U16_4FSC", "CVBS_TPG21_4FSC",
      "RAW_S16_28M",   "RAW_S16_40M",   "CVBS_S16_FSC"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;
    Project base_project = parsed.project;
    ResolveProgressiveSourcePaths(&base_project);
    ASSERT_TRUE(validator.Validate(base_project).is_valid) << fixture;

    std::vector<SampleFixed> y_mv;
    std::vector<SampleFixed> c_mv;
    std::vector<std::string> generation_errors;
    ASSERT_TRUE(
        generation.Generate(base_project, &y_mv, &c_mv, &generation_errors))
        << fixture;

    for (const std::string& output_preset : output_presets) {
      Project project = base_project;
      project.cvbs_presets.sample_encoding_preset = output_preset;
      const int64_t expected_frame_count =
          static_cast<int64_t>(ExpectedProgressiveExrFrameCount(
              project.cvbs_presets.video_standard_preset));

      const std::string fixture_stem =
          std::filesystem::path(fixture).stem().string();
      const std::filesystem::path output_path =
          std::filesystem::temp_directory_path() /
          ("videosynth_stage4_" + fixture_stem + "_" + output_preset +
           ".composite");
      const std::filesystem::path metadata_path =
          std::filesystem::temp_directory_path() /
          ("videosynth_stage4_" + fixture_stem + "_" + output_preset + ".meta");
      project.output.video_path = output_path.string();
      project.output.metadata_path = metadata_path.string();

      std::filesystem::remove(output_path);
      std::filesystem::remove(metadata_path);

      std::vector<std::string> output_errors;
      ASSERT_TRUE(output.Write(project, y_mv, c_mv, &output_errors))
          << fixture << " / " << output_preset;

      const std::size_t expected_samples =
          SamplesPerFrameForEncodingPreset(
              project.cvbs_presets.video_standard_preset, output_preset) *
          static_cast<std::size_t>(expected_frame_count);
      EXPECT_EQ(std::filesystem::file_size(output_path),
                expected_samples * sizeof(std::int16_t))
          << fixture << " / " << output_preset;

      std::string written_output_preset;
      int64_t frame_count = 0;
      ASSERT_TRUE(QueryCvbsMetadataEncodingAndFrameCount(
          metadata_path, &written_output_preset, &frame_count))
          << fixture << " / " << output_preset;
      EXPECT_EQ(written_output_preset, output_preset)
          << fixture << " / " << output_preset;
      EXPECT_EQ(frame_count, expected_frame_count)
          << fixture << " / " << output_preset;

      std::filesystem::remove(output_path);
      std::filesystem::remove(metadata_path);
    }
  }
}

// Return the absolute sample offset for the start of a 1-based PAL line.
// EBU Tech. 3280-E: lines 313 and 625 each carry 2 extra samples.
int PalLineSampleOffsetFixture(int line_1based) {
  if (line_1based <= 312) {
    return (line_1based - 1) * 1135;
  }
  if (line_1based == 313) {
    return 312 * 1135;
  }
  if (line_1based <= 624) {
    return 312 * 1135 + 1137 + (line_1based - 314) * 1135;
  }
  return 312 * 1135 + 1137 + 311 * 1135;
}

TEST(ProjectFixturesTest, PalPilotBurstFixtureParsesValidatesAndGenerated) {
  YamlProjectParser parser;
  ProjectValidator validator;
  GenerationStage generation;

  const ParseResult parsed =
      parser.ParseFile(FixturePath("general/pal_pilot_burst.yaml"));
  ASSERT_TRUE(parsed.ok);

  Project project = parsed.project;
  ResolveProgressiveSourcePaths(&project);

  EXPECT_EQ(project.cvbs_presets.video_standard_preset, Standard::kPal);
  EXPECT_TRUE(project.cvbs_presets.pal_laserdisc_pilot_burst);
  ASSERT_EQ(project.sections.size(), 6U);

  const ValidationResult validation = validator.Validate(project);
  ASSERT_TRUE(validation.is_valid)
      << (!validation.errors.empty() ? validation.errors[0]
                                     : "unknown validation error");

  std::vector<SampleFixed> y_mv;
  std::vector<SampleFixed> c_mv;
  std::vector<std::string> errors;
  ASSERT_TRUE(generation.Generate(project, &y_mv, &c_mv, &errors));
  ASSERT_TRUE(errors.empty());

  const TimingConstants timing = GetTimingConstants(Standard::kPal);
  const std::size_t expected_samples =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal)) * 24U;
  EXPECT_EQ(y_mv.size(), expected_samples);

  // Verify the pilot burst is active: flat sync-pulse region on line 10 of
  // frame 1 must oscillate, not be constant at sync tip.
  const int line_offset = PalLineSampleOffsetFixture(10);
  const int pulse_samples =
      static_cast<int>(std::lround(timing.sample_rate_4fsc_hz * 4.7e-6));
  const int ramp_samples =
      RiseTimeToRampSamples(200.0e-9, timing.sample_rate_4fsc_hz);
  const int flat_start = line_offset + ramp_samples;
  const int flat_end = line_offset + pulse_samples - ramp_samples;
  ASSERT_LT(flat_start, flat_end);

  const SampleFixed first = y_mv[static_cast<std::size_t>(flat_start)];
  bool oscillates = false;
  for (int i = flat_start + 1; i < flat_end; ++i) {
    if (y_mv[static_cast<std::size_t>(i)] != first) {
      oscillates = true;
      break;
    }
  }
  EXPECT_TRUE(oscillates)
      << "pal_pilot_burst.yaml: sync pulse must carry the 3.75 MHz pilot burst";
}

TEST(ProjectFixturesTest,
     PalPilotBurstFixtureGeneratesCompositeOutputWithCorrectFrameCount) {
  YamlProjectParser parser;
  ProjectValidator validator;
  GenerationStage generation;
  OutputStage output;

  const ParseResult parsed =
      parser.ParseFile(FixturePath("general/pal_pilot_burst.yaml"));
  ASSERT_TRUE(parsed.ok);

  Project project = parsed.project;
  project.output.video_path =
      ResolveFixtureOutputPath(project.output.video_path).string();
  project.output.metadata_path =
      ResolveFixtureOutputPath(project.output.metadata_path).string();
  ResolveProgressiveSourcePaths(&project);
  ASSERT_TRUE(validator.Validate(project).is_valid);

  std::vector<SampleFixed> y_mv;
  std::vector<SampleFixed> c_mv;
  std::vector<std::string> generation_errors;
  ASSERT_TRUE(generation.Generate(project, &y_mv, &c_mv, &generation_errors));

  const std::filesystem::path output_path = project.output.video_path;
  const std::filesystem::path metadata_path = project.output.metadata_path;
  std::filesystem::create_directories(output_path.parent_path());
  std::filesystem::remove(output_path);
  std::filesystem::remove(metadata_path);

  std::vector<std::string> output_errors;
  ASSERT_TRUE(output.Write(project, y_mv, c_mv, &output_errors));

  // 6 sections × 4 frames each = 24 frames total.
  int64_t frame_count = 0;
  ASSERT_TRUE(QueryCvbsMetadataFrameCount(metadata_path, &frame_count));
  EXPECT_EQ(frame_count, 24LL);

  std::filesystem::remove(output_path);
  std::filesystem::remove(metadata_path);
}

}  // namespace
}  // namespace videosynth