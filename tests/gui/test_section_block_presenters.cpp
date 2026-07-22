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

TEST(SectionBlockPresenterTest, DefaultChannelPairHasIdenticalStereoChannels) {
  const AudioChannelPair channel_pair = MakeDefaultAudioChannelPair(4);
  EXPECT_EQ(channel_pair.pair, 4);
  EXPECT_TRUE(channel_pair.pair_specified);
  EXPECT_TRUE(channel_pair.left.enabled);
  EXPECT_EQ(channel_pair.left.waveform, AudioWaveform::kSine);
  // Identical channels are what the editor infers as its linked
  // "same tone on both channels" mode.
  EXPECT_TRUE(channel_pair.right == channel_pair.left);
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

TEST(SectionBlockPresenterTest, ApplySectionEditDeltaMirrorsOnlyChangedFields) {
  Section before;
  before.name = "Primary";
  before.source = "assets/bars.exr";
  before.duration_frames = 200;
  before.section_type = SectionType::kUnknown;

  // The edit: section type changed, nothing else.
  Section after = before;
  after.section_type = SectionType::kProgrammeArea;

  Section target;
  target.name = "Other";
  target.source = "assets/sweep.mkv";
  target.duration_frames = 50;
  SetNoiseBlockEnabled(&target, true);

  const Section result = ApplySectionEditDelta(before, after, target);
  EXPECT_EQ(result.section_type, SectionType::kProgrammeArea);
  // Untouched fields keep the target's own values.
  EXPECT_EQ(result.name, "Other");
  EXPECT_EQ(result.source, "assets/sweep.mkv");
  EXPECT_EQ(result.duration_frames, 50);
  EXPECT_TRUE(result.noise.enabled);
}

TEST(SectionBlockPresenterTest, ApplySectionEditDeltaNeverMirrorsName) {
  Section before;
  before.name = "Primary";
  Section after = before;
  after.name = "Primary renamed";

  Section target;
  target.name = "Other";
  const Section result = ApplySectionEditDelta(before, after, target);
  EXPECT_EQ(result.name, "Other");
}

TEST(SectionBlockPresenterTest,
     ApplySectionEditDeltaDurationFieldsPropagateAsUnit) {
  Section before;
  before.duration_frames = 200;
  Section after = before;
  after.duration_frames = 0;
  after.duration_frames_all = true;
  after.duration_frames_repeat = 3;

  Section target;
  target.duration_frames = 50;
  const Section result = ApplySectionEditDelta(before, after, target);
  EXPECT_TRUE(result.duration_frames_all);
  EXPECT_EQ(result.duration_frames, 0);
  EXPECT_EQ(result.duration_frames_repeat, 3);
}

TEST(SectionBlockPresenterTest,
     ApplySectionEditDeltaDropoutBlocksDiffIndependently) {
  Section before;
  SetScratchDropoutsEnabled(&before, true);
  before.dropouts.scratch.scale = 5;

  // The edit: enable random dropouts; scratch untouched.
  Section after = before;
  SetRandomDropoutsEnabled(&after, true);
  after.dropouts.random.scale = 7;

  // The target has its own scratch configuration that must survive.
  Section target;
  SetScratchDropoutsEnabled(&target, true);
  target.dropouts.scratch.scale = 12;

  const Section result = ApplySectionEditDelta(before, after, target);
  EXPECT_TRUE(result.dropouts.random.enabled);
  EXPECT_EQ(result.dropouts.random.scale, 7);
  EXPECT_TRUE(result.dropouts.scratch.enabled);
  EXPECT_EQ(result.dropouts.scratch.scale, 12);
}

TEST(SectionBlockPresenterTest,
     ApplySectionEditDeltaOptionalBlocksMirrorOnChange) {
  Section before;
  Section after = before;
  SetNoiseBlockEnabled(&after, true);
  after.noise.noise_db = 30.0;
  after.osd.overlays.push_back(MakeDefaultOsdOverlay());
  after.audio_channel_pairs.push_back(MakeDefaultAudioChannelPair(0));

  Section target;
  const Section result = ApplySectionEditDelta(before, after, target);
  EXPECT_TRUE(result.noise.enabled);
  EXPECT_DOUBLE_EQ(result.noise.noise_db, 30.0);
  ASSERT_EQ(result.osd.overlays.size(), 1u);
  ASSERT_EQ(result.audio_channel_pairs.size(), 1u);
  EXPECT_EQ(result.audio_channel_pairs[0].pair, 0);
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
