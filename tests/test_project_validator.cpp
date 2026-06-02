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
#include <vector>

#include "videosynth/project_validator.h"

namespace videosynth {
namespace {

std::string DefaultProgressiveSourcePath() {
  return (std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) /
          "videosynth-assets/assets/exr/720x576/100_BARS.exr")
      .string();
}

Project MakeValidProject() {
  Project project;
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  project.output.video_path = "/tmp/videosynth_validator_test.composite";
  project.output.metadata_path = "/tmp/videosynth_validator_test.meta";
  project.sections.push_back(
      Section{.name = "Progressive",
          .type = "progressive",
          .source = DefaultProgressiveSourcePath(),
              .duration_frames = 1});
  return project;
}

std::string CreateTemporarySourceFile(const std::string& file_name) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / file_name;
  std::ofstream stream(path);
  stream << "fixture";
  return path.string();
}

class MockProgressiveFrameSourceProbe final : public IProgressiveFrameSourceProbe {
 public:
  bool should_succeed = true;
  ProgressiveFrameSourceProfile profile;
  std::string error_message;

  bool Probe(const Section&,
             ProgressiveFrameSourceProfile* out_profile,
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

TEST(ProjectValidatorTest, AcceptsSupportedSampleEncodingPresets) {
  const std::vector<std::string> presets = {
      "CVBS_U10_4FSC", "CVBS_U16_4FSC", "RAW_S16_28M", "RAW_S16_40M", "CVBS_TPG21_4FSC"};

  for (const std::string& preset : presets) {
    Project project = MakeValidProject();
    project.cvbs_presets.sample_encoding_preset = preset;

    ProjectValidator validator;
    const ValidationResult result = validator.Validate(project);

    EXPECT_TRUE(result.is_valid) << preset;
    EXPECT_TRUE(result.errors.empty()) << preset;
  }
}

TEST(ProjectValidatorTest, RejectsInvalidStandard) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kUnknown;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  EXPECT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsSubcarrierLockDisabled) {
  Project project = MakeValidProject();
  project.cvbs_presets.signal_state_preset = "NONSTANDARD_RAW";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, AcceptsSupportedNtscBlackSetupValues) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.cvbs_presets.ntsc_black_setup_ire = 0.0;
  project.cvbs_presets.ntsc_black_setup_ire_specified = true;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsUnsupportedNtscBlackSetupValue) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.cvbs_presets.ntsc_black_setup_ire = 1.0;
  project.cvbs_presets.ntsc_black_setup_ire_specified = true;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  EXPECT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsNtscBlackSetupOnPalProject) {
  Project project = MakeValidProject();
  project.cvbs_presets.ntsc_black_setup_ire = 0.0;
  project.cvbs_presets.ntsc_black_setup_ire_specified = true;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  EXPECT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsPalLaserdiscPilotBurstOnNtscProject) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.cvbs_presets.pal_laserdisc_pilot_burst = true;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_EQ(result.errors.size(), 1U);
  EXPECT_EQ(result.errors[0],
            "MVP constraint violation: pal_laserdisc_pilot_burst can only be enabled for PAL projects.");
}

TEST(ProjectValidatorTest, RejectsNtscLaserdiscVbiBurstOnPalProject) {
  Project project = MakeValidProject();
  project.cvbs_presets.ntsc_laserdisc_vbi_burst = true;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_EQ(result.errors.size(), 1U);
  EXPECT_EQ(result.errors[0],
            "MVP constraint violation: ntsc_laserdisc_vbi_burst can only be enabled for NTSC projects.");
}

TEST(ProjectValidatorTest, RejectsPalLaserdiscPilotBurstAsDeferredFeature) {
  Project project = MakeValidProject();
  project.cvbs_presets.pal_laserdisc_pilot_burst = true;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_EQ(result.errors.size(), 1U);
  EXPECT_EQ(result.errors[0],
            "MVP constraint violation: pal_laserdisc_pilot_burst is parsed but not implemented in the current runtime.");
}

TEST(ProjectValidatorTest, RejectsNtscLaserdiscVbiBurstAsDeferredFeature) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.cvbs_presets.ntsc_laserdisc_vbi_burst = true;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_EQ(result.errors.size(), 1U);
  EXPECT_EQ(result.errors[0],
            "MVP constraint violation: ntsc_laserdisc_vbi_burst is parsed but not implemented in the current runtime.");
}

TEST(ProjectValidatorTest, RejectsLineInjectionsAsDeferredFeature) {
  Project project = MakeValidProject();
  Section::LineInjection injection;
  injection.type = "vits";
  injection.target_lines = {19};
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_EQ(result.errors.size(), 1U);
  EXPECT_EQ(result.errors[0],
            "MVP constraint violation: line_injections are parsed but not implemented in the current runtime.");
}

TEST(ProjectValidatorTest, RejectsUnsupportedSectionType) {
  Project project = MakeValidProject();
  project.sections[0].type = "software_generated";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, AcceptsProgressiveExrSectionWithFixedDuration) {
  Project project = MakeValidProject();
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive.exr");
  project.sections[0].duration_frames = 8;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, RejectsProgressiveSectionWithoutSource) {
  Project project = MakeValidProject();
  project.sections[0].source.clear();

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, RejectsProgressiveRawSourceFamily) {
  Project project = MakeValidProject();
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive.raw");

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  EXPECT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("Unsupported progressive source family"), std::string::npos);

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, AcceptsProgressiveAllDurationSemantics) {
  Project project = MakeValidProject();
  project.sections[0].duration_frames_all = true;
  project.sections[0].duration_frames = 0;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
}

TEST(ProjectValidatorTest, RejectsMissingDurationFrames) {
  Project project = MakeValidProject();
  project.sections[0].duration_frames = 0;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
}

TEST(ProjectValidatorTest, AcceptsProgressiveMkvWithSupportedFfv1Profile) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive_ok.mkv");
  project.sections[0].duration_frames_all = true;
  project.sections[0].duration_frames = 0;

  MockProgressiveFrameSourceProbe probe;
  probe.profile.container = "matroska,webm";
  probe.profile.codec = "ffv1";
  probe.profile.pixel_format = "yuv422p10le";
  probe.profile.field_order = "tb";
  probe.profile.color_space = "smpte170m";
  probe.profile.color_primaries = "bt470bg";
  probe.profile.color_transfer = "bt709";
  probe.profile.color_range = "tv";
  probe.profile.bit_depth = 10;
  probe.profile.width = 720;
  probe.profile.height = 576;
  probe.profile.frame_rate_hz = 25.0;
  probe.profile.frame_count = 120;

  ProjectValidator validator(&probe);
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, RejectsProgressiveMkvWithMismatchedSampleAspectMetadata) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive_bad_sar.mkv");
  project.sections[0].duration_frames_all = true;
  project.sections[0].duration_frames = 0;

  MockProgressiveFrameSourceProbe probe;
  probe.profile.container = "matroska,webm";
  probe.profile.codec = "ffv1";
  probe.profile.pixel_format = "yuv422p10le";
  probe.profile.field_order = "bt";
  probe.profile.color_space = "smpte170m";
  probe.profile.color_primaries = "smpte170m";
  probe.profile.color_transfer = "bt709";
  probe.profile.color_range = "tv";
  probe.profile.bit_depth = 10;
  probe.profile.width = 720;
  probe.profile.height = 486;
  probe.profile.sample_aspect_ratio = 1.0;
  probe.profile.frame_rate_hz = 30000.0 / 1001.0;
  probe.profile.frame_count = 90;

  ProjectValidator validator(&probe);
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("sample-aspect"), std::string::npos);

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, RejectsProgressiveMkvWithStreamCropMetadata) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive_bad_crop.mkv");
  project.sections[0].duration_frames_all = true;
  project.sections[0].duration_frames = 0;

  MockProgressiveFrameSourceProbe probe;
  probe.profile.container = "matroska,webm";
  probe.profile.codec = "ffv1";
  probe.profile.pixel_format = "yuv422p10le";
  probe.profile.field_order = "tb";
  probe.profile.color_space = "smpte170m";
  probe.profile.color_primaries = "bt470bg";
  probe.profile.color_transfer = "bt709";
  probe.profile.color_range = "tv";
  probe.profile.bit_depth = 10;
  probe.profile.width = 720;
  probe.profile.height = 576;
  probe.profile.sample_aspect_ratio = 128.0 / 117.0;
  probe.profile.crop_left = 1;
  probe.profile.frame_rate_hz = 25.0;
  probe.profile.frame_count = 120;

  ProjectValidator validator(&probe);
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("crop metadata"), std::string::npos);

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, RejectsProgressiveMkvWithUnsupportedCodecProfile) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive_bad.mkv");
  project.sections[0].duration_frames_all = true;
  project.sections[0].duration_frames = 0;

  MockProgressiveFrameSourceProbe probe;
  probe.profile.container = "matroska,webm";
  probe.profile.codec = "h264";
  probe.profile.pixel_format = "yuv420p";
  probe.profile.field_order = "bt";
  probe.profile.color_space = "smpte170m";
  probe.profile.color_primaries = "smpte170m";
  probe.profile.color_transfer = "bt709";
  probe.profile.color_range = "tv";
  probe.profile.bit_depth = 8;
  probe.profile.width = 720;
  probe.profile.height = 486;
  probe.profile.frame_rate_hz = 30000.0 / 1001.0;
  probe.profile.frame_count = 90;

  ProjectValidator validator(&probe);
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, AcceptsProgressiveExrWithSupportedProfile) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive_ok.exr");
  project.sections[0].duration_frames = 1;

  MockProgressiveFrameSourceProbe probe;
  probe.profile.container = "exr";
  probe.profile.codec = "openexr";
  probe.profile.pixel_format = "rgbf";
  probe.profile.bit_depth = 32;
  probe.profile.width = 720;
  probe.profile.height = 576;
  probe.profile.frame_rate_hz = 0.0;
  probe.profile.frame_count = 1;

  ProjectValidator validator(&probe);
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, RejectsProgressiveExrWithUnsupportedPixelProfile) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive_bad.exr");
  project.sections[0].duration_frames = 1;

  MockProgressiveFrameSourceProbe probe;
  probe.profile.container = "exr";
  probe.profile.codec = "openexr";
  probe.profile.pixel_format = "rgb24";
  probe.profile.bit_depth = 24;
  probe.profile.width = 720;
  probe.profile.height = 486;
  probe.profile.frame_rate_hz = 0.0;
  probe.profile.frame_count = 1;

  ProjectValidator validator(&probe);
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, RejectsUnsupportedSourceFamilyWithExpectedErrorMessage) {
  Project project = MakeValidProject();
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive_bad.avi");

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0],
            "Unsupported progressive source family. Supported source families are EXR and MKV.");

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, RejectsPalProjectWhenProgressiveRasterDoesNotMatchStandard) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive_bad_raster.exr");
  project.sections[0].duration_frames = 1;

  MockProgressiveFrameSourceProbe probe;
  probe.profile.container = "exr";
  probe.profile.codec = "openexr";
  probe.profile.pixel_format = "rgbf";
  probe.profile.bit_depth = 32;
  probe.profile.width = 720;
  probe.profile.height = 486;
  probe.profile.frame_rate_hz = 0.0;
  probe.profile.frame_count = 1;

  ProjectValidator validator(&probe);
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0],
            "Progressive section validation error: source raster must be 720x576 for PAL and 720x486 for NTSC.");

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, RejectsPalProjectWhenProgressiveFrameRateDoesNotMatchStandard) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive_bad_rate.exr");
  project.sections[0].duration_frames = 1;

  MockProgressiveFrameSourceProbe probe;
  probe.profile.container = "exr";
  probe.profile.codec = "openexr";
  probe.profile.pixel_format = "rgbf";
  probe.profile.bit_depth = 32;
  probe.profile.width = 720;
  probe.profile.height = 576;
  probe.profile.frame_rate_hz = 24.0;
  probe.profile.frame_count = 1;

  ProjectValidator validator(&probe);
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0],
            "Progressive section validation error: source frame rate must match selected video standard.");

  std::filesystem::remove(project.sections[0].source);
}

TEST(ProjectValidatorTest, PropagatesProgressiveProbeErrorMessage) {
  Project project = MakeValidProject();
  project.sections[0].source = CreateTemporarySourceFile("videosynth_progressive_probe_error.mkv");

  MockProgressiveFrameSourceProbe probe;
  probe.should_succeed = false;
  probe.error_message = "Phase1 probe failure test message.";

  ProjectValidator validator(&probe);
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0], "Phase1 probe failure test message.");

  std::filesystem::remove(project.sections[0].source);
}

}  // namespace
}  // namespace videosynth
