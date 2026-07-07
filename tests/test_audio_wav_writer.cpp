/*
 * File:        test_audio_wav_writer.cpp
 * Module:      audio_wav_writer_tests
 * Purpose:     Validates AudioWavWriter path derivation and the RIFF/WAVE
 *              stereo 16-bit PCM output it produces.
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
#include "videosynth/model.h"

namespace videosynth {
namespace {

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

std::uint16_t ReadLe16(const std::vector<char>& bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(
      static_cast<unsigned char>(bytes[offset]) |
      (static_cast<unsigned char>(bytes[offset + 1]) << 8));
}

std::string FourCc(const std::vector<char>& bytes, std::size_t offset) {
  return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                     bytes.begin() + static_cast<std::ptrdiff_t>(offset + 4));
}

Project MakeAudioProject(Standard standard, const std::string& video_path) {
  Project project;
  project.cvbs_presets.video_standard_preset = standard;
  project.output.video_path = video_path;
  Section section;
  section.audio.enabled = true;
  project.sections.push_back(section);
  return project;
}

// ---------------------------------------------------------------------------
// Path derivation (pure; unit-labelled).
// ---------------------------------------------------------------------------

TEST(AudioWavWriterPathTest, StripsCompositeSuffix) {
  EXPECT_EQ(AudioWavWriter::DeriveAudioPath("out/clip.composite"),
            "out/clip_audio_00.wav");
}

TEST(AudioWavWriterPathTest, StripsLumaSuffix) {
  EXPECT_EQ(AudioWavWriter::DeriveAudioPath("out/clip.y"),
            "out/clip_audio_00.wav");
}

TEST(AudioWavWriterPathTest, AppendsWhenNoKnownSuffix) {
  EXPECT_EQ(AudioWavWriter::DeriveAudioPath("out/clip"),
            "out/clip_audio_00.wav");
}

// ---------------------------------------------------------------------------
// RIFF/WAVE output (touches the filesystem; functional-labelled).
// ---------------------------------------------------------------------------

TEST(AudioWavWriterTest, WritesValidPalStereoPcm) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_wav_pal.composite";
  const std::filesystem::path expected_audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_wav_pal_audio_00.wav";
  std::filesystem::remove(expected_audio_path);

  const Project project = MakeAudioProject(Standard::kPal, video_path.string());

  AudioWavWriter writer;
  std::vector<std::string> errors;
  ASSERT_TRUE(writer.BeginWrite(project, &errors))
      << (errors.empty() ? "" : errors.front());

  // Two frames of 4 mono samples each.
  const std::vector<std::int16_t> frame0 = {0, 100, -100, 32767};
  const std::vector<std::int16_t> frame1 = {-32768, 5, -5, 0};
  ASSERT_TRUE(writer.AppendFrameAudio(frame0, &errors));
  ASSERT_TRUE(writer.AppendFrameAudio(frame1, &errors));
  ASSERT_TRUE(writer.FinalizeWrite(&errors));

  ASSERT_TRUE(std::filesystem::exists(expected_audio_path));
  const std::vector<char> bytes = ReadFileBytes(expected_audio_path);
  ASSERT_GE(bytes.size(), 44U);

  const std::size_t mono_samples = frame0.size() + frame1.size();
  const std::uint32_t data_bytes =
      static_cast<std::uint32_t>(mono_samples * 2U * 2U);  // stereo, 16-bit.

  EXPECT_EQ(FourCc(bytes, 0), "RIFF");
  EXPECT_EQ(ReadLe32(bytes, 4), 36U + data_bytes);
  EXPECT_EQ(FourCc(bytes, 8), "WAVE");
  EXPECT_EQ(FourCc(bytes, 12), "fmt ");
  EXPECT_EQ(ReadLe32(bytes, 16), 16U);
  EXPECT_EQ(ReadLe16(bytes, 20), 1U);  // PCM.
  EXPECT_EQ(ReadLe16(bytes, 22), 2U);  // Channels.
  EXPECT_EQ(ReadLe32(bytes, 24), 44100U);
  EXPECT_EQ(ReadLe32(bytes, 28), 44100U * 2U * 2U);  // Byte rate.
  EXPECT_EQ(ReadLe16(bytes, 32), 4U);                // Block align.
  EXPECT_EQ(ReadLe16(bytes, 34), 16U);               // Bits per sample.
  EXPECT_EQ(FourCc(bytes, 36), "data");
  EXPECT_EQ(ReadLe32(bytes, 40), data_bytes);
  EXPECT_EQ(bytes.size(), 44U + data_bytes);

  // First mono sample duplicated to L and R.
  EXPECT_EQ(ReadLe16(bytes, 44), static_cast<std::uint16_t>(frame0[0]));
  EXPECT_EQ(ReadLe16(bytes, 46), static_cast<std::uint16_t>(frame0[0]));
  EXPECT_EQ(ReadLe16(bytes, 48), static_cast<std::uint16_t>(frame0[1]));
  EXPECT_EQ(ReadLe16(bytes, 50), static_cast<std::uint16_t>(frame0[1]));

  std::filesystem::remove(expected_audio_path);
}

TEST(AudioWavWriterTest, UsesSystemMHeaderRateForNtsc) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() / "videosynth_audio_wav_ntsc.y";
  const std::filesystem::path expected_audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_wav_ntsc_audio_00.wav";
  std::filesystem::remove(expected_audio_path);

  const Project project =
      MakeAudioProject(Standard::kNtsc, video_path.string());

  AudioWavWriter writer;
  std::vector<std::string> errors;
  ASSERT_TRUE(writer.BeginWrite(project, &errors));
  ASSERT_TRUE(writer.AppendFrameAudio({1, 2, 3}, &errors));
  ASSERT_TRUE(writer.FinalizeWrite(&errors));

  ASSERT_TRUE(std::filesystem::exists(expected_audio_path));
  const std::vector<char> bytes = ReadFileBytes(expected_audio_path);
  ASSERT_GE(bytes.size(), 44U);
  EXPECT_EQ(ReadLe32(bytes, 24), 44056U);
  EXPECT_EQ(ReadLe32(bytes, 28), 44056U * 2U * 2U);

  std::filesystem::remove(expected_audio_path);
}

}  // namespace
}  // namespace videosynth
