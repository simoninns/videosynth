/*
 * File:        test_project_validator.cpp
 * Module:      project_validator_tests
 * Purpose:     Validates project constraint enforcement and error reporting.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "videosynth/project_validator.h"

namespace videosynth {
namespace {

std::string DefaultProgressiveSourcePath() { return "fixture.exr"; }

Project MakeValidProject() {
  Project project;
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  project.output.video_path = "/tmp/videosynth_validator_test.composite";
  project.output.metadata_path = "/tmp/videosynth_validator_test.meta";
  project.sections.push_back(Section{.name = "Progressive",
                                     .type = "progressive",
                                     .line_injections = {},
                                     .source = DefaultProgressiveSourcePath(),
                                     .duration_frames = 1});
  return project;
}

std::string MakeSourcePath(const std::string& file_name) { return file_name; }

class MockProgressiveFrameSourceProbe final
    : public IProgressiveFrameSourceProbe {
 public:
  bool should_succeed = true;
  ProgressiveFrameSourceProfile profile;
  std::string error_message;

  bool Probe(const Section&, ProgressiveFrameSourceProfile* out_profile,
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
  const std::vector<std::string> presets = {"CVBS_U10_4FSC",   "CVBS_U16_4FSC",
                                            "RAW_S16_28M",     "RAW_S16_40M",
                                            "CVBS_TPG21_4FSC", "CVBS_S16_FSC"};

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
  EXPECT_EQ(
      result.errors[0],
      "Project configuration error: pal_laserdisc_pilot_burst can only be "
      "enabled for PAL projects.");
}

TEST(ProjectValidatorTest, RejectsNtscLaserdiscVbiBurstOnPalProject) {
  Project project = MakeValidProject();
  project.cvbs_presets.ntsc_laserdisc_vbi_burst = true;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_EQ(result.errors.size(), 1U);
  EXPECT_EQ(result.errors[0],
            "Project configuration error: ntsc_laserdisc_vbi_burst can only be "
            "enabled for NTSC projects.");
}

TEST(ProjectValidatorTest, AcceptsPalLaserdiscPilotBurstOnPalProject) {
  Project project = MakeValidProject();
  project.cvbs_presets.pal_laserdisc_pilot_burst = true;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST(ProjectValidatorTest, WarnsPalPilotBurstClippingWithU10Preset) {
  Project project = MakeValidProject();
  project.cvbs_presets.pal_laserdisc_pilot_burst = true;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
  ASSERT_EQ(result.warnings.size(), 1U);
  EXPECT_EQ(result.warnings[0],
            "pal_laserdisc_pilot_burst warning: preset 'CVBS_U10_4FSC' clips "
            "sub-sync excursions below -300 mV; the pilot burst trough reaches "
            "-600 mV. Use CVBS_S16_FSC or RAW_S16_28M/RAW_S16_40M to preserve "
            "the full burst waveform.");
}

TEST(ProjectValidatorTest, WarnsPalPilotBurstClippingWithU16Preset) {
  Project project = MakeValidProject();
  project.cvbs_presets.pal_laserdisc_pilot_burst = true;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U16_4FSC";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
  ASSERT_EQ(result.warnings.size(), 1U);
  EXPECT_EQ(result.warnings[0],
            "pal_laserdisc_pilot_burst warning: preset 'CVBS_U16_4FSC' clips "
            "sub-sync excursions below -300 mV; the pilot burst trough reaches "
            "-600 mV. Use CVBS_S16_FSC or RAW_S16_28M/RAW_S16_40M to preserve "
            "the full burst waveform.");
}

TEST(ProjectValidatorTest, WarnsPalPilotBurstClippingWithTpg21Preset) {
  Project project = MakeValidProject();
  project.cvbs_presets.pal_laserdisc_pilot_burst = true;
  project.cvbs_presets.sample_encoding_preset = "CVBS_TPG21_4FSC";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
  ASSERT_EQ(result.warnings.size(), 1U);
  EXPECT_EQ(result.warnings[0],
            "pal_laserdisc_pilot_burst warning: preset 'CVBS_TPG21_4FSC' clips "
            "sub-sync excursions below -300 mV; the pilot burst trough reaches "
            "-600 mV. Use CVBS_S16_FSC or RAW_S16_28M/RAW_S16_40M to preserve "
            "the full burst waveform.");
}

TEST(ProjectValidatorTest, NoPilotBurstClippingWarningWithS16FscPreset) {
  Project project = MakeValidProject();
  project.cvbs_presets.pal_laserdisc_pilot_burst = true;
  project.cvbs_presets.sample_encoding_preset = "CVBS_S16_FSC";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
  EXPECT_TRUE(result.warnings.empty());
}

TEST(ProjectValidatorTest, NoPilotBurstClippingWarningWithRawS1628MPreset) {
  Project project = MakeValidProject();
  project.cvbs_presets.pal_laserdisc_pilot_burst = true;
  project.cvbs_presets.sample_encoding_preset = "RAW_S16_28M";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
  EXPECT_TRUE(result.warnings.empty());
}

TEST(ProjectValidatorTest, NoPilotBurstClippingWarningWithRawS1640MPreset) {
  Project project = MakeValidProject();
  project.cvbs_presets.pal_laserdisc_pilot_burst = true;
  project.cvbs_presets.sample_encoding_preset = "RAW_S16_40M";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
  EXPECT_TRUE(result.warnings.empty());
}

TEST(ProjectValidatorTest, NoPilotBurstClippingWarningWhenBurstDisabled) {
  Project project = MakeValidProject();
  project.cvbs_presets.pal_laserdisc_pilot_burst = false;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
  EXPECT_TRUE(result.warnings.empty());
}

TEST(ProjectValidatorTest, RejectsNtscLaserdiscVbiBurstAsDeferredFeature) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.cvbs_presets.ntsc_laserdisc_vbi_burst = true;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_EQ(result.errors.size(), 1U);
  EXPECT_EQ(
      result.errors[0],
      "Project configuration error: ntsc_laserdisc_vbi_burst is parsed but "
      "not implemented in the current runtime.");
}

TEST(ProjectValidatorTest, AcceptsVitsLineInjectionsForImplementedRuntimePath) {
  Project project = MakeValidProject();
  Section::LineInjection injection;
  injection.type = "vits";
  injection.target_lines = {17};
  injection.vits_type = "vits17";
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST(ProjectValidatorTest, AcceptsLaserdiscInjectionWithoutDiscTypeField) {
  // Bare laserdisc injection with no disc_type: structurally valid and now
  // fully implemented via BiphaseInjectionManager.
  Project project = MakeValidProject();
  Section::LineInjection injection;
  injection.type = "laserdisc";
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsVitsInjectionWithoutTargetLines) {
  Project project = MakeValidProject();
  Section::LineInjection injection;
  injection.type = "vits";
  injection.vits_type = "vits17";
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0],
            "Line injection validation error: target_lines must be provided "
            "and non-empty for injection type 'vits'.");
}

TEST(ProjectValidatorTest, RejectsVitsTypeThatDoesNotMatchProjectStandard) {
  Project project = MakeValidProject();
  Section::LineInjection injection;
  injection.type = "vits";
  injection.target_lines = {17};
  injection.vits_type = "ntc7-composite";
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0],
            "Line injection validation error: Unsupported vits_type "
            "'ntc7-composite' for standard 'PAL'.");
}

TEST(ProjectValidatorTest, RejectsVitsTargetLineOutsideStandardRange) {
  Project project = MakeValidProject();
  Section::LineInjection injection;
  injection.type = "vits";
  injection.target_lines = {626};
  injection.vits_type = "vits17";
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0],
            "Line injection validation error: target line 626 is outside the "
            "valid frame-line range for PAL.");
}

TEST(ProjectValidatorTest, RejectsVitsTypeOnNonRecommendedLine) {
  Project project = MakeValidProject();
  Section::LineInjection injection;
  injection.type = "vits";
  injection.target_lines = {19};
  injection.vits_type = "vits17";
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0],
            "Line injection validation error: vits_type 'vits17' must target "
            "frame line 17 for PAL.");
}

TEST(ProjectValidatorTest, RejectsOverlappingTargetLinesAcrossInjections) {
  Project project = MakeValidProject();

  Section::LineInjection first;
  first.type = "vits";
  first.target_lines = {17};
  first.vits_type = "vits17";
  project.sections[0].line_injections.push_back(first);

  Section::LineInjection second;
  second.type = "vitc";
  second.target_lines = {17};
  project.sections[0].line_injections.push_back(second);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0],
            "Line injection validation error: overlapping target line 17 "
            "within the same section.");
}

TEST(ProjectValidatorTest, RejectsLaserdiscInjectionWithExplicitTargetLines) {
  Project project = MakeValidProject();

  Section::LineInjection injection;
  injection.type = "laserdisc";
  injection.target_lines = {16};
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0],
            "Line injection validation error: target_lines must not be "
            "specified for laserdisc injections.");
}

TEST(ProjectValidatorTest, RejectsLaserdiscAndVitcInSameSection) {
  Project project = MakeValidProject();

  Section::LineInjection laserdisc;
  laserdisc.type = "laserdisc";
  project.sections[0].line_injections.push_back(laserdisc);

  Section::LineInjection vitc;
  vitc.type = "vitc";
  vitc.target_lines = {21};
  project.sections[0].line_injections.push_back(vitc);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0],
            "Line injection validation error: vitc and laserdisc injections "
            "cannot appear in the same section.");
}

TEST(ProjectValidatorTest,
     RejectsLinesInLaserdiscReservedRangesWhenLaserdiscIsActive) {
  Project project = MakeValidProject();

  Section::LineInjection laserdisc;
  laserdisc.type = "laserdisc";
  project.sections[0].line_injections.push_back(laserdisc);

  Section::LineInjection vits;
  vits.type = "vits";
  vits.target_lines = {17};
  vits.vits_type = "vits17";
  project.sections[0].line_injections.push_back(vits);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0],
            "Line injection validation error: target line 17 conflicts with "
            "laserdisc reserved VBI ranges for PAL.");
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
  project.sections[0].source = MakeSourcePath("test.exr");
  project.sections[0].duration_frames = 8;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
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
  project.sections[0].source = MakeSourcePath("test.raw");

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  EXPECT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("Unsupported progressive source family"),
            std::string::npos);
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
  project.sections[0].source = MakeSourcePath("test.mkv");
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
}

TEST(ProjectValidatorTest,
     RejectsProgressiveMkvWithMismatchedSampleAspectMetadata) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.sections[0].source = MakeSourcePath("test.mkv");
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
}

TEST(ProjectValidatorTest, RejectsProgressiveMkvWithStreamCropMetadata) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.sections[0].source = MakeSourcePath("test.mkv");
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
}

TEST(ProjectValidatorTest, RejectsProgressiveMkvWithUnsupportedCodecProfile) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.sections[0].source = MakeSourcePath("test.mkv");
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
}

TEST(ProjectValidatorTest, AcceptsProgressiveExrWithSupportedProfile) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.sections[0].source = MakeSourcePath("test.exr");
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
}

TEST(ProjectValidatorTest, RejectsProgressiveExrWithUnsupportedPixelProfile) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kNtsc;
  project.sections[0].source = MakeSourcePath("test.exr");
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
}

TEST(ProjectValidatorTest,
     RejectsUnsupportedSourceFamilyWithExpectedErrorMessage) {
  Project project = MakeValidProject();
  project.sections[0].source = MakeSourcePath("test.avi");

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0],
            "Unsupported progressive source family. Supported source families "
            "are EXR and MKV.");
}

TEST(ProjectValidatorTest,
     RejectsPalProjectWhenProgressiveRasterDoesNotMatchStandard) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.sections[0].source = MakeSourcePath("test.exr");
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
            "Progressive section validation error: source raster must be "
            "720x576 for PAL and 720x486 for NTSC or PAL-M.");
}

TEST(ProjectValidatorTest,
     RejectsPalProjectWhenProgressiveFrameRateDoesNotMatchStandard) {
  Project project = MakeValidProject();
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.sections[0].source = MakeSourcePath("test.exr");
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
            "Progressive section validation error: source frame rate must "
            "match selected video standard.");
}

TEST(ProjectValidatorTest, PropagatesProgressiveProbeErrorMessage) {
  Project project = MakeValidProject();
  project.sections[0].source = MakeSourcePath("test.mkv");

  MockProgressiveFrameSourceProbe probe;
  probe.should_succeed = false;
  probe.error_message = "Phase1 probe failure test message.";

  ProjectValidator validator(&probe);
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_EQ(result.errors[0], "Phase1 probe failure test message.");
}

TEST(ProjectValidatorTest, AcceptsLaserdiscInjectionWithValidCavDiscType) {
  Project project = MakeValidProject();
  project.sections[0].section_type = SectionType::kProgrammeArea;
  Section::LineInjection injection;
  injection.type = "laserdisc";
  injection.disc_type = "CAV";
  Section::LineInjectionCode code;
  code.code_type = "picture_number";
  code.start_value = 1;
  code.start_value_specified = true;
  injection.codes.push_back(code);
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST(ProjectValidatorTest, AcceptsLaserdiscInjectionWithValidClvDiscType) {
  Project project = MakeValidProject();
  project.sections[0].section_type = SectionType::kProgrammeArea;
  Section::LineInjection injection;
  injection.type = "laserdisc";
  injection.disc_type = "CLV";
  Section::LineInjectionCode code;
  code.code_type = "programme_time_code";
  injection.codes.push_back(code);
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsLaserdiscInjectionWithUnknownDiscType) {
  Project project = MakeValidProject();
  Section::LineInjection injection;
  injection.type = "laserdisc";
  injection.disc_type = "VHD";
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("disc_type"), std::string::npos);
  EXPECT_NE(result.errors[0].find("VHD"), std::string::npos);
}

TEST(ProjectValidatorTest, RejectsLaserdiscInjectionWithCodeTypeInvalidForCav) {
  Project project = MakeValidProject();
  Section::LineInjection injection;
  injection.type = "laserdisc";
  injection.disc_type = "CAV";
  Section::LineInjectionCode code;
  code.code_type = "programme_time_code";
  injection.codes.push_back(code);
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("programme_time_code"), std::string::npos);
  EXPECT_NE(result.errors[0].find("CAV"), std::string::npos);
}

TEST(ProjectValidatorTest, RejectsLaserdiscInjectionWithCodeTypeInvalidForClv) {
  Project project = MakeValidProject();
  Section::LineInjection injection;
  injection.type = "laserdisc";
  injection.disc_type = "CLV";
  Section::LineInjectionCode code;
  code.code_type = "picture_number";
  injection.codes.push_back(code);
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("picture_number"), std::string::npos);
  EXPECT_NE(result.errors[0].find("CLV"), std::string::npos);
}

TEST(ProjectValidatorTest, RejectsLaserdiscInjectionWithUnknownCodeType) {
  Project project = MakeValidProject();
  Section::LineInjection injection;
  injection.type = "laserdisc";
  injection.disc_type = "CAV";
  Section::LineInjectionCode code;
  code.code_type = "unknown_code";
  injection.codes.push_back(code);
  project.sections[0].line_injections.push_back(injection);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("unknown_code"), std::string::npos);
}

// ---------------------------------------------------------------------------
// OSD validation tests
// ---------------------------------------------------------------------------

TEST(ProjectValidatorTest, AcceptsValidOsdOverlay) {
  Project project = MakeValidProject();
  OsdOverlay overlay;
  overlay.text = "PN:{picture_number}";
  overlay.x = 0;
  overlay.y = 0;
  overlay.scale = 2;
  overlay.fg_luma = 1.0;
  overlay.bg_luma = 0.0;
  project.sections[0].osd.overlays.push_back(overlay);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_TRUE(result.is_valid);
}

TEST(ProjectValidatorTest, RejectsOsdOverlayScaleTooLow) {
  Project project = MakeValidProject();
  OsdOverlay overlay;
  overlay.text = "LABEL";
  overlay.scale = 0;
  overlay.fg_luma = 1.0;
  overlay.bg_luma = -1.0;
  project.sections[0].osd.overlays.push_back(overlay);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("scale"), std::string::npos);
}

TEST(ProjectValidatorTest, RejectsOsdOverlayScaleTooHigh) {
  Project project = MakeValidProject();
  OsdOverlay overlay;
  overlay.text = "LABEL";
  overlay.scale = 5;
  overlay.fg_luma = 1.0;
  overlay.bg_luma = -1.0;
  project.sections[0].osd.overlays.push_back(overlay);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("scale"), std::string::npos);
}

TEST(ProjectValidatorTest, RejectsOsdOverlayFgLumaOutOfRange) {
  Project project = MakeValidProject();
  OsdOverlay overlay;
  overlay.text = "LABEL";
  overlay.scale = 1;
  overlay.fg_luma = 1.5;
  overlay.bg_luma = -1.0;
  project.sections[0].osd.overlays.push_back(overlay);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("fg_luma"), std::string::npos);
}

TEST(ProjectValidatorTest, RejectsOsdOverlayBgLumaInvalidNegative) {
  Project project = MakeValidProject();
  OsdOverlay overlay;
  overlay.text = "LABEL";
  overlay.scale = 1;
  overlay.fg_luma = 1.0;
  overlay.bg_luma = -0.5;
  project.sections[0].osd.overlays.push_back(overlay);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("bg_luma"), std::string::npos);
}

TEST(ProjectValidatorTest, AcceptsOsdOverlayTransparentBackground) {
  Project project = MakeValidProject();
  OsdOverlay overlay;
  overlay.text = "LABEL";
  overlay.scale = 1;
  overlay.fg_luma = 1.0;
  overlay.bg_luma = -1.0;
  project.sections[0].osd.overlays.push_back(overlay);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_TRUE(result.is_valid);
}

TEST(ProjectValidatorTest, RejectsOsdOverlayWithUnknownToken) {
  Project project = MakeValidProject();
  OsdOverlay overlay;
  overlay.text = "VALUE:{unknown_token}";
  overlay.scale = 1;
  overlay.fg_luma = 1.0;
  overlay.bg_luma = -1.0;
  project.sections[0].osd.overlays.push_back(overlay);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("unknown_token"), std::string::npos);
}

TEST(ProjectValidatorTest, AcceptsAllFourSupportedTokens) {
  Project project = MakeValidProject();
  OsdOverlay overlay;
  overlay.text = "{picture_number} {biphase_hex} {phase_id} {section_name}";
  overlay.scale = 1;
  overlay.fg_luma = 1.0;
  overlay.bg_luma = -1.0;
  project.sections[0].osd.overlays.push_back(overlay);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_TRUE(result.is_valid);
}

// ---------------------------------------------------------------------------
// disc_skips validation tests
// ---------------------------------------------------------------------------

Project MakeProjectWithFrames(int frame_count) {
  Project project = MakeValidProject();
  project.sections[0].duration_frames = frame_count;
  return project;
}

TEST(ProjectValidatorTest, AcceptsValidForwardDiscSkip) {
  Project project = MakeProjectWithFrames(10);
  DiscSkip skip;
  skip.at_frame = 3;
  skip.direction = DiscSkipDirection::kForward;
  skip.count = 4;
  project.disc_skips.push_back(skip);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_TRUE(result.is_valid);
}

TEST(ProjectValidatorTest, AcceptsValidBackwardDiscSkip) {
  Project project = MakeProjectWithFrames(20);
  DiscSkip skip;
  skip.at_frame = 15;
  skip.direction = DiscSkipDirection::kBackward;
  skip.count = 5;
  project.disc_skips.push_back(skip);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_TRUE(result.is_valid);
}

TEST(ProjectValidatorTest, RejectsDiscSkipAtFrameZero) {
  Project project = MakeProjectWithFrames(10);
  DiscSkip skip;
  skip.at_frame = 0;
  skip.direction = DiscSkipDirection::kForward;
  skip.count = 1;
  project.disc_skips.push_back(skip);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("at_frame"), std::string::npos);
}

TEST(ProjectValidatorTest, RejectsDiscSkipCountZero) {
  Project project = MakeProjectWithFrames(10);
  DiscSkip skip;
  skip.at_frame = 5;
  skip.direction = DiscSkipDirection::kForward;
  skip.count = 0;
  project.disc_skips.push_back(skip);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("count"), std::string::npos);
}

TEST(ProjectValidatorTest, RejectsForwardSkipExceedingTotalFrames) {
  Project project = MakeProjectWithFrames(10);
  DiscSkip skip;
  skip.at_frame = 9;
  skip.direction = DiscSkipDirection::kForward;
  skip.count = 5;  // 9+5-1 = 13 > 10
  project.disc_skips.push_back(skip);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsBackwardSkipExceedingFrameOne) {
  Project project = MakeProjectWithFrames(20);
  DiscSkip skip;
  skip.at_frame = 3;
  skip.direction = DiscSkipDirection::kBackward;
  skip.count = 5;  // first_replay = 3-5+1 = -1 < 1
  project.disc_skips.push_back(skip);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, AcceptsMultipleValidDiscSkips) {
  Project project = MakeProjectWithFrames(40);
  DiscSkip fwd;
  fwd.at_frame = 1;
  fwd.direction = DiscSkipDirection::kForward;
  fwd.count = 2;
  project.disc_skips.push_back(fwd);

  DiscSkip bwd;
  bwd.at_frame = 20;
  bwd.direction = DiscSkipDirection::kBackward;
  bwd.count = 4;
  project.disc_skips.push_back(bwd);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_TRUE(result.is_valid);
}

TEST(ProjectValidatorTest, AcceptsValidFixedAudio) {
  Project project = MakeValidProject();
  project.sections[0].duration_frames = 25;
  AudioParameters& audio = project.sections[0].audio;
  audio.enabled = true;
  audio.waveform = AudioWaveform::kSine;
  audio.frequency_hz = 1000.0;
  audio.amplitude = 0.5;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST(ProjectValidatorTest, AcceptsValidPeriodicRampAudio) {
  Project project = MakeValidProject();
  project.sections[0].duration_frames = 25;  // 1.0 s at 25 fps.
  AudioParameters& audio = project.sections[0].audio;
  audio.enabled = true;
  audio.ramp_enabled = true;
  audio.ramp_start_specified = true;
  audio.ramp_end_specified = true;
  audio.ramp_start_hz = 100.0;
  audio.ramp_end_hz = 2000.0;
  audio.ramp_mode = AudioRampMode::kBounce;
  audio.ramp_period_seconds = 0.5;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsUnknownAudioWaveform) {
  Project project = MakeValidProject();
  project.sections[0].audio.enabled = true;
  project.sections[0].audio.waveform = AudioWaveform::kUnknown;
  project.sections[0].audio.waveform_text = "noise";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsAudioAmplitudeOutOfRange) {
  Project project = MakeValidProject();
  project.sections[0].audio.enabled = true;
  project.sections[0].audio.amplitude = 1.5;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsAudioFrequencyOutOfRange) {
  Project project = MakeValidProject();
  project.sections[0].audio.enabled = true;
  project.sections[0].audio.frequency_hz = 30000.0;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsRampMissingEndFrequency) {
  Project project = MakeValidProject();
  AudioParameters& audio = project.sections[0].audio;
  audio.enabled = true;
  audio.ramp_enabled = true;
  audio.ramp_start_specified = true;
  audio.ramp_start_hz = 100.0;
  // ramp_end_specified deliberately left false.

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsUnknownRampMode) {
  Project project = MakeValidProject();
  AudioParameters& audio = project.sections[0].audio;
  audio.enabled = true;
  audio.ramp_enabled = true;
  audio.ramp_start_specified = true;
  audio.ramp_end_specified = true;
  audio.ramp_start_hz = 100.0;
  audio.ramp_end_hz = 200.0;
  audio.ramp_mode = AudioRampMode::kUnknown;
  audio.ramp_mode_text = "fast";

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsRampFrequencyOutOfRange) {
  Project project = MakeValidProject();
  AudioParameters& audio = project.sections[0].audio;
  audio.enabled = true;
  audio.ramp_enabled = true;
  audio.ramp_start_specified = true;
  audio.ramp_end_specified = true;
  audio.ramp_start_hz = 100.0;
  audio.ramp_end_hz = 25000.0;
  audio.ramp_mode = AudioRampMode::kUp;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsRampPeriodExceedingSectionDuration) {
  Project project = MakeValidProject();
  project.sections[0].duration_frames = 25;  // 1.0 s at 25 fps.
  AudioParameters& audio = project.sections[0].audio;
  audio.enabled = true;
  audio.ramp_enabled = true;
  audio.ramp_start_specified = true;
  audio.ramp_end_specified = true;
  audio.ramp_start_hz = 100.0;
  audio.ramp_end_hz = 200.0;
  audio.ramp_mode = AudioRampMode::kUp;
  audio.ramp_period_seconds = 2.0;  // Exceeds the 1.0 s section.

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
}

TEST(ProjectValidatorTest, RejectsNegativeRampPeriod) {
  Project project = MakeValidProject();
  AudioParameters& audio = project.sections[0].audio;
  audio.enabled = true;
  audio.ramp_enabled = true;
  audio.ramp_start_specified = true;
  audio.ramp_end_specified = true;
  audio.ramp_start_hz = 100.0;
  audio.ramp_end_hz = 200.0;
  audio.ramp_mode = AudioRampMode::kUp;
  audio.ramp_period_seconds = -1.0;

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);

  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
}

}  // namespace
}  // namespace videosynth
