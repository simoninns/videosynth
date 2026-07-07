/*
 * File:        test_audio_fixture_projects.cpp
 * Module:      audio_fixture_project_tests
 * Purpose:     Runs the PAL/NTSC composite and Y/C audio fixtures end-to-end
 *              through the full pipeline and verifies the frame-locked WAV
 *              track name, format, and data length.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "videosynth/audio_wav_writer.h"
#include "videosynth/dropout_injection_stage.h"
#include "videosynth/generation_stage.h"
#include "videosynth/interfaces.h"
#include "videosynth/noise_injection_stage.h"
#include "videosynth/output_stage.h"
#include "videosynth/pipeline.h"
#include "videosynth/progressive_frame_source_probe.h"
#include "videosynth/project_validator.h"
#include "videosynth/timing_constants.h"
#include "videosynth/yaml_project_parser.h"

namespace videosynth {
namespace {

// Restores the process working directory on scope exit so relative fixture
// source/output paths resolve exactly as they do for the CLI (from the repo
// root), without leaking a directory change into sibling tests.
class ScopedCurrentPath {
 public:
  explicit ScopedCurrentPath(const std::filesystem::path& path)
      : previous_(std::filesystem::current_path()) {
    std::filesystem::current_path(path);
  }
  ~ScopedCurrentPath() {
    std::error_code ec;
    std::filesystem::current_path(previous_, ec);
  }
  ScopedCurrentPath(const ScopedCurrentPath&) = delete;
  ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

 private:
  std::filesystem::path previous_;
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

std::uint16_t ReadLe16(const std::vector<char>& bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(
      static_cast<unsigned char>(bytes[offset]) |
      (static_cast<unsigned char>(bytes[offset + 1]) << 8));
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

std::string FourCc(const std::vector<char>& bytes, std::size_t offset) {
  return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                     bytes.begin() + static_cast<std::ptrdiff_t>(offset + 4));
}

struct AudioFixture {
  std::string fixture_path;  // Relative to the repo root.
  Standard standard;
};

// Runs one audio fixture through the full pipeline and asserts that exactly one
// frame-locked WAV track is written with the expected name, format, and data
// length (frames x samples_per_frame x 4 bytes).
void RunAudioFixtureProject(const AudioFixture& fixture) {
  const std::filesystem::path repo_root(VIDEOSYNTH_SOURCE_DIR);
  const ScopedCurrentPath cwd_guard(repo_root);

  // Parse the fixture up front to derive expected paths and frame count. This
  // mirrors the Project the pipeline will build from the same file.
  SilentLogger logger;
  YamlProjectParser parser(&logger);
  const ParseResult parsed = parser.ParseFile(fixture.fixture_path);
  ASSERT_TRUE(parsed.ok) << fixture.fixture_path;

  int total_frames = 0;
  for (const Section& section : parsed.project.sections) {
    total_frames += section.duration_frames;
  }
  ASSERT_GT(total_frames, 0) << fixture.fixture_path;

  const std::filesystem::path video_path = parsed.project.output.video_path;
  const std::filesystem::path metadata_path =
      parsed.project.output.metadata_path;
  const std::filesystem::path audio_path =
      AudioWavWriter::DeriveAudioPath(parsed.project.output.video_path);

  std::filesystem::create_directories(video_path.parent_path());
  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
  std::filesystem::remove(audio_path);

  // Wire the pipeline exactly as main.cpp does.
  ProgressiveFrameSourceProbe progressive_frame_source_probe;
  ProjectValidator validator(&progressive_frame_source_probe, &logger);
  GenerationStage generation(&logger);
  NoiseInjectionStage noise_injection(&logger);
  DropoutInjectionStage dropout_injection(&logger);
  OutputStage output(&logger);
  AudioWavWriter audio_writer(&logger);

  VideoSynthPipeline pipeline(&parser, &validator, &generation,
                              &noise_injection, &dropout_injection, &output,
                              &logger, &audio_writer);
  RunOptions options;
  options.project_path = fixture.fixture_path;
  ASSERT_TRUE(pipeline.Run(options)) << fixture.fixture_path;

  // Exactly one WAV track, named from the composite/.y basename.
  ASSERT_TRUE(std::filesystem::exists(audio_path))
      << fixture.fixture_path << " -> " << audio_path;
  EXPECT_EQ(audio_path.filename().string(),
            video_path.stem().string() + "_audio_00.wav")
      << fixture.fixture_path;

  const std::vector<char> bytes = ReadFileBytes(audio_path);
  ASSERT_GE(bytes.size(), 44U) << fixture.fixture_path;

  // Valid RIFF/WAVE, 2-channel 16-bit PCM at the preset's header rate.
  EXPECT_EQ(FourCc(bytes, 0), "RIFF") << fixture.fixture_path;
  EXPECT_EQ(FourCc(bytes, 8), "WAVE") << fixture.fixture_path;
  EXPECT_EQ(FourCc(bytes, 36), "data") << fixture.fixture_path;
  EXPECT_EQ(ReadLe16(bytes, 20), 1U) << fixture.fixture_path;   // PCM tag.
  EXPECT_EQ(ReadLe16(bytes, 22), 2U) << fixture.fixture_path;   // Stereo.
  EXPECT_EQ(ReadLe16(bytes, 34), 16U) << fixture.fixture_path;  // Bits/sample.
  EXPECT_EQ(ReadLe32(bytes, 24), static_cast<std::uint32_t>(
                                     AudioHeaderSampleRateHz(fixture.standard)))
      << fixture.fixture_path;

  // data length == frames x samples_per_frame x 2ch x 2bytes.
  const std::uint32_t expected_data_bytes =
      static_cast<std::uint32_t>(total_frames) *
      static_cast<std::uint32_t>(AudioSamplesPerFrame(fixture.standard)) * 4U;
  EXPECT_EQ(ReadLe32(bytes, 40), expected_data_bytes) << fixture.fixture_path;
  EXPECT_EQ(bytes.size(), 44U + expected_data_bytes) << fixture.fixture_path;

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
  std::filesystem::remove(audio_path);
}

TEST(AudioFixtureProjectsTest, PalCompositeFixtureEmitsFrameLockedWav) {
  RunAudioFixtureProject(
      {"tests/projects/general/pal_audio.yaml", Standard::kPal});
}

TEST(AudioFixtureProjectsTest, NtscCompositeFixtureEmitsFrameLockedWav) {
  RunAudioFixtureProject(
      {"tests/projects/general/ntsc_audio.yaml", Standard::kNtsc});
}

TEST(AudioFixtureProjectsTest, PalYcFixtureEmitsFrameLockedWav) {
  RunAudioFixtureProject(
      {"tests/projects/general-yc/pal_audio.yaml", Standard::kPal});
}

TEST(AudioFixtureProjectsTest, NtscYcFixtureEmitsFrameLockedWav) {
  RunAudioFixtureProject(
      {"tests/projects/general-yc/ntsc_audio.yaml", Standard::kNtsc});
}

}  // namespace
}  // namespace videosynth
