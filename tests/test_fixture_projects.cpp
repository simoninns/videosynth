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

#include "videosynth/generation_stage.h"
#include "videosynth/output_stage.h"
#include "videosynth/progressive_frame_source.h"
#include "videosynth/progressive_frame_source_probe.h"
#include "videosynth/project_validator.h"
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

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream stream(path);
  return std::string((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(stream)),
                                   std::istreambuf_iterator<char>());
}

std::uint64_t Fnv1a64(const std::vector<std::uint8_t>& bytes) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (std::uint8_t b : bytes) {
    hash ^= static_cast<std::uint64_t>(b);
    hash *= 1099511628211ULL;
  }
  return hash;
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

  const std::vector<std::string> fixtures = {"pal_progressive_exr.yaml",
                                             "ntsc_progressive_exr.yaml"};

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

  const std::vector<std::string> fixtures = {"pal_progressive_mkv.yaml",
                                             "ntsc_progressive_mkv.yaml"};

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

  const std::vector<std::string> fixtures = {"pal_progressive_exr.yaml",
                                             "ntsc_progressive_exr.yaml"};

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

  const std::vector<std::string> fixtures = {"pal_progressive_mkv.yaml",
                                             "ntsc_progressive_mkv.yaml"};

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
      .fixture_name = "pal_vits.yaml",
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
      .fixture_name = "ntsc_vits.yaml",
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

  const std::vector<std::string> fixtures = {"pal_progressive_exr.yaml",
                                             "ntsc_progressive_exr.yaml"};
  const std::vector<std::string> output_presets = {
      "CVBS_U10_4FSC", "CVBS_U16_4FSC", "CVBS_TPG21_4FSC", "RAW_S16_28M",
      "RAW_S16_40M"};

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

      const std::filesystem::path output_path =
          std::filesystem::temp_directory_path() /
          ("videosynth_stage4_" + fixture + "_" + output_preset + ".composite");
      const std::filesystem::path metadata_path =
          std::filesystem::temp_directory_path() /
          ("videosynth_stage4_" + fixture + "_" + output_preset + ".meta");
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

}  // namespace
}  // namespace videosynth