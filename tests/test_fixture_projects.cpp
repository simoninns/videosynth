/*
 * File:        test_fixture_projects.cpp
 * Module:      fixture_project_tests
 * Purpose:     Verifies PAL/NTSC fixture projects through full generation and output checks.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <gtest/gtest.h>

#include "videosynth/generation_stage.h"
#include "videosynth/progressive_frame_source.h"
#include "videosynth/output_stage.h"
#include "videosynth/progressive_frame_source_probe.h"
#include "videosynth/project_validator.h"
#include "videosynth/timing_constants.h"
#include "videosynth/yaml_project_parser.h"

namespace videosynth {
namespace {

std::string FixturePath(const std::string& fixture_name) {
  return std::string(VIDEOSYNTH_SOURCE_DIR) + "/tests/projects/" + fixture_name;
}

std::filesystem::path ResolveFixtureOutputPath(const std::string& configured_path) {
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
      section.source = (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) / source_path).string();
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

bool QueryCvbsMetadataFrameCount(const std::filesystem::path& path, int64_t* frame_count) {
  if (frame_count == nullptr) {
    return false;
  }

  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    sqlite3_close(db);
    return false;
  }

  const char* query_sql = "SELECT number_of_sequential_frames FROM cvbs_file LIMIT 1;";
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

TEST(ProjectFixturesTest, PalAndNtscProjectsParseAndValidate) {
  YamlProjectParser parser;
  ProjectValidator validator;

  const std::vector<std::string> fixtures = {
      "pal_32f_bars_ramp.yaml", "ntsc_32f_bars_ramp.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;

    const ValidationResult validation = validator.Validate(parsed.project);
    ASSERT_TRUE(validation.is_valid) << fixture;

    ASSERT_EQ(parsed.project.sections.size(), 10U);
    for (const Section& section : parsed.project.sections) {
      EXPECT_EQ(section.duration_frames, 8);
    }
  }
}

TEST(ProjectFixturesTest, ProgressivePngFixturesParseAndValidate) {
  YamlProjectParser parser;
  ProgressiveFrameSourceProbe progressive_frame_source_probe;
  ProjectValidator validator(&progressive_frame_source_probe);

  const std::vector<std::string> fixtures = {
      "pal_progressive_png.yaml", "ntsc_progressive_png.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;

    Project project = parsed.project;
    ResolveProgressiveSourcePaths(&project);
    const ValidationResult validation = validator.Validate(project);
    ASSERT_TRUE(validation.is_valid) << fixture;

    ASSERT_EQ(project.sections.size(), 9U);
    for (const Section& section : project.sections) {
      EXPECT_EQ(section.type, "progressive");
      EXPECT_EQ(section.duration_frames, 8);
      EXPECT_FALSE(section.duration_frames_all);
    }
  }
}

TEST(ProjectFixturesTest, ProgressiveRawFixturesParseAndValidate) {
  YamlProjectParser parser;
  ProgressiveFrameSourceProbe progressive_frame_source_probe;
  ProjectValidator validator(&progressive_frame_source_probe);

  const std::vector<std::string> fixtures = {
      "pal_progressive_raw.yaml", "ntsc_progressive_raw.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;

    Project project = parsed.project;
    ResolveProgressiveSourcePaths(&project);
    const ValidationResult validation = validator.Validate(project);
    ASSERT_TRUE(validation.is_valid) << fixture;

    ASSERT_EQ(project.sections.size(), 19U);
    for (const Section& section : project.sections) {
      EXPECT_EQ(section.type, "progressive");
      EXPECT_EQ(section.source_pixel_format, "yuv422p10le");
      EXPECT_EQ(section.duration_frames, 8);
      EXPECT_FALSE(section.duration_frames_all);
    }
  }
}

TEST(ProjectFixturesTest, ProgressiveMp4FixturesParseAndValidate) {
  YamlProjectParser parser;
  ProgressiveFrameSourceProbe progressive_frame_source_probe;
  ProjectValidator validator(&progressive_frame_source_probe);

  const std::vector<std::string> fixtures = {
      "pal_progressive_mp4.yaml", "ntsc_progressive_mp4.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;

    Project project = parsed.project;
    ResolveProgressiveSourcePaths(&project);
    const ValidationResult validation = validator.Validate(project);
    ASSERT_TRUE(validation.is_valid) << fixture;

    ASSERT_EQ(project.sections.size(), 1U);
    EXPECT_EQ(project.sections[0].type, "progressive");
    EXPECT_FALSE(project.sections[0].duration_frames_all);
    const int expected_duration_frames =
        fixture == "pal_progressive_mp4.yaml" ? 50 : 60;
    EXPECT_EQ(project.sections[0].duration_frames, expected_duration_frames);
  }
}

TEST(ProjectFixturesTest, ProgressiveMovFixturesParseAndValidate) {
  YamlProjectParser parser;
  ProgressiveFrameSourceProbe progressive_frame_source_probe;
  ProjectValidator validator(&progressive_frame_source_probe);

  const std::vector<std::string> fixtures = {
      "pal_progressive_mov.yaml", "ntsc_progressive_mov.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;

    Project project = parsed.project;
    ResolveProgressiveSourcePaths(&project);
    const ValidationResult validation = validator.Validate(project);
    ASSERT_TRUE(validation.is_valid) << fixture;

    const std::size_t expected_section_count =
        fixture == "pal_progressive_mov.yaml" ? 3U : 1U;
    ASSERT_EQ(project.sections.size(), expected_section_count);
    for (const Section& section : project.sections) {
      EXPECT_EQ(section.type, "progressive");
      EXPECT_TRUE(section.duration_frames_all);
      EXPECT_EQ(section.duration_frames, 0);
    }
  }
}

TEST(ProjectFixturesTest, FixtureProjectsGenerateCompositeOutputWith80Frames) {
  YamlProjectParser parser;
  ProjectValidator validator;
  GenerationStage generation;
  OutputStage output;

  const std::vector<std::string> fixtures = {
      "pal_32f_bars_ramp.yaml", "ntsc_32f_bars_ramp.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;
    Project project = parsed.project;
    project.output.video_path = ResolveFixtureOutputPath(project.output.video_path).string();
    project.output.metadata_path =
      ResolveFixtureOutputPath(project.output.metadata_path).string();
    ASSERT_TRUE(validator.Validate(project).is_valid) << fixture;

    std::vector<SampleFixed> y_mv;
    std::vector<SampleFixed> c_mv;
    std::vector<std::string> generation_errors;
    ASSERT_TRUE(
      generation.Generate(project, &y_mv, &c_mv, &generation_errors))
        << fixture;

    const TimingConstants timing =
      GetTimingConstants(project.cvbs_presets.video_standard_preset);
    const std::size_t frame_span = static_cast<std::size_t>(
        SamplesPerFrame4fsc(project.cvbs_presets.video_standard_preset));
    ASSERT_EQ(y_mv.size(), frame_span * 80U) << fixture;
    ASSERT_EQ(c_mv.size(), y_mv.size()) << fixture;

    const std::filesystem::path output_path = project.output.video_path;
    const std::filesystem::path metadata_path = project.output.metadata_path;
    std::filesystem::create_directories(output_path.parent_path());
    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);

    std::vector<std::string> output_errors;
    ASSERT_TRUE(output.Write(project, y_mv, c_mv, &output_errors))
        << fixture;

    int64_t frame_count_from_metadata = 0;
    ASSERT_TRUE(QueryCvbsMetadataFrameCount(metadata_path, &frame_count_from_metadata)) << fixture;
    EXPECT_EQ(frame_count_from_metadata, 80) << fixture;

    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);
  }
}

TEST(ProjectFixturesTest, ProgressivePngFixturesGenerateCompositeOutputWith72Frames) {
  YamlProjectParser parser;
  ProjectValidator validator;
  GenerationStage generation;
  OutputStage output;

  const std::vector<std::string> fixtures = {
      "pal_progressive_png.yaml", "ntsc_progressive_png.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;
    Project project = parsed.project;
    project.output.video_path = ResolveFixtureOutputPath(project.output.video_path).string();
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
    ASSERT_EQ(y_mv.size(), frame_span * 72U) << fixture;
    ASSERT_EQ(c_mv.size(), y_mv.size()) << fixture;

    const std::filesystem::path output_path = project.output.video_path;
    const std::filesystem::path metadata_path = project.output.metadata_path;
    std::filesystem::create_directories(output_path.parent_path());
    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);

    std::vector<std::string> output_errors;
    ASSERT_TRUE(output.Write(project, y_mv, c_mv, &output_errors)) << fixture;

    int64_t frame_count_from_metadata = 0;
    ASSERT_TRUE(QueryCvbsMetadataFrameCount(metadata_path, &frame_count_from_metadata)) << fixture;
    EXPECT_EQ(frame_count_from_metadata, 72) << fixture;

    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);
  }
}

TEST(ProjectFixturesTest, ProgressiveRawFixturesGenerateCompositeOutputWith152Frames) {
  YamlProjectParser parser;
  ProjectValidator validator;
  GenerationStage generation;
  OutputStage output;

  const std::vector<std::string> fixtures = {
      "pal_progressive_raw.yaml", "ntsc_progressive_raw.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;
    Project project = parsed.project;
    project.output.video_path = ResolveFixtureOutputPath(project.output.video_path).string();
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
    ASSERT_EQ(y_mv.size(), frame_span * 152U) << fixture;
    ASSERT_EQ(c_mv.size(), y_mv.size()) << fixture;

    const std::filesystem::path output_path = project.output.video_path;
    const std::filesystem::path metadata_path = project.output.metadata_path;
    std::filesystem::create_directories(output_path.parent_path());
    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);

    std::vector<std::string> output_errors;
    ASSERT_TRUE(output.Write(project, y_mv, c_mv, &output_errors)) << fixture;

    int64_t frame_count_from_metadata = 0;
    ASSERT_TRUE(QueryCvbsMetadataFrameCount(metadata_path, &frame_count_from_metadata)) << fixture;
    EXPECT_EQ(frame_count_from_metadata, 152) << fixture;

    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);
  }
}

TEST(ProjectFixturesTest, ProgressiveMp4FixturesGenerateCompositeOutputForConfiguredDuration) {
  YamlProjectParser parser;
  ProgressiveFrameSourceProbe progressive_frame_source_probe;
  ProjectValidator validator(&progressive_frame_source_probe);
  GenerationStage generation;
  OutputStage output;
  ProgressiveFrameSource progressive_source;

  const std::vector<std::string> fixtures = {
      "pal_progressive_mp4.yaml", "ntsc_progressive_mp4.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;
    Project project = parsed.project;
    project.output.video_path = ResolveFixtureOutputPath(project.output.video_path).string();
    project.output.metadata_path =
      ResolveFixtureOutputPath(project.output.metadata_path).string();
    ResolveProgressiveSourcePaths(&project);
    ASSERT_TRUE(validator.Validate(project).is_valid) << fixture;

    int expected_section_frames = project.sections[0].duration_frames;
    if (project.sections[0].duration_frames_all) {
      std::string count_error;
      ASSERT_TRUE(progressive_source.ResolveFrameCount(project.sections[0],
                                                       project.cvbs_presets.video_standard_preset,
                                                       &expected_section_frames,
                                                       &count_error))
          << fixture << ": " << count_error;
    }
    ASSERT_GT(expected_section_frames, 0) << fixture;

    std::vector<SampleFixed> y_mv;
    std::vector<SampleFixed> c_mv;
    std::vector<std::string> generation_errors;
    ASSERT_TRUE(generation.Generate(project, &y_mv, &c_mv, &generation_errors))
        << fixture;

    const std::size_t frame_span = static_cast<std::size_t>(
        SamplesPerFrame4fsc(project.cvbs_presets.video_standard_preset));
    ASSERT_EQ(y_mv.size(), frame_span * static_cast<std::size_t>(expected_section_frames)) << fixture;
    ASSERT_EQ(c_mv.size(), y_mv.size()) << fixture;

    const std::filesystem::path output_path = project.output.video_path;
    const std::filesystem::path metadata_path = project.output.metadata_path;
    std::filesystem::create_directories(output_path.parent_path());
    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);

    std::vector<std::string> output_errors;
    ASSERT_TRUE(output.Write(project, y_mv, c_mv, &output_errors)) << fixture;

    int64_t frame_count_from_metadata = 0;
    ASSERT_TRUE(QueryCvbsMetadataFrameCount(metadata_path, &frame_count_from_metadata)) << fixture;
    EXPECT_EQ(frame_count_from_metadata, expected_section_frames) << fixture;

    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);
  }
}

TEST(ProjectFixturesTest, ProgressiveMovFixturesGenerateCompositeOutputForFullSourceLength) {
  YamlProjectParser parser;
  ProgressiveFrameSourceProbe progressive_frame_source_probe;
  ProjectValidator validator(&progressive_frame_source_probe);
  GenerationStage generation;
  OutputStage output;
  ProgressiveFrameSource progressive_source;

  const std::vector<std::string> fixtures = {
      "pal_progressive_mov.yaml", "ntsc_progressive_mov.yaml"};

  for (const std::string& fixture : fixtures) {
    const ParseResult parsed = parser.ParseFile(FixturePath(fixture));
    ASSERT_TRUE(parsed.ok) << fixture;
    Project project = parsed.project;
    project.output.video_path = ResolveFixtureOutputPath(project.output.video_path).string();
    project.output.metadata_path =
      ResolveFixtureOutputPath(project.output.metadata_path).string();
    ResolveProgressiveSourcePaths(&project);
    ASSERT_TRUE(validator.Validate(project).is_valid) << fixture;

    int expected_source_frames = 0;
    for (const Section& section : project.sections) {
      int section_source_frames = 0;
      std::string count_error;
      ASSERT_TRUE(progressive_source.ResolveFrameCount(section,
                                                       project.cvbs_presets.video_standard_preset,
                                                       &section_source_frames,
                                                       &count_error))
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
    ASSERT_EQ(y_mv.size(), frame_span * static_cast<std::size_t>(expected_source_frames)) << fixture;
    ASSERT_EQ(c_mv.size(), y_mv.size()) << fixture;

    const std::filesystem::path output_path = project.output.video_path;
    const std::filesystem::path metadata_path = project.output.metadata_path;
    std::filesystem::create_directories(output_path.parent_path());
    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);

    std::vector<std::string> output_errors;
    ASSERT_TRUE(output.Write(project, y_mv, c_mv, &output_errors)) << fixture;

    int64_t frame_count_from_metadata = 0;
    ASSERT_TRUE(QueryCvbsMetadataFrameCount(metadata_path, &frame_count_from_metadata)) << fixture;
    EXPECT_EQ(frame_count_from_metadata, expected_source_frames) << fixture;

    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);
  }
}

TEST(ProjectFixturesTest, FixtureOutputHashesRemainStable) {
  YamlProjectParser parser;
  ProjectValidator validator;
  GenerationStage generation;
  OutputStage output;

  struct FixtureExpectation {
    const char* fixture;
    std::uint64_t expected_hash;
  };

  const std::vector<FixtureExpectation> expectations = {
      {"pal_32f_bars_ramp.yaml", 9954163301157790659ULL},
      {"ntsc_32f_bars_ramp.yaml", 10524627689147863747ULL},
  };

  for (const FixtureExpectation& expectation : expectations) {
    const ParseResult parsed = parser.ParseFile(FixturePath(expectation.fixture));
    ASSERT_TRUE(parsed.ok) << expectation.fixture;
    Project project = parsed.project;
    project.output.video_path = ResolveFixtureOutputPath(project.output.video_path).string();
    project.output.metadata_path =
      ResolveFixtureOutputPath(project.output.metadata_path).string();
    ASSERT_TRUE(validator.Validate(project).is_valid) << expectation.fixture;

    std::vector<SampleFixed> y_mv;
    std::vector<SampleFixed> c_mv;
    std::vector<std::string> generation_errors;
    ASSERT_TRUE(
      generation.Generate(project, &y_mv, &c_mv, &generation_errors))
        << expectation.fixture;

    const std::filesystem::path output_path = project.output.video_path;
    const std::filesystem::path metadata_path = project.output.metadata_path;
    std::filesystem::create_directories(output_path.parent_path());
    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);

    std::vector<std::string> output_errors;
    ASSERT_TRUE(output.Write(project, y_mv, c_mv, &output_errors))
        << expectation.fixture;

    const std::vector<std::uint8_t> payload = ReadBinaryFile(output_path);
    const std::uint64_t hash = Fnv1a64(payload);
    EXPECT_EQ(hash, expectation.expected_hash) << expectation.fixture;

    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);
  }
}

}  // namespace
}  // namespace videosynth