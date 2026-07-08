/*
 * File:        test_audio_pipeline.cpp
 * Module:      audio_pipeline_tests
 * Purpose:     Verifies frame-locked audio generation and disc-skip routing of
 *              the WAV track through the pipeline using deterministic mocks.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "videosynth/audio_wav_writer.h"
#include "videosynth/pipeline.h"
#include "videosynth/timing_constants.h"

namespace videosynth {
namespace {

class MockParser final : public IProjectParser {
 public:
  ParseResult result;
  ParseResult ParseFile(const std::string&) override { return result; }
};

class MockValidator final : public IProjectValidator {
 public:
  ValidationResult result;
  ValidationResult Validate(const Project&) override { return result; }
};

// Builds one schedule entry per section frame and emits 8 zero video samples
// per frame; audio is synthesised independently by the pipeline.
class MockGeneration final : public IGenerationStage {
 public:
  bool BuildFrameSchedule(const Project& project,
                          std::vector<FrameScheduleItem>* out_schedule,
                          std::vector<std::string>* errors) override {
    out_schedule->clear();
    for (const Section& s : project.sections) {
      for (int i = 0; i < s.duration_frames; ++i) {
        out_schedule->push_back(
            FrameScheduleItem{.section = &s, .source_frame_index = i});
      }
    }
    errors->clear();
    return true;
  }

  bool GenerateFrameBatch(const Project&, const std::vector<FrameScheduleItem>&,
                          std::size_t, std::size_t frame_count,
                          std::vector<SampleFixed>* out_y_mv,
                          std::vector<SampleFixed>* out_c_mv,
                          std::vector<std::string>* errors) override {
    out_y_mv->assign(frame_count * 8U, 0);
    out_c_mv->assign(frame_count * 8U, 0);
    errors->clear();
    return true;
  }

  bool Generate(const Project&, std::vector<SampleFixed>* out_y_mv,
                std::vector<SampleFixed>* out_c_mv,
                std::vector<std::string>* errors) override {
    out_y_mv->assign(8, 0);
    out_c_mv->assign(8, 0);
    errors->clear();
    return true;
  }
};

class MockOutput final : public IOutputStage {
 public:
  bool BeginWrite(const Project&, std::size_t,
                  std::vector<std::string>* errors) override {
    errors->clear();
    return true;
  }
  bool AppendSamples(const std::vector<SampleFixed>&,
                     const std::vector<SampleFixed>&,
                     std::vector<std::string>* errors) override {
    errors->clear();
    return true;
  }
  bool FinalizeWrite(std::vector<std::string>* errors) override {
    errors->clear();
    return true;
  }
  void AbortWrite() override {}
  bool Write(const Project&, const std::vector<SampleFixed>&,
             const std::vector<SampleFixed>&,
             std::vector<std::string>* errors) override {
    errors->clear();
    return true;
  }
};

class SilentLogger final : public ILogger {
 public:
  void Info(const std::string&) override {}
  void Warning(const std::string&) override {}
  void Error(const std::string&) override {}
  void Debug(const std::string&) override {}
  void Trace(const std::string&) override {}
};

std::vector<char> ReadFileBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
}

std::uint32_t ReadLe32(const std::vector<char>& bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) |
         (static_cast<std::uint32_t>(
              static_cast<unsigned char>(bytes[offset + 1]))
          << 8) |
         (static_cast<std::uint32_t>(
              static_cast<unsigned char>(bytes[offset + 2]))
          << 16) |
         (static_cast<std::uint32_t>(
              static_cast<unsigned char>(bytes[offset + 3]))
          << 24);
}

Project MakeProject(int duration_frames, bool enable_audio,
                    const std::filesystem::path& video_path) {
  Project project;
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  project.output.video_path = video_path.string();
  Section section;
  section.name = "Tone";
  section.type = "progressive";
  section.duration_frames = duration_frames;
  section.audio.enabled = enable_audio;
  section.audio.waveform = AudioWaveform::kSine;
  section.audio.frequency_hz = 1000.0;
  section.audio.amplitude = 0.5;
  project.sections.push_back(section);
  return project;
}

bool RunPipeline(Project project, AudioWavWriter* audio_writer) {
  MockParser parser;
  parser.result.ok = true;
  parser.result.project = std::move(project);
  MockValidator validator;
  validator.result.is_valid = true;
  MockGeneration generation;
  MockOutput output;
  SilentLogger logger;

  VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                              nullptr, &output, &logger, audio_writer);
  RunOptions options;
  options.project_path = "project.yaml";
  return pipeline.Run(options);
}

constexpr std::size_t kPalSamplesPerFrame = 1764;
constexpr std::size_t kStereo16BytesPerSample = 4;
constexpr std::size_t kFrameBytes =
    kPalSamplesPerFrame * kStereo16BytesPerSample;

TEST(AudioPipelineTest, NoAudioSectionProducesNoWavFile) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_pipeline_none.composite";
  const std::filesystem::path audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_pipeline_none_audio_00.wav";
  std::filesystem::remove(audio_path);

  AudioWavWriter writer;
  ASSERT_TRUE(
      RunPipeline(MakeProject(3, /*enable_audio=*/false, video_path), &writer));
  EXPECT_FALSE(std::filesystem::exists(audio_path));
}

TEST(AudioPipelineTest, FrameLockedSampleCountMatchesFrameCount) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_pipeline_locked.composite";
  const std::filesystem::path audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_pipeline_locked_audio_00.wav";
  std::filesystem::remove(audio_path);

  constexpr int kFrames = 4;
  AudioWavWriter writer;
  ASSERT_TRUE(RunPipeline(
      MakeProject(kFrames, /*enable_audio=*/true, video_path), &writer));

  ASSERT_TRUE(std::filesystem::exists(audio_path));
  const std::vector<char> bytes = ReadFileBytes(audio_path);
  ASSERT_GE(bytes.size(), 44U);
  const std::uint32_t data_bytes = ReadLe32(bytes, 40);
  EXPECT_EQ(data_bytes, kFrames * kFrameBytes);
  EXPECT_EQ(bytes.size(), 44U + kFrames * kFrameBytes);

  std::filesystem::remove(audio_path);
}

TEST(AudioPipelineTest, ForwardSkipWithholdsAudioFrames) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_pipeline_fwd.composite";
  const std::filesystem::path audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_pipeline_fwd_audio_00.wav";
  std::filesystem::remove(audio_path);

  // 6 disc frames, forward skip of 2 at frame 3 (1-based) → 4 output frames.
  Project project = MakeProject(6, /*enable_audio=*/true, video_path);
  DiscSkip fwd;
  fwd.at_frame = 3;
  fwd.direction = DiscSkipDirection::kForward;
  fwd.count = 2;
  project.disc_skips.push_back(fwd);

  AudioWavWriter writer;
  ASSERT_TRUE(RunPipeline(std::move(project), &writer));

  ASSERT_TRUE(std::filesystem::exists(audio_path));
  const std::vector<char> bytes = ReadFileBytes(audio_path);
  ASSERT_GE(bytes.size(), 44U);
  EXPECT_EQ(ReadLe32(bytes, 40), 4U * kFrameBytes);

  std::filesystem::remove(audio_path);
}

TEST(AudioPipelineTest, BackwardSkipReplaysByteIdenticalAudio) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_pipeline_bwd.composite";
  const std::filesystem::path audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_pipeline_bwd_audio_00.wav";
  std::filesystem::remove(audio_path);

  // 5 disc frames, backward skip of 2 at frame 3 (1-based). Output order:
  // 0,1,2, copy(1), copy(2), 3,4 → 7 output frames.
  Project project = MakeProject(5, /*enable_audio=*/true, video_path);
  DiscSkip bwd;
  bwd.at_frame = 3;
  bwd.direction = DiscSkipDirection::kBackward;
  bwd.count = 2;
  project.disc_skips.push_back(bwd);

  AudioWavWriter writer;
  ASSERT_TRUE(RunPipeline(std::move(project), &writer));

  ASSERT_TRUE(std::filesystem::exists(audio_path));
  const std::vector<char> bytes = ReadFileBytes(audio_path);
  ASSERT_EQ(bytes.size(), 44U + 7U * kFrameBytes);
  EXPECT_EQ(ReadLe32(bytes, 40), 7U * kFrameBytes);

  auto frame_range = [&](std::size_t frame_index) {
    const std::size_t start = 44U + frame_index * kFrameBytes;
    return std::vector<char>(
        bytes.begin() + static_cast<std::ptrdiff_t>(start),
        bytes.begin() + static_cast<std::ptrdiff_t>(start + kFrameBytes));
  };

  // Output frame 3 replays disc frame 1 (output frame 1); frame 4 replays
  // disc frame 2 (output frame 2).
  EXPECT_EQ(frame_range(3), frame_range(1));
  EXPECT_EQ(frame_range(4), frame_range(2));

  std::filesystem::remove(audio_path);
}

}  // namespace
}  // namespace videosynth
