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

#include <gtest/gtest.h>

#include "videosynth/generation_stage.h"
#include "videosynth/output_stage.h"
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

    ASSERT_EQ(parsed.project.sections.size(), 2U);
    EXPECT_EQ(parsed.project.sections[0].duration_frames, 16);
    EXPECT_EQ(parsed.project.sections[1].duration_frames, 16);
  }
}

TEST(ProjectFixturesTest, FixtureProjectsGenerateCompositeOutputWith32Frames) {
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

    std::vector<double> y_mv;
    std::vector<double> c_mv;
    std::vector<std::string> generation_errors;
    ASSERT_TRUE(
      generation.Generate(project, &y_mv, &c_mv, &generation_errors))
        << fixture;

    const TimingConstants timing =
      GetTimingConstants(project.cvbs_presets.video_standard_preset);
    const std::size_t frame_span = static_cast<std::size_t>(
        SamplesPerFrame4fsc(project.cvbs_presets.video_standard_preset));
    ASSERT_EQ(y_mv.size(), frame_span * 32U) << fixture;
    ASSERT_EQ(c_mv.size(), y_mv.size()) << fixture;

    const std::filesystem::path output_path = project.output.video_path;
    const std::filesystem::path metadata_path = project.output.metadata_path;
    std::filesystem::create_directories(output_path.parent_path());
    std::filesystem::remove(output_path);
    std::filesystem::remove(metadata_path);

    std::vector<std::string> output_errors;
    ASSERT_TRUE(output.Write(project, y_mv, c_mv, &output_errors))
        << fixture;

    const std::string metadata = ReadTextFile(metadata_path);
    EXPECT_NE(metadata.find("signal_type=composite"), std::string::npos) << fixture;
    EXPECT_NE(metadata.find("sample_encoding_preset=" + project.cvbs_presets.sample_encoding_preset),
          std::string::npos)
      << fixture;
    EXPECT_NE(metadata.find("frame_count=32"), std::string::npos) << fixture;
    EXPECT_NE(metadata.find("sample_count=" + std::to_string(y_mv.size())), std::string::npos)
        << fixture;

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
      {"pal_32f_bars_ramp.yaml", 3183976727390330307ULL},
      {"ntsc_32f_bars_ramp.yaml", 4652568662636059011ULL},
  };

  for (const FixtureExpectation& expectation : expectations) {
    const ParseResult parsed = parser.ParseFile(FixturePath(expectation.fixture));
    ASSERT_TRUE(parsed.ok) << expectation.fixture;
    Project project = parsed.project;
    project.output.video_path = ResolveFixtureOutputPath(project.output.video_path).string();
    project.output.metadata_path =
      ResolveFixtureOutputPath(project.output.metadata_path).string();
    ASSERT_TRUE(validator.Validate(project).is_valid) << expectation.fixture;

    std::vector<double> y_mv;
    std::vector<double> c_mv;
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