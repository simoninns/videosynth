/*
 * File:        test_audio_efm_writer.cpp
 * Module:      audio_efm_writer_tests
 * Purpose:     Validates AudioEfmWriter path derivation and the T-value stream
 *              it writes beside the CVBS output.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "videosynth/audio_efm_writer.h"
#include "videosynth/efm/efm_modulator.h"
#include "videosynth/efm/subcode_generator.h"
#include "videosynth/model.h"

namespace videosynth {
namespace {

std::vector<std::uint8_t> ReadFileBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
  return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

Project MakeAudioProject(Standard standard, const std::string& video_path) {
  Project project;
  project.cvbs_presets.video_standard_preset = standard;
  project.output.video_path = video_path;
  return project;
}

// A minimal lead-in / programme / lead-out layout in subcode sections.
efm::TrackTable MakeTrackTable() {
  efm::TrackTable table;
  table.video_system = efm::VideoSystem::kPal;
  table.entries.push_back(
      efm::TrackTableEntry{efm::SubcodeArea::kLeadIn, 0, 0, 75});
  table.entries.push_back(
      efm::TrackTableEntry{efm::SubcodeArea::kProgramme, 1, 75, 750});
  table.entries.push_back(
      efm::TrackTableEntry{efm::SubcodeArea::kLeadOut, 0, 825, 75});
  return table;
}

// One PAL video frame of 44.1 kHz audio is 1764 sample positions
// (IEC 60856:1986 Amd 2, 13.2); a shorter block keeps the test fast.
std::vector<std::int32_t> MakeFrameSamples(std::size_t count, int seed) {
  std::vector<std::int32_t> samples;
  samples.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    samples.push_back(
        static_cast<std::int32_t>(
            ((index + static_cast<std::size_t>(seed)) * 65537U) % 8388608U) -
        4194304);
  }
  return samples;
}

// ---------------------------------------------------------------------------
// Path derivation (pure; unit-labelled).
// ---------------------------------------------------------------------------

TEST(AudioEfmWriterPathTest, StripsCompositeSuffix) {
  EXPECT_EQ(AudioEfmWriter::DeriveAudioPath("out/clip.composite", 0),
            "out/clip_audio_0.efm");
}

TEST(AudioEfmWriterPathTest, StripsLumaSuffix) {
  EXPECT_EQ(AudioEfmWriter::DeriveAudioPath("out/clip.y", 3),
            "out/clip_audio_3.efm");
}

TEST(AudioEfmWriterPathTest, AppendsWhenNoKnownSuffix) {
  EXPECT_EQ(AudioEfmWriter::DeriveAudioPath("out/clip", 7),
            "out/clip_audio_7.efm");
}

// ---------------------------------------------------------------------------
// T-value output (touches the filesystem; functional-labelled).
// ---------------------------------------------------------------------------

TEST(AudioEfmWriterTest, WritesTValueStreamForTheSelectedPair) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() / "videosynth_audio_efm.composite";
  const std::filesystem::path expected_audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_audio_2.efm";
  std::filesystem::remove(expected_audio_path);

  const Project project = MakeAudioProject(Standard::kPal, video_path.string());

  AudioEfmWriter writer;
  std::vector<std::string> errors;
  ASSERT_TRUE(writer.BeginWrite(project, 2, MakeTrackTable(), &errors))
      << (errors.empty() ? "" : errors.front());

  // Two video frames of 600 sample positions each: 200 whole F1 frames.
  constexpr std::size_t kSamplesPerFrame = 600;
  for (int frame = 0; frame < 2; ++frame) {
    ASSERT_TRUE(writer.AppendFrameAudio(
        MakeFrameSamples(kSamplesPerFrame, frame),
        MakeFrameSamples(kSamplesPerFrame, frame + 10), &errors))
        << (errors.empty() ? "" : errors.front());
  }
  ASSERT_TRUE(writer.FinalizeWrite(&errors))
      << (errors.empty() ? "" : errors.front());

  ASSERT_TRUE(std::filesystem::exists(expected_audio_path));
  const std::vector<std::uint8_t> bytes = ReadFileBytes(expected_audio_path);
  ASSERT_FALSE(bytes.empty());

  // Every byte is a pit or land run length between T_min and T_max
  // (IEC 60908-1999, clause 13).
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    ASSERT_GE(bytes[index], efm::kMinRunLengthT) << "T value " << index;
    ASSERT_LE(bytes[index], efm::kMaxRunLengthT) << "T value " << index;
  }

  // The run lengths tile the channel frames of the encoded stream, one channel
  // frame per six stereo samples plus the flushed CIRC pipeline.
  const std::size_t expected_frames =
      ((2U * kSamplesPerFrame) / efm::kStereoSamplesPerF1Frame) +
      efm::kCircPipelineLatencyFrames;
  std::size_t total_bits = 0;
  for (const std::uint8_t t_value : bytes) {
    total_bits += t_value;
  }
  EXPECT_GE(total_bits, expected_frames * efm::kChannelBitsPerFrame);

  std::filesystem::remove(expected_audio_path);
}

TEST(AudioEfmWriterTest, RejectsMismatchedChannelLengths) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_mismatch.composite";
  const std::filesystem::path expected_audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_mismatch_audio_0.efm";
  std::filesystem::remove(expected_audio_path);

  const Project project = MakeAudioProject(Standard::kPal, video_path.string());

  AudioEfmWriter writer;
  std::vector<std::string> errors;
  ASSERT_TRUE(writer.BeginWrite(project, 0, MakeTrackTable(), &errors));

  EXPECT_FALSE(writer.AppendFrameAudio({0, 1, 2}, {0, 1}, &errors));
  EXPECT_FALSE(errors.empty());

  writer.AbortWrite();
  EXPECT_FALSE(std::filesystem::exists(expected_audio_path));
}

TEST(AudioEfmWriterTest, RejectsAnInvalidTrackTable) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_bad_table.composite";
  const std::filesystem::path expected_audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_bad_table_audio_1.efm";
  std::filesystem::remove(expected_audio_path);

  const Project project = MakeAudioProject(Standard::kPal, video_path.string());

  AudioEfmWriter writer;
  std::vector<std::string> errors;
  EXPECT_FALSE(writer.BeginWrite(project, 1, efm::TrackTable{}, &errors));
  EXPECT_FALSE(errors.empty());
  EXPECT_FALSE(std::filesystem::exists(expected_audio_path));
}

TEST(AudioEfmWriterTest, AbortRemovesThePartialFile) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_abort.composite";
  const std::filesystem::path expected_audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_abort_audio_4.efm";
  std::filesystem::remove(expected_audio_path);

  const Project project = MakeAudioProject(Standard::kPal, video_path.string());

  AudioEfmWriter writer;
  std::vector<std::string> errors;
  ASSERT_TRUE(writer.BeginWrite(project, 4, MakeTrackTable(), &errors));
  ASSERT_TRUE(writer.AppendFrameAudio(MakeFrameSamples(600, 0),
                                      MakeFrameSamples(600, 1), &errors));
  ASSERT_TRUE(std::filesystem::exists(expected_audio_path));

  writer.AbortWrite();

  EXPECT_FALSE(std::filesystem::exists(expected_audio_path));
  // A second abort is a no-op, and finalizing without a session fails.
  writer.AbortWrite();
  EXPECT_FALSE(writer.FinalizeWrite(&errors));
}

}  // namespace
}  // namespace videosynth
