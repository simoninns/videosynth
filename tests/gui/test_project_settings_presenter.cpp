/*
 * File:        test_project_settings_presenter.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for the project settings mapping layer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "project_settings_presenter.h"
#include "videosynth/model.h"
#include "videosynth/project_validator.h"

namespace videosynth::gui {
namespace {

Project MakeProject(Standard standard) {
  Project project;
  project.cvbs_presets.video_standard_preset = standard;
  project.output.video_path = "out/video.composite";
  project.output.metadata_path = "out/video.meta";
  Section section;
  section.name = "Bars";
  section.type = "progressive";
  section.source = "assets/bars.exr";
  section.duration_frames = 10;
  project.sections.push_back(section);
  return project;
}

TEST(ProjectSettingsPresenterTest, EnablementFollowsStandardPal) {
  const ProjectSettingsFormState state =
      BuildProjectSettingsFormState(MakeProject(Standard::kPal));
  EXPECT_TRUE(state.pilot_burst_editable);
  EXPECT_FALSE(state.vbi_burst_editable);
  EXPECT_FALSE(state.setup_ire_editable);
}

TEST(ProjectSettingsPresenterTest, EnablementFollowsStandardNtsc) {
  const ProjectSettingsFormState state =
      BuildProjectSettingsFormState(MakeProject(Standard::kNtsc));
  EXPECT_FALSE(state.pilot_burst_editable);
  EXPECT_TRUE(state.vbi_burst_editable);
  EXPECT_TRUE(state.setup_ire_editable);
}

TEST(ProjectSettingsPresenterTest, EnablementFollowsStandardPalM) {
  const ProjectSettingsFormState state =
      BuildProjectSettingsFormState(MakeProject(Standard::kPalM));
  EXPECT_FALSE(state.pilot_burst_editable);
  EXPECT_FALSE(state.vbi_burst_editable);
  EXPECT_TRUE(state.setup_ire_editable);
}

TEST(ProjectSettingsPresenterTest, YSuffixFlagFollowsSignalType) {
  Project project = MakeProject(Standard::kPal);
  EXPECT_FALSE(
      BuildProjectSettingsFormState(project).video_path_requires_y_suffix);
  project.output.signal_type = "yc";
  EXPECT_TRUE(
      BuildProjectSettingsFormState(project).video_path_requires_y_suffix);
}

// Every offered sample-encoding option must be accepted by the model's
// supported-preset predicate the validator uses.
TEST(ProjectSettingsPresenterTest, SampleEncodingOptionsMirrorSupportedSet) {
  const ProjectSettingsFormState state =
      BuildProjectSettingsFormState(MakeProject(Standard::kPal));
  ASSERT_FALSE(state.sample_encoding_options.empty());
  for (const std::string& preset : state.sample_encoding_options) {
    EXPECT_TRUE(IsSupportedSampleEncodingPreset(preset)) << preset;
  }
}

TEST(ProjectSettingsPresenterTest, NormalizeClearsFlagsForeignToStandard) {
  CvbsPresets presets;
  presets.video_standard_preset = Standard::kNtsc;
  presets.pal_laserdisc_pilot_burst = true;  // PAL-only flag.
  presets.ntsc_laserdisc_vbi_burst = true;
  presets.ntsc_black_setup_ire = 0.0;
  presets.ntsc_black_setup_ire_specified = true;

  const CvbsPresets ntsc = NormalizeCvbsPresetsForStandard(presets);
  EXPECT_FALSE(ntsc.pal_laserdisc_pilot_burst);
  EXPECT_TRUE(ntsc.ntsc_laserdisc_vbi_burst);
  EXPECT_TRUE(ntsc.ntsc_black_setup_ire_specified);

  presets.video_standard_preset = Standard::kPal;
  const CvbsPresets pal = NormalizeCvbsPresetsForStandard(presets);
  EXPECT_TRUE(pal.pal_laserdisc_pilot_burst);
  EXPECT_FALSE(pal.ntsc_laserdisc_vbi_burst);
  EXPECT_FALSE(pal.ntsc_black_setup_ire_specified);
  EXPECT_EQ(pal.ntsc_black_setup_ire, 7.5);
}

// Normalized presets must never trip the validator's standard-dependent
// preset rules (the enablement mirrors validation).
TEST(ProjectSettingsPresenterTest, NormalizedPresetsPassValidatorFlagRules) {
  for (const Standard standard :
       {Standard::kPal, Standard::kNtsc, Standard::kPalM}) {
    Project project = MakeProject(standard);
    CvbsPresets presets = project.cvbs_presets;
    presets.pal_laserdisc_pilot_burst = true;
    presets.ntsc_black_setup_ire_specified = true;
    presets.ntsc_black_setup_ire = 7.5;
    project.cvbs_presets = NormalizeCvbsPresetsForStandard(presets);

    ProjectValidator validator;
    const ValidationResult result = validator.Validate(project);
    for (const std::string& error : result.errors) {
      EXPECT_EQ(error.find("pal_laserdisc_pilot_burst"), std::string::npos)
          << error;
      EXPECT_EQ(error.find("ntsc_black_setup_ire"), std::string::npos) << error;
    }
  }
}

TEST(ProjectSettingsPresenterTest, DeriveMetadataPathStripsOutputSuffixes) {
  EXPECT_EQ(DeriveMetadataPath("out/video.composite"), "out/video.meta");
  EXPECT_EQ(DeriveMetadataPath("out/video.y"), "out/video.meta");
  EXPECT_EQ(DeriveMetadataPath("out/video"), "out/video.meta");
  EXPECT_EQ(DeriveMetadataPath("out/video.raw"), "out/video.meta");
  // A dot in a directory component is not an extension.
  EXPECT_EQ(DeriveMetadataPath("out.v2/video"), "out.v2/video.meta");
  EXPECT_EQ(DeriveMetadataPath(""), "");
}

TEST(ProjectSettingsPresenterTest, EnforceSignalTypeRewritesVideoPath) {
  EXPECT_EQ(EnforceSignalTypeVideoPath("out/video.composite", "yc"),
            "out/video.y");
  EXPECT_EQ(EnforceSignalTypeVideoPath("out/video.y", "yc"), "out/video.y");
  EXPECT_EQ(EnforceSignalTypeVideoPath("out/video", "yc"), "out/video.y");
  EXPECT_EQ(EnforceSignalTypeVideoPath("out/video.y", "composite"),
            "out/video.composite");
  EXPECT_EQ(EnforceSignalTypeVideoPath("out/video.composite", "composite"),
            "out/video.composite");
  EXPECT_EQ(EnforceSignalTypeVideoPath("", "yc"), "");
}

// The enforced yc path must satisfy the validator's ".y" suffix rule.
TEST(ProjectSettingsPresenterTest, EnforcedYcPathPassesValidator) {
  Project project = MakeProject(Standard::kPal);
  project.output.signal_type = "yc";
  project.output.video_path =
      EnforceSignalTypeVideoPath("out/video.composite", "yc");
  project.output.metadata_path = DeriveMetadataPath(project.output.video_path);

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_TRUE(result.is_valid);
}

}  // namespace
}  // namespace videosynth::gui
