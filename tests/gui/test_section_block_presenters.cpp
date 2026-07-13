/*
 * File:        test_section_block_presenters.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for the optional-block mapping layer (audio,
 *              noise, dropouts, OSD)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "section_block_presenters.h"
#include "videosynth/model.h"
#include "videosynth/project_validator.h"
#include "videosynth/yaml_project_emitter.h"

namespace videosynth::gui {
namespace {

Project MakeValidProject() {
  Project project;
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.output.video_path = "out/video.composite";
  project.output.metadata_path = "out/video.meta";
  Section section;
  section.name = "Bars";
  section.type = "progressive";
  section.source = "assets/bars.exr";
  section.duration_frames = 200;
  project.sections.push_back(section);
  return project;
}

std::string EmitYaml(const Project& project) {
  const YamlProjectEmitter emitter;
  return emitter.EmitString(project);
}

bool ProjectIsValid(const Project& project, std::string* first_error) {
  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  if (!result.is_valid && first_error != nullptr && !result.errors.empty()) {
    *first_error = result.errors.front();
  }
  return result.is_valid;
}

TEST(SectionBlockPresenterTest, NoiseEnableEmitsBlockDisableRemovesIt) {
  Project project = MakeValidProject();
  EXPECT_EQ(EmitYaml(project).find("noise:"), std::string::npos);

  SetNoiseBlockEnabled(&project.sections[0], true);
  EXPECT_NE(EmitYaml(project).find("noise:"), std::string::npos);

  std::string error;
  EXPECT_TRUE(ProjectIsValid(project, &error)) << error;

  SetNoiseBlockEnabled(&project.sections[0], false);
  EXPECT_EQ(EmitYaml(project).find("noise:"), std::string::npos);
  EXPECT_EQ(project.sections[0].noise, NoiseParameters{});
}

TEST(SectionBlockPresenterTest, DropoutEnableEmitsBlockDisableRemovesIt) {
  Project project = MakeValidProject();
  EXPECT_EQ(EmitYaml(project).find("dropouts:"), std::string::npos);

  SetRandomDropoutsEnabled(&project.sections[0], true);
  EXPECT_NE(EmitYaml(project).find("random:"), std::string::npos);
  std::string error;
  EXPECT_TRUE(ProjectIsValid(project, &error)) << error;

  SetScratchDropoutsEnabled(&project.sections[0], true);
  EXPECT_NE(EmitYaml(project).find("scratch:"), std::string::npos);
  EXPECT_TRUE(ProjectIsValid(project, &error)) << error;

  SetRandomDropoutsEnabled(&project.sections[0], false);
  SetScratchDropoutsEnabled(&project.sections[0], false);
  EXPECT_EQ(EmitYaml(project).find("dropouts:"), std::string::npos);
}

TEST(SectionBlockPresenterTest, AddChannelPairEmitsBlockClearRemovesIt) {
  Project project = MakeValidProject();
  EXPECT_EQ(EmitYaml(project).find("audio:"), std::string::npos);

  project.sections[0].audio_channel_pairs.push_back(
      MakeDefaultAudioChannelPair(0));
  EXPECT_NE(EmitYaml(project).find("audio:"), std::string::npos);
  EXPECT_NE(EmitYaml(project).find("channel_pairs:"), std::string::npos);
  std::string error;
  EXPECT_TRUE(ProjectIsValid(project, &error)) << error;

  project.sections[0].audio_channel_pairs.clear();
  EXPECT_EQ(EmitYaml(project).find("audio:"), std::string::npos);
}

TEST(SectionBlockPresenterTest, DefaultChannelPairHasActiveLeftSilentRight) {
  const AudioChannelPair channel_pair = MakeDefaultAudioChannelPair(4);
  EXPECT_EQ(channel_pair.pair, 4);
  EXPECT_TRUE(channel_pair.pair_specified);
  EXPECT_TRUE(channel_pair.left.enabled);
  EXPECT_EQ(channel_pair.left.waveform, AudioWaveform::kSine);
  EXPECT_FALSE(channel_pair.right.enabled);
}

TEST(SectionBlockPresenterTest, ChannelRampEnableSeedsValidRamp) {
  Project project = MakeValidProject();
  AudioChannelPair channel_pair = MakeDefaultAudioChannelPair(0);
  SetAudioChannelRampEnabled(&channel_pair.left, true);
  project.sections[0].audio_channel_pairs.push_back(channel_pair);

  const AudioParameters& audio =
      project.sections[0].audio_channel_pairs[0].left;
  EXPECT_TRUE(audio.ramp_enabled);
  EXPECT_TRUE(audio.ramp_start_specified);
  EXPECT_TRUE(audio.ramp_end_specified);
  EXPECT_EQ(audio.ramp_mode, AudioRampMode::kUp);
  EXPECT_NE(EmitYaml(project).find("ramp:"), std::string::npos);

  std::string error;
  EXPECT_TRUE(ProjectIsValid(project, &error)) << error;

  SetAudioChannelRampEnabled(&project.sections[0].audio_channel_pairs[0].left,
                             false);
  EXPECT_EQ(EmitYaml(project).find("ramp:"), std::string::npos);
  EXPECT_TRUE(ProjectIsValid(project, &error)) << error;
}

TEST(SectionBlockPresenterTest, SetChannelWaveformKeepsTextAndEnumConsistent) {
  AudioParameters channel = MakeDefaultAudioChannel();
  SetAudioChannelWaveform(&channel, "triangle");
  EXPECT_EQ(channel.waveform, AudioWaveform::kTriangle);
  EXPECT_EQ(channel.waveform_text, "triangle");
}

TEST(SectionBlockPresenterTest, NextFreeChannelPairSkipsUsedNumbers) {
  std::vector<AudioChannelPair> pairs;
  EXPECT_EQ(NextFreeAudioChannelPair(pairs), 0);
  pairs.push_back(MakeDefaultAudioChannelPair(0));
  pairs.push_back(MakeDefaultAudioChannelPair(1));
  EXPECT_EQ(NextFreeAudioChannelPair(pairs), 2);
  for (int pair = 2; pair < kMaxAudioChannelPairs; ++pair) {
    pairs.push_back(MakeDefaultAudioChannelPair(pair));
  }
  EXPECT_EQ(NextFreeAudioChannelPair(pairs), -1);  // All eight used.
}

TEST(SectionBlockPresenterTest, OsdBlockFollowsOverlayList) {
  Project project = MakeValidProject();
  EXPECT_FALSE(OsdBlockEnabled(project.sections[0]));
  EXPECT_EQ(EmitYaml(project).find("osd:"), std::string::npos);

  project.sections[0].osd.overlays.push_back(MakeDefaultOsdOverlay());
  EXPECT_TRUE(OsdBlockEnabled(project.sections[0]));
  EXPECT_NE(EmitYaml(project).find("osd:"), std::string::npos);

  std::string error;
  EXPECT_TRUE(ProjectIsValid(project, &error)) << error;
}

// The editor limits must accept exactly what the validator accepts: values
// at both bounds pass, values one step outside fail.
TEST(SectionBlockPresenterTest, NoiseLimitsMirrorValidator) {
  Project project = MakeValidProject();
  SetNoiseBlockEnabled(&project.sections[0], true);

  project.sections[0].noise.noise_db = editor_limits::kNoiseDbMin;
  EXPECT_TRUE(ProjectIsValid(project, nullptr));
  project.sections[0].noise.noise_db = editor_limits::kNoiseDbMax;
  EXPECT_TRUE(ProjectIsValid(project, nullptr));
  project.sections[0].noise.noise_db = editor_limits::kNoiseDbMin - 0.1;
  EXPECT_FALSE(ProjectIsValid(project, nullptr));
  project.sections[0].noise.noise_db = editor_limits::kNoiseDbMax + 0.1;
  EXPECT_FALSE(ProjectIsValid(project, nullptr));
}

TEST(SectionBlockPresenterTest, DropoutScaleLimitsMirrorValidator) {
  Project project = MakeValidProject();
  SetRandomDropoutsEnabled(&project.sections[0], true);

  project.sections[0].dropouts.random.scale = editor_limits::kDropoutScaleMin;
  EXPECT_TRUE(ProjectIsValid(project, nullptr));
  project.sections[0].dropouts.random.scale = editor_limits::kDropoutScaleMax;
  EXPECT_TRUE(ProjectIsValid(project, nullptr));
  project.sections[0].dropouts.random.scale =
      editor_limits::kDropoutScaleMax + 1;
  EXPECT_FALSE(ProjectIsValid(project, nullptr));
}

TEST(SectionBlockPresenterTest, OsdScaleLimitsMirrorValidator) {
  Project project = MakeValidProject();
  project.sections[0].osd.overlays.push_back(MakeDefaultOsdOverlay());

  project.sections[0].osd.overlays[0].scale = editor_limits::kOsdScaleMin;
  EXPECT_TRUE(ProjectIsValid(project, nullptr));
  project.sections[0].osd.overlays[0].scale = editor_limits::kOsdScaleMax;
  EXPECT_TRUE(ProjectIsValid(project, nullptr));
  project.sections[0].osd.overlays[0].scale = editor_limits::kOsdScaleMax + 1;
  EXPECT_FALSE(ProjectIsValid(project, nullptr));
}

TEST(SectionBlockPresenterTest, AudioLimitsMirrorValidator) {
  Project project = MakeValidProject();
  project.sections[0].audio_channel_pairs.push_back(
      MakeDefaultAudioChannelPair(0));
  AudioParameters& left = project.sections[0].audio_channel_pairs[0].left;

  left.frequency_hz = editor_limits::kAudioFrequencyMaxHz;
  EXPECT_TRUE(ProjectIsValid(project, nullptr));
  left.frequency_hz = editor_limits::kAudioFrequencyMaxHz + 1.0;
  EXPECT_FALSE(ProjectIsValid(project, nullptr));

  left.frequency_hz = 1000.0;
  left.amplitude = editor_limits::kAudioAmplitudeMax;
  EXPECT_TRUE(ProjectIsValid(project, nullptr));
  left.amplitude = editor_limits::kAudioAmplitudeMax + 0.1;
  EXPECT_FALSE(ProjectIsValid(project, nullptr));
}

TEST(SectionBlockPresenterTest, OsdTokenCatalogueMatchesResolverTokens) {
  Project project = MakeValidProject();
  // Every documented token must be accepted by OSD validation when used in
  // an overlay.
  for (const OsdTokenHelp& help : OsdTokenCatalogue()) {
    project.sections[0].osd.overlays.clear();
    OsdOverlay overlay = MakeDefaultOsdOverlay();
    overlay.text = help.token;
    project.sections[0].osd.overlays.push_back(overlay);
    std::string error;
    EXPECT_TRUE(ProjectIsValid(project, &error)) << help.token << ": " << error;
  }
}

}  // namespace
}  // namespace videosynth::gui
