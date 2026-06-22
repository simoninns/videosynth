/*
 * File:        test_yaml_project_parser.cpp
 * Module:      yaml_project_parser_tests
 * Purpose:     Validates progressive section parsing and duration semantics.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <string>

#include "videosynth/yaml_project_parser.h"

namespace videosynth {
namespace {

TEST(YamlProjectParserTest, ParsesProgressiveSectionWithAllDuration) {
  const std::string yaml =
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
      "    source: fixture.mkv\n"
      "    duration_frames: all\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  EXPECT_TRUE(result.project.sections[0].duration_frames_all);
  EXPECT_EQ(result.project.sections[0].duration_frames, 0);
}

TEST(YamlProjectParserTest, ParsesProgressiveSectionWithIntegerDuration) {
  const std::string yaml =
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
      "    source: fixture.exr\n"
      "    duration_frames: 8\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  EXPECT_FALSE(result.project.sections[0].duration_frames_all);
  EXPECT_EQ(result.project.sections[0].duration_frames, 8);
}

TEST(YamlProjectParserTest, ParsesOptionalNtscBlackSetupIre) {
  const std::string yaml =
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
      "    source: fixture.exr\n"
      "    duration_frames: 8\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  EXPECT_TRUE(result.project.cvbs_presets.ntsc_black_setup_ire_specified);
  EXPECT_DOUBLE_EQ(result.project.cvbs_presets.ntsc_black_setup_ire, 0.0);
}

TEST(YamlProjectParserTest, RejectsInvalidDurationFramesScalar) {
  const std::string yaml =
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
      "    source: fixture.mkv\n"
      "    duration_frames: forever\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.errors.empty());
}

TEST(YamlProjectParserTest, ParsesLineInjectionsAndLaserdiscPresetFlags) {
  const std::string yaml =
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
      "  - name: ProgressiveWithInjections\n"
      "    type: progressive\n"
      "    source: fixture.exr\n"
      "    duration_frames: 4\n"
      "    line_injections:\n"
      "      - type: vits\n"
      "        target_lines: [19, 20]\n"
      "        vits_type: virs\n"
      "      - type: laserdisc\n"
      "        disc_type: CAV\n"
      "        codes:\n"
      "          - code_type: picture_number\n"
      "            start_value: 1\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  EXPECT_TRUE(result.project.cvbs_presets.pal_laserdisc_pilot_burst);
  EXPECT_FALSE(result.project.cvbs_presets.ntsc_laserdisc_vbi_burst);
  ASSERT_EQ(result.project.sections[0].line_injections.size(), 2U);
  EXPECT_EQ(result.project.sections[0].line_injections[0].type, "vits");
  EXPECT_EQ(result.project.sections[0].line_injections[0].target_lines.size(),
            2U);
  EXPECT_EQ(result.project.sections[0].line_injections[0].target_lines[0], 19);
  EXPECT_EQ(result.project.sections[0].line_injections[1].type, "laserdisc");
  ASSERT_EQ(result.project.sections[0].line_injections[1].codes.size(), 1U);
  EXPECT_EQ(result.project.sections[0].line_injections[1].codes[0].code_type,
            "picture_number");
  EXPECT_TRUE(result.project.sections[0]
                  .line_injections[1]
                  .codes[0]
                  .start_value_specified);
  EXPECT_EQ(result.project.sections[0].line_injections[1].codes[0].start_value,
            1);
}

TEST(YamlProjectParserTest, RejectsUnsupportedLineInjectionField) {
  const std::string yaml =
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
      "  - name: ProgressiveWithInjections\n"
      "    type: progressive\n"
      "    source: fixture.exr\n"
      "    duration_frames: 4\n"
      "    line_injections:\n"
      "      - type: vits\n"
      "        unsupported_field: true\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.errors.empty());
}

TEST(YamlProjectParserTest, RejectsDeprecatedSourcePixelFormatField) {
  const std::string yaml =
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
      "    duration_frames: 8\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("unsupported field"), std::string::npos);
  EXPECT_NE(result.errors[0].find("source_pixel_format"), std::string::npos);
}

TEST(YamlProjectParserTest, ParsesSectionTypeLeadIn) {
  const std::string yaml =
      "project:\n"
      "  name: SectionTypeTest\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: LeadInSection\n"
      "    type: progressive\n"
      "    source: fixture.exr\n"
      "    duration_frames: 8\n"
      "    section_type: lead_in\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  EXPECT_EQ(result.project.sections[0].section_type, SectionType::kLeadIn);
}

TEST(YamlProjectParserTest, ParsesSectionTypeProgrammeArea) {
  const std::string yaml =
      "project:\n"
      "  name: SectionTypeTest\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: ProgrammeSection\n"
      "    type: progressive\n"
      "    source: fixture.exr\n"
      "    duration_frames: 8\n"
      "    section_type: programme_area\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  EXPECT_EQ(result.project.sections[0].section_type,
            SectionType::kProgrammeArea);
}

TEST(YamlProjectParserTest, ParsesSectionTypeLeadOut) {
  const std::string yaml =
      "project:\n"
      "  name: SectionTypeTest\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: LeadOutSection\n"
      "    type: progressive\n"
      "    source: fixture.exr\n"
      "    duration_frames: 8\n"
      "    section_type: lead_out\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  EXPECT_EQ(result.project.sections[0].section_type, SectionType::kLeadOut);
}

TEST(YamlProjectParserTest, SectionWithNoSectionTypeDefaultsToUnknown) {
  const std::string yaml =
      "project:\n"
      "  name: SectionTypeTest\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: DefaultSection\n"
      "    type: progressive\n"
      "    source: fixture.exr\n"
      "    duration_frames: 8\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  EXPECT_EQ(result.project.sections[0].section_type, SectionType::kUnknown);
}

TEST(YamlProjectParserTest, RejectsUnrecognisedSectionTypeValue) {
  const std::string yaml =
      "project:\n"
      "  name: SectionTypeTest\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: BadSection\n"
      "    type: progressive\n"
      "    source: fixture.exr\n"
      "    duration_frames: 8\n"
      "    section_type: programme\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("section_type"), std::string::npos);
  EXPECT_NE(result.errors[0].find("programme"), std::string::npos);
}

TEST(YamlProjectParserTest, ParsesOsdOverlayBlock) {
  const std::string yaml =
      "project:\n"
      "  name: OsdTest\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: OsdSection\n"
      "    type: progressive\n"
      "    source: fixture.exr\n"
      "    duration_frames: 4\n"
      "    osd:\n"
      "      overlays:\n"
      "        - text: \"LABEL\"\n"
      "          x: 10\n"
      "          y: 5\n"
      "          scale: 2\n"
      "          fg_luma: 1.0\n"
      "          bg_luma: 0.0\n"
      "        - text: \"PN:{picture_number}\"\n"
      "          x: 0\n"
      "          y: 20\n"
      "          scale: 1\n"
      "          fg_luma: 0.8\n"
      "          bg_luma: -1.0\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  const OsdConfig& osd = result.project.sections[0].osd;
  ASSERT_EQ(osd.overlays.size(), 2U);

  EXPECT_EQ(osd.overlays[0].text, "LABEL");
  EXPECT_EQ(osd.overlays[0].x, 10);
  EXPECT_EQ(osd.overlays[0].y, 5);
  EXPECT_EQ(osd.overlays[0].scale, 2);
  EXPECT_DOUBLE_EQ(osd.overlays[0].fg_luma, 1.0);
  EXPECT_DOUBLE_EQ(osd.overlays[0].bg_luma, 0.0);

  EXPECT_EQ(osd.overlays[1].text, "PN:{picture_number}");
  EXPECT_EQ(osd.overlays[1].x, 0);
  EXPECT_EQ(osd.overlays[1].y, 20);
  EXPECT_EQ(osd.overlays[1].scale, 1);
  EXPECT_DOUBLE_EQ(osd.overlays[1].fg_luma, 0.8);
  EXPECT_DOUBLE_EQ(osd.overlays[1].bg_luma, -1.0);
}

TEST(YamlProjectParserTest, OsdOverlayDefaultsApplied) {
  const std::string yaml =
      "project:\n"
      "  name: OsdDefaults\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: OsdSection\n"
      "    type: progressive\n"
      "    source: fixture.exr\n"
      "    duration_frames: 4\n"
      "    osd:\n"
      "      overlays:\n"
      "        - text: \"ONLY TEXT\"\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  const OsdConfig& osd = result.project.sections[0].osd;
  ASSERT_EQ(osd.overlays.size(), 1U);
  EXPECT_EQ(osd.overlays[0].x, 0);
  EXPECT_EQ(osd.overlays[0].y, 0);
  EXPECT_EQ(osd.overlays[0].scale, 1);
  EXPECT_DOUBLE_EQ(osd.overlays[0].fg_luma, 1.0);
  EXPECT_DOUBLE_EQ(osd.overlays[0].bg_luma, -1.0);
}

TEST(YamlProjectParserTest, OsdBlockNotAMapReturnsError) {
  const std::string yaml =
      "project:\n"
      "  name: OsdBad\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: OsdSection\n"
      "    type: progressive\n"
      "    source: fixture.exr\n"
      "    duration_frames: 4\n"
      "    osd: \"not a map\"\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("osd"), std::string::npos);
}

TEST(YamlProjectParserTest, OsdOverlaysNotAListReturnsError) {
  const std::string yaml =
      "project:\n"
      "  name: OsdBad\n"
      "  version: \"1.0\"\n"
      "cvbs_presets:\n"
      "  video_standard_preset: PAL\n"
      "  sample_encoding_preset: CVBS_U10_4FSC\n"
      "  signal_state_preset: STANDARD_TBC_LOCKED\n"
      "output:\n"
      "  video_path: out.composite\n"
      "  metadata_path: out.meta\n"
      "sections:\n"
      "  - name: OsdSection\n"
      "    type: progressive\n"
      "    source: fixture.exr\n"
      "    duration_frames: 4\n"
      "    osd:\n"
      "      overlays: scalar\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("overlays"), std::string::npos);
}

}  // namespace
}  // namespace videosynth
