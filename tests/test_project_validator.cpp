/*
 * File:        test_project_validator.cpp
 * Module:      project_validator_tests
 * Purpose:     Validates project constraint enforcement and error reporting.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "videosynth/project_validator.h"

namespace videosynth {
namespace {

Project MakeValidProject() {
  Project project;
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  project.output.video_path = "/tmp/videosynth_validator_test.composite";
  project.output.metadata_path = "/tmp/videosynth_validator_test.meta";
  project.sections.push_back(
      Section{.name = "Bars",
              .type = "software_generated",
          .pattern = "pal_ebu_colour_bars_100",
              .duration_frames = 1});
  return project;
}

std::string CreateTemporarySourceFile(const std::string& file_name) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / file_name;
  std::ofstream stream(path);
  stream << "fixture";
  return path.string();
}

class MockProgressiveSourceProbe final : public IProgressiveSourceProbe {
 public:
  bool should_succeed = true;
  ProgressiveSourceProfile profile;
  std::string error_message;

  bool Probe(const Section&,
             ProgressiveSourceProfile* out_profile,
             std::string* error) override {
    if (!should_succeed) {
      if (error != nullptr) {
        *error = error_message;
      }
      return false;
    }

    if (out_profile != nullptr) {
      *out_profile = profile;
    }
    return true;
  }
};

TEST(ProjectValidatorTest, AcceptsMvpCompliantProject) {
  ProjectValidator validator;
  const ValidationResult result = validator.Validate(MakeValidProject());

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST(ProjectValidatorTest, AcceptsTpg21SampleEncodingPreset) {
  Project project = MakeValidProject();
  project.cvbs_presets.sample_encoding_preset = "CVBS_TPG21_4FSC";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsInvalidStandard) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kUnknown;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  EXPECT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsSampleRateOtherThan4fsc) {
  Project project = MakeValidProject();
  project.cvbs_presets.sample_encoding_preset = "RAW_S16_40M";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, RejectsSubcarrierLockDisabled) {
  Project project = MakeValidProject();
  project.cvbs_presets.signal_state_preset = "NONSTANDARD_RAW";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, RejectsNonSoftwareSections) {
  Project project = MakeValidProject();
  project.sections[0].type = "progressive";
  project.sections[0].pattern.clear();

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, AcceptsProgressivePngSectionWithFixedDuration) {
  Project project = MakeValidProject();
  project.sections[0].type = "progressive";
  project.sections[0].pattern.clear();
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive.png");
  project.sections[0].duration_frames = 8;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, RejectsProgressiveSectionWithoutSource) {
  Project project = MakeValidProject();
  project.sections[0].type = "progressive";
  project.sections[0].pattern.clear();
  project.sections[0].source.clear();

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, AcceptsProgressiveRawWithSupportedPixelFormat) {
  Project project = MakeValidProject();
  project.sections[0].type = "progressive";
  project.sections[0].pattern.clear();
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive.raw");
  project.sections[0].source_pixel_format = "yuv422p10le";
  project.sections[0].duration_frames = 8;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, RejectsProgressiveRawWithoutPixelFormat) {
  Project project = MakeValidProject();
  project.sections[0].type = "progressive";
  project.sections[0].pattern.clear();
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive.raw");
  project.sections[0].source_pixel_format.clear();

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, RejectsSoftwareGeneratedAllDurationSemantics) {
  Project project = MakeValidProject();
  project.sections[0].duration_frames_all = true;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, RejectsMissingPatternOnSoftwareSection) {
  Project project = MakeValidProject();
  project.sections[0].pattern.clear();

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, RejectsUnsupportedPattern) {
  Project project = MakeValidProject();
  project.sections[0].pattern = "invalid_pattern_name";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, RejectsNtscBarsPatternInPalProject) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.sections[0].pattern = "ntsc_smpte_170m_colour_bars_100";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, RejectsPalBarsPatternInNtscProject) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.sections[0].pattern = "pal_ebu_colour_bars_100";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, RejectsMissingDurationFrames) {
  Project project = MakeValidProject();
  project.sections[0].duration_frames = 0;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, AcceptsProgressiveMp4WithSupportedProfile) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.sections[0].type = "progressive";
  project.sections[0].pattern.clear();
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive_ok.mp4");
  project.sections[0].duration_frames_all = true;
  project.sections[0].duration_frames = 0;

  MockProgressiveSourceProbe probe;
  probe.profile.container = "mov,mp4,m4a,3gp,3g2,mj2";
  probe.profile.codec = "h264";
  probe.profile.pixel_format = "yuv420p";
  probe.profile.bit_depth = 8;
  probe.profile.width = 720;
  probe.profile.height = 576;
  probe.profile.frame_rate_hz = 25.0;
  probe.profile.frame_count = 100;

  ProjectValidator validator(&probe);
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, RejectsProgressiveMp4WithUnsupportedPixelFormat) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.sections[0].type = "progressive";
  project.sections[0].pattern.clear();
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive_bad_pixfmt.mp4");
  project.sections[0].duration_frames_all = true;
  project.sections[0].duration_frames = 0;

  MockProgressiveSourceProbe probe;
  probe.profile.container = "mov,mp4,m4a,3gp,3g2,mj2";
  probe.profile.codec = "h264";
  probe.profile.pixel_format = "yuv420p10le";
  probe.profile.bit_depth = 10;
  probe.profile.width = 720;
  probe.profile.height = 480;
  probe.profile.frame_rate_hz = 30000.0 / 1001.0;
  probe.profile.frame_count = 200;

  ProjectValidator validator(&probe);
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());

  std::filesystem::remove(project.sections[0].source);
}

}  // namespace
}  // namespace videosynth
