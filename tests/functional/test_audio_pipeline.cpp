/*
 * File:        test_audio_pipeline.cpp
 * Module:      audio_pipeline_tests
 * Purpose:     Verifies frame-locked multi-pair audio generation and disc-skip
 *              routing of the WAV tracks through the pipeline using
 *              deterministic mocks.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "videosynth/audio_track_generator.h"
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
  bool EncodeFrame(const std::vector<SampleFixed>&,
                   const std::vector<SampleFixed>&, EncodedFrame*,
                   std::vector<std::string>* errors) const override {
    errors->clear();
    return true;
  }
  bool AppendEncodedFrame(const EncodedFrame&,
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

std::int32_t ReadLe24Signed(const std::vector<char>& bytes,
                            std::size_t offset) {
  std::uint32_t raw =
      static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1]))
       << 8) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2]))
       << 16);
  if ((raw & 0x800000U) != 0U) {
    raw |= 0xFF000000U;
  }
  return static_cast<std::int32_t>(raw);
}

AudioParameters Tone(AudioWaveform waveform, double frequency_hz) {
  AudioParameters channel;
  channel.enabled = true;
  channel.waveform = waveform;
  channel.frequency_hz = frequency_hz;
  channel.amplitude = 0.5;
  return channel;
}

Project MakeProject(Standard standard, int duration_frames,
                    std::vector<AudioChannelPair> pairs,
                    const std::filesystem::path& video_path) {
  Project project;
  project.cvbs_presets.video_standard_preset = standard;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  project.output.video_path = video_path.string();
  Section section;
  section.name = "Tone";
  section.type = "progressive";
  section.duration_frames = duration_frames;
  section.audio_channel_pairs = std::move(pairs);
  project.sections.push_back(section);
  return project;
}

// A single stereo pair 0 carrying a 1 kHz sine on both channels.
std::vector<AudioChannelPair> SinglePair() {
  AudioChannelPair pair;
  pair.pair = 0;
  pair.pair_specified = true;
  pair.left = Tone(AudioWaveform::kSine, 1000.0);
  pair.right = Tone(AudioWaveform::kSine, 1000.0);
  return {pair};
}

bool RunPipeline(Project project, AudioTrackGenerator* audio_generator) {
  MockParser parser;
  parser.result.ok = true;
  parser.result.project = std::move(project);
  MockValidator validator;
  validator.result.is_valid = true;
  MockGeneration generation;
  MockOutput output;
  SilentLogger logger;

  VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                              nullptr, &output, &logger, audio_generator);
  RunOptions options;
  options.project_path = "project.yaml";
  return pipeline.Run(options);
}

constexpr std::size_t kPalSamplesPerFrame = 1920;
constexpr std::size_t kStereo24BytesPerSample = 6;  // 2 channels × 3 bytes.
constexpr std::size_t kPalFrameBytes =
    kPalSamplesPerFrame * kStereo24BytesPerSample;

std::filesystem::path TempPath(const std::string& name) {
  return std::filesystem::temp_directory_path() / name;
}

TEST(AudioPipelineTest, NoAudioSectionProducesNoWavFile) {
  const std::filesystem::path video_path =
      TempPath("videosynth_audio_pipeline_none.cvbs");
  const std::filesystem::path audio_path =
      TempPath("videosynth_audio_pipeline_none_audio_0.wav");
  std::filesystem::remove(audio_path);

  AudioTrackGenerator generator;
  ASSERT_TRUE(
      RunPipeline(MakeProject(Standard::kPal, 3, {}, video_path), &generator));
  EXPECT_FALSE(std::filesystem::exists(audio_path));
}

TEST(AudioPipelineTest, FrameLockedSampleCountMatchesFrameCount) {
  const std::filesystem::path video_path =
      TempPath("videosynth_audio_pipeline_locked.cvbs");
  const std::filesystem::path audio_path =
      TempPath("videosynth_audio_pipeline_locked_audio_0.wav");
  std::filesystem::remove(audio_path);

  constexpr int kFrames = 4;
  AudioTrackGenerator generator;
  ASSERT_TRUE(RunPipeline(
      MakeProject(Standard::kPal, kFrames, SinglePair(), video_path),
      &generator));

  ASSERT_TRUE(std::filesystem::exists(audio_path));
  const std::vector<char> bytes = ReadFileBytes(audio_path);
  ASSERT_GE(bytes.size(), 44U);
  EXPECT_EQ(ReadLe32(bytes, 40), kFrames * kPalFrameBytes);
  EXPECT_EQ(bytes.size(), 44U + kFrames * kPalFrameBytes);

  std::filesystem::remove(audio_path);
}

TEST(AudioPipelineTest, ForwardSkipWithholdsAudioFrames) {
  const std::filesystem::path video_path =
      TempPath("videosynth_audio_pipeline_fwd.cvbs");
  const std::filesystem::path audio_path =
      TempPath("videosynth_audio_pipeline_fwd_audio_0.wav");
  std::filesystem::remove(audio_path);

  // 6 disc frames, forward skip of 2 at frame 3 (1-based) → 4 output frames.
  Project project = MakeProject(Standard::kPal, 6, SinglePair(), video_path);
  DiscSkip fwd;
  fwd.at_frame = 3;
  fwd.direction = DiscSkipDirection::kForward;
  fwd.count = 2;
  project.disc_skips.push_back(fwd);

  AudioTrackGenerator generator;
  ASSERT_TRUE(RunPipeline(std::move(project), &generator));

  ASSERT_TRUE(std::filesystem::exists(audio_path));
  const std::vector<char> bytes = ReadFileBytes(audio_path);
  ASSERT_GE(bytes.size(), 44U);
  EXPECT_EQ(ReadLe32(bytes, 40), 4U * kPalFrameBytes);

  std::filesystem::remove(audio_path);
}

TEST(AudioPipelineTest, BackwardSkipExtendsOutputSampleCount) {
  const std::filesystem::path video_path =
      TempPath("videosynth_audio_pipeline_bwd.cvbs");
  const std::filesystem::path audio_path =
      TempPath("videosynth_audio_pipeline_bwd_audio_0.wav");
  std::filesystem::remove(audio_path);

  // 5 disc frames, backward skip of 2 at frame 3 (1-based). Output order:
  // 0,1,2, copy(1), copy(2), 3,4 → 7 output frames. Audio follows the output
  // stream: a continuous tone sample-locked to all 7 stored frames.
  Project project = MakeProject(Standard::kPal, 5, SinglePair(), video_path);
  DiscSkip bwd;
  bwd.at_frame = 3;
  bwd.direction = DiscSkipDirection::kBackward;
  bwd.count = 2;
  project.disc_skips.push_back(bwd);

  AudioTrackGenerator generator;
  ASSERT_TRUE(RunPipeline(std::move(project), &generator));

  ASSERT_TRUE(std::filesystem::exists(audio_path));
  const std::vector<char> bytes = ReadFileBytes(audio_path);
  ASSERT_EQ(bytes.size(), 44U + 7U * kPalFrameBytes);
  EXPECT_EQ(ReadLe32(bytes, 40), 7U * kPalFrameBytes);

  std::filesystem::remove(audio_path);
}

TEST(AudioPipelineTest, MultiplePairsWriteSeparateFilesWithSilentChannel) {
  const std::filesystem::path video_path =
      TempPath("videosynth_audio_pipeline_multi.cvbs");
  const std::filesystem::path audio0 =
      TempPath("videosynth_audio_pipeline_multi_audio_0.wav");
  const std::filesystem::path audio3 =
      TempPath("videosynth_audio_pipeline_multi_audio_3.wav");
  std::filesystem::remove(audio0);
  std::filesystem::remove(audio3);

  // Pair 0: stereo sine. Pair 3: square left, silent right (right stored as
  // all zeros per SMPTE 272M §6.4).
  AudioChannelPair pair0;
  pair0.pair = 0;
  pair0.pair_specified = true;
  pair0.left = Tone(AudioWaveform::kSine, 1000.0);
  pair0.right = Tone(AudioWaveform::kSine, 1000.0);
  AudioChannelPair pair3;
  pair3.pair = 3;
  pair3.pair_specified = true;
  pair3.left = Tone(AudioWaveform::kSquare, 440.0);
  // pair3.right left disabled → silent.

  constexpr int kFrames = 2;
  AudioTrackGenerator generator;
  ASSERT_TRUE(RunPipeline(
      MakeProject(Standard::kPal, kFrames, {pair0, pair3}, video_path),
      &generator));

  ASSERT_TRUE(std::filesystem::exists(audio0));
  ASSERT_TRUE(std::filesystem::exists(audio3));

  const std::vector<char> bytes0 = ReadFileBytes(audio0);
  const std::vector<char> bytes3 = ReadFileBytes(audio3);
  EXPECT_EQ(ReadLe32(bytes0, 40), kFrames * kPalFrameBytes);
  EXPECT_EQ(ReadLe32(bytes3, 40), kFrames * kPalFrameBytes);

  // Pair 3's right channel is silent: every second (odd) 24-bit sample is 0.
  const std::size_t total_positions = kFrames * kPalSamplesPerFrame;
  for (std::size_t i = 0; i < total_positions; ++i) {
    const std::size_t right_offset = 44U + i * kStereo24BytesPerSample + 3U;
    EXPECT_EQ(ReadLe24Signed(bytes3, right_offset), 0)
        << "right channel not silent at position " << i;
  }

  // Different waveforms → the two files' left channels differ.
  EXPECT_NE(bytes0, bytes3);

  std::filesystem::remove(audio0);
  std::filesystem::remove(audio3);
}

TEST(AudioPipelineTest, NtscUsesVariablePerFrameSampleCounts) {
  const std::filesystem::path video_path =
      TempPath("videosynth_audio_pipeline_ntsc.cvbs");
  const std::filesystem::path audio_path =
      TempPath("videosynth_audio_pipeline_ntsc_audio_0.wav");
  std::filesystem::remove(audio_path);

  // One full 5-frame NTSC audio-frame sequence: 1602+1601+1602+1601+1602 =
  // 8008 sample positions.
  constexpr int kFrames = 5;
  AudioTrackGenerator generator;
  ASSERT_TRUE(RunPipeline(
      MakeProject(Standard::kNtsc, kFrames, SinglePair(), video_path),
      &generator));

  ASSERT_TRUE(std::filesystem::exists(audio_path));
  const std::vector<char> bytes = ReadFileBytes(audio_path);
  const std::uint32_t expected_positions = 8008U;
  EXPECT_EQ(ReadLe32(bytes, 40), expected_positions * kStereo24BytesPerSample);

  std::filesystem::remove(audio_path);
}

}  // namespace
}  // namespace videosynth
