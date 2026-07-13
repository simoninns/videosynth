/*
 * File:        test_audio_track_generator.cpp
 * Module:      audio_track_generator_tests
 * Purpose:     Validates the multi-pair AudioTrackGenerator: per-pair file
 *              emission, output-position sample counts, and per-section silence
 *              for undeclared pairs / channels.
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

#include "videosynth/audio_track_generator.h"
#include "videosynth/audio_wav_writer.h"
#include "videosynth/model.h"

namespace videosynth {
namespace {

std::vector<char> ReadFileBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
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

AudioParameters ActiveTone() {
  AudioParameters channel;
  channel.enabled = true;
  channel.waveform = AudioWaveform::kSquare;  // Non-zero on the first sample.
  channel.frequency_hz = 1000.0;
  channel.amplitude = 0.5;
  return channel;
}

AudioChannelPair MakePair(int pair, bool left, bool right) {
  AudioChannelPair channel_pair;
  channel_pair.pair = pair;
  channel_pair.pair_specified = true;
  if (left) {
    channel_pair.left = ActiveTone();
  }
  if (right) {
    channel_pair.right = ActiveTone();
  }
  return channel_pair;
}

Project MakeProject(const std::filesystem::path& video_path) {
  Project project;
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.output.video_path = video_path.string();
  return project;
}

constexpr std::size_t kPalSamplesPerFrame = 1920;
constexpr std::size_t kStereoBytes = 6;
constexpr std::size_t kFrameBytes = kPalSamplesPerFrame * kStereoBytes;

// True if every 24-bit sample in the given output frame of `bytes` is zero.
bool FrameIsSilent(const std::vector<char>& bytes, std::size_t frame_index) {
  const std::size_t start = 44U + frame_index * kFrameBytes;
  for (std::size_t i = 0; i < kPalSamplesPerFrame * 2U; ++i) {
    if (ReadLe24Signed(bytes, start + i * 3U) != 0) {
      return false;
    }
  }
  return true;
}

std::filesystem::path TempPath(const std::string& name) {
  return std::filesystem::temp_directory_path() / name;
}

TEST(AudioTrackGeneratorTest, InactiveWhenNoPairsDeclared) {
  const std::filesystem::path video_path =
      TempPath("videosynth_atg_none.composite");
  const Project project = MakeProject(video_path);

  AudioTrackGenerator generator;
  std::vector<std::string> errors;
  Section section;
  const std::vector<const Section*> frames = {&section, &section};
  ASSERT_TRUE(generator.Begin(project, frames, &errors));
  EXPECT_FALSE(generator.active());
  EXPECT_TRUE(generator.channel_pairs().empty());
  // EmitFrame and Finalize are no-ops when inactive.
  EXPECT_TRUE(generator.EmitFrame(0, &errors));
  EXPECT_TRUE(generator.Finalize(&errors));
}

TEST(AudioTrackGeneratorTest, EmitsOneFilePerDeclaredPairWithSilence) {
  const std::filesystem::path video_path =
      TempPath("videosynth_atg_multi.composite");
  const std::filesystem::path audio0 =
      TempPath("videosynth_atg_multi_audio_0.wav");
  const std::filesystem::path audio1 =
      TempPath("videosynth_atg_multi_audio_1.wav");
  std::filesystem::remove(audio0);
  std::filesystem::remove(audio1);

  Project project = MakeProject(video_path);

  // Section A declares pair 0 (stereo). Section B declares pair 1 (left only).
  Section section_a;
  section_a.name = "A";
  section_a.audio_channel_pairs = {MakePair(0, /*left=*/true, /*right=*/true)};
  Section section_b;
  section_b.name = "B";
  section_b.audio_channel_pairs = {MakePair(1, /*left=*/true, /*right=*/false)};
  project.sections = {section_a, section_b};

  // Output order: A, A, B, B.
  const std::vector<const Section*> frames = {
      &project.sections[0], &project.sections[0], &project.sections[1],
      &project.sections[1]};

  AudioTrackGenerator generator;
  std::vector<std::string> errors;
  ASSERT_TRUE(generator.Begin(project, frames, &errors))
      << (errors.empty() ? "" : errors.front());
  EXPECT_TRUE(generator.active());
  ASSERT_EQ(generator.channel_pairs().size(), 2U);
  EXPECT_EQ(generator.channel_pairs()[0], 0);
  EXPECT_EQ(generator.channel_pairs()[1], 1);

  for (std::size_t k = 0; k < frames.size(); ++k) {
    ASSERT_TRUE(generator.EmitFrame(k, &errors))
        << (errors.empty() ? "" : errors.front());
  }
  ASSERT_TRUE(generator.Finalize(&errors));

  ASSERT_TRUE(std::filesystem::exists(audio0));
  ASSERT_TRUE(std::filesystem::exists(audio1));

  const std::vector<char> bytes0 = ReadFileBytes(audio0);
  const std::vector<char> bytes1 = ReadFileBytes(audio1);
  ASSERT_EQ(bytes0.size(), 44U + 4U * kFrameBytes);
  ASSERT_EQ(bytes1.size(), 44U + 4U * kFrameBytes);

  // Pair 0 is declared only by section A (output frames 0,1): frames 2,3 (B)
  // are silent, frames 0,1 are not.
  EXPECT_FALSE(FrameIsSilent(bytes0, 0));
  EXPECT_FALSE(FrameIsSilent(bytes0, 1));
  EXPECT_TRUE(FrameIsSilent(bytes0, 2));
  EXPECT_TRUE(FrameIsSilent(bytes0, 3));

  // Pair 1 is declared only by section B (output frames 2,3): frames 0,1 (A)
  // are silent, frames 2,3 are not.
  EXPECT_TRUE(FrameIsSilent(bytes1, 0));
  EXPECT_TRUE(FrameIsSilent(bytes1, 1));
  EXPECT_FALSE(FrameIsSilent(bytes1, 2));
  EXPECT_FALSE(FrameIsSilent(bytes1, 3));

  std::filesystem::remove(audio0);
  std::filesystem::remove(audio1);
}

TEST(AudioTrackGeneratorTest, SilentChannelWithinDeclaredPairIsZero) {
  const std::filesystem::path video_path =
      TempPath("videosynth_atg_silentchan.composite");
  const std::filesystem::path audio0 =
      TempPath("videosynth_atg_silentchan_audio_0.wav");
  std::filesystem::remove(audio0);

  Project project = MakeProject(video_path);
  Section section;
  section.name = "Mono";
  section.audio_channel_pairs = {MakePair(0, /*left=*/true, /*right=*/false)};
  project.sections = {section};
  const std::vector<const Section*> frames = {&project.sections[0]};

  AudioTrackGenerator generator;
  std::vector<std::string> errors;
  ASSERT_TRUE(generator.Begin(project, frames, &errors));
  ASSERT_TRUE(generator.EmitFrame(0, &errors));
  ASSERT_TRUE(generator.Finalize(&errors));

  const std::vector<char> bytes = ReadFileBytes(audio0);
  ASSERT_EQ(bytes.size(), 44U + kFrameBytes);
  bool any_left_nonzero = false;
  for (std::size_t i = 0; i < kPalSamplesPerFrame; ++i) {
    const std::size_t left_offset = 44U + i * kStereoBytes;
    const std::size_t right_offset = left_offset + 3U;
    if (ReadLe24Signed(bytes, left_offset) != 0) {
      any_left_nonzero = true;
    }
    EXPECT_EQ(ReadLe24Signed(bytes, right_offset), 0);  // Right is silent.
  }
  EXPECT_TRUE(any_left_nonzero);

  std::filesystem::remove(audio0);
}

}  // namespace
}  // namespace videosynth
