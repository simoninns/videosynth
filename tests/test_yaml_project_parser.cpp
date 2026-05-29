/*
 * File:        test_yaml_project_parser.cpp
 * Module:      yaml_project_parser_tests
 * Purpose:     Validates progressive section parsing and duration semantics.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "videosynth/yaml_project_parser.h"

namespace videosynth {
namespace {

std::string WriteTempYaml(const std::string& file_name, const std::string& yaml) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / file_name;
  std::ofstream stream(path);
  stream << yaml;
  return path.string();
}

TEST(YamlProjectParserTest, ParsesProgressiveSectionWithAllDuration) {
  const std::string path = WriteTempYaml(
      "videosynth_parser_progressive_all.yaml",
      "project:\n"
      "  name: ProgressiveAll\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: ProgressiveVideo\n"
      "    type: progressive\n"
      "    source: fixture.mp4\n"
      "    duration_frames: all\n");

  YamlProjectParser parser;
  const ParseResult result = parser.ParseFile(path);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  EXPECT_TRUE(result.project.sections[0].duration_frames_all);
  EXPECT_EQ(result.project.sections[0].duration_frames, 0);

  std::filesystem::remove(path);
}

TEST(YamlProjectParserTest, ParsesProgressiveSectionWithIntegerDuration) {
  const std::string path = WriteTempYaml(
      "videosynth_parser_progressive_int.yaml",
      "project:\n"
      "  name: ProgressiveFixed\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: NTSC\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: ProgressiveStill\n"
      "    type: progressive\n"
      "    source: fixture.png\n"
      "    duration_frames: 8\n");

  YamlProjectParser parser;
  const ParseResult result = parser.ParseFile(path);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  EXPECT_FALSE(result.project.sections[0].duration_frames_all);
  EXPECT_EQ(result.project.sections[0].duration_frames, 8);

  std::filesystem::remove(path);
}

TEST(YamlProjectParserTest, ParsesOptionalNtscBlackSetupIre) {
  const std::string path = WriteTempYaml(
      "videosynth_parser_ntsc_black_setup.yaml",
      "project:\n"
      "  name: NtscZeroSetup\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: NTSC\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "  ntsc_black_setup_ire: 0.0\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: ProgressiveStill\n"
      "    type: progressive\n"
      "    source: fixture.png\n"
      "    duration_frames: 8\n");

  YamlProjectParser parser;
  const ParseResult result = parser.ParseFile(path);

  ASSERT_TRUE(result.ok);
  EXPECT_TRUE(result.project.cvbs_presets.ntsc_black_setup_ire_specified);
  EXPECT_DOUBLE_EQ(result.project.cvbs_presets.ntsc_black_setup_ire, 0.0);

  std::filesystem::remove(path);
}

TEST(YamlProjectParserTest, RejectsInvalidDurationFramesScalar) {
  const std::string path = WriteTempYaml(
      "videosynth_parser_progressive_bad_duration.yaml",
      "project:\n"
      "  name: ProgressiveInvalid\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: ProgressiveBad\n"
      "    type: progressive\n"
      "    source: fixture.mov\n"
      "    duration_frames: forever\n");

  YamlProjectParser parser;
  const ParseResult result = parser.ParseFile(path);

  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.errors.empty());

  std::filesystem::remove(path);
}

TEST(YamlProjectParserTest, ParsesLineInjectionsAndLaserdiscPresetFlags) {
  const std::string path = WriteTempYaml(
      "videosynth_parser_line_injections.yaml",
      "project:\n"
      "  name: InjectionParse\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "  pal_laserdisc_pilot_burst: true\n"
      "  ntsc_laserdisc_vbi_burst: false\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: GeneratedWithInjections\n"
      "    type: software_generated\n"
      "    pattern: pal_ebu_colour_bars_100\n"
      "    duration_frames: 4\n"
      "    line_injections:\n"
      "      - type: vits\n"
      "        target_lines: [19, 20]\n"
      "        vits_type: virs\n"
      "      - type: laserdisc\n"
      "        disc_type: CAV\n"
      "        codes:\n"
      "          - code_type: picture_number\n"
      "            start_value: 1\n");

  YamlProjectParser parser;
  const ParseResult result = parser.ParseFile(path);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  EXPECT_TRUE(result.project.cvbs_presets.pal_laserdisc_pilot_burst);
  EXPECT_FALSE(result.project.cvbs_presets.ntsc_laserdisc_vbi_burst);
  ASSERT_EQ(result.project.sections[0].line_injections.size(), 2U);
  EXPECT_EQ(result.project.sections[0].line_injections[0].type, "vits");
  EXPECT_EQ(result.project.sections[0].line_injections[0].target_lines.size(), 2U);
  EXPECT_EQ(result.project.sections[0].line_injections[0].target_lines[0], 19);
  EXPECT_EQ(result.project.sections[0].line_injections[1].type, "laserdisc");
  ASSERT_EQ(result.project.sections[0].line_injections[1].codes.size(), 1U);
  EXPECT_EQ(result.project.sections[0].line_injections[1].codes[0].code_type, "picture_number");
  EXPECT_TRUE(result.project.sections[0].line_injections[1].codes[0].start_value_specified);
  EXPECT_EQ(result.project.sections[0].line_injections[1].codes[0].start_value, 1);

  std::filesystem::remove(path);
}

TEST(YamlProjectParserTest, RejectsUnsupportedLineInjectionField) {
  const std::string path = WriteTempYaml(
      "videosynth_parser_line_injections_unsupported_field.yaml",
      "project:\n"
      "  name: InjectionInvalid\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: GeneratedWithInjections\n"
      "    type: software_generated\n"
      "    pattern: pal_ebu_colour_bars_100\n"
      "    duration_frames: 4\n"
      "    line_injections:\n"
      "      - type: vits\n"
      "        unsupported_field: true\n");

  YamlProjectParser parser;
  const ParseResult result = parser.ParseFile(path);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.errors.empty());

  std::filesystem::remove(path);
}

TEST(YamlProjectParserTest, RejectsDeprecatedSourcePixelFormatField) {
  const std::string path = WriteTempYaml(
      "videosynth_parser_rejects_source_pixel_format.yaml",
      "project:\n"
      "  name: DeprecatedField\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: ProgressiveStill\n"
      "    type: progressive\n"
      "    source: fixture.exr\n"
      "    source_pixel_format: yuv422p10le\n"
      "    duration_frames: 8\n");

  YamlProjectParser parser;
  const ParseResult result = parser.ParseFile(path);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("unsupported field"), std::string::npos);
  EXPECT_NE(result.errors[0].find("source_pixel_format"), std::string::npos);

  std::filesystem::remove(path);
}

}  // namespace
}  // namespace videosynth
