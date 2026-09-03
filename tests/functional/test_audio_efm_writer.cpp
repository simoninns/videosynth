/*
 * File:        test_audio_efm_writer.cpp
 * Module:      audio_efm_writer_tests
 * Purpose:     Validates the T-value stream AudioEfmWriter writes beside the
 *              CVBS output and the conformance of the `.efm.meta` frame-index
 *              sidecar. Path derivation is covered by the unit suite in
 *              tests/unit/test_audio_efm_writer_paths.cpp.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>
#include <sqlite3.h>

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
#include "videosynth/efm/t_value_byte.h"
#include "videosynth/model.h"

namespace videosynth {
namespace {

std::vector<std::uint8_t> ReadFileBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
  return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

// One row of the sidecar's efm_frame table.
struct EfmFrameRow {
  int cvbs_file_id = 0;
  std::int64_t frame_id = 0;
  std::int64_t t_value_offset = 0;
  std::int64_t t_value_count = 0;
};

std::vector<EfmFrameRow> ReadEfmFrameRows(const std::filesystem::path& path) {
  std::vector<EfmFrameRow> rows;
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    sqlite3_close(db);
    return rows;
  }
  const char* query_sql =
      "SELECT cvbs_file_id, frame_id, t_value_offset, t_value_count "
      "FROM efm_frame ORDER BY frame_id;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, query_sql, -1, &stmt, nullptr) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      EfmFrameRow row;
      row.cvbs_file_id = sqlite3_column_int(stmt, 0);
      row.frame_id = sqlite3_column_int64(stmt, 1);
      row.t_value_offset = sqlite3_column_int64(stmt, 2);
      row.t_value_count = sqlite3_column_int64(stmt, 3);
      rows.push_back(row);
    }
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return rows;
}

// Reads PRAGMA user_version from a sidecar database.
int ReadUserVersion(const std::filesystem::path& path) {
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    sqlite3_close(db);
    return -1;
  }
  int version = -1;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, nullptr) ==
      SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      version = sqlite3_column_int(stmt, 0);
    }
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return version;
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

TEST(AudioEfmWriterTest, WritesTValueStreamForTheSelectedPair) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() / "videosynth_audio_efm.cvbs";
  const std::filesystem::path expected_audio_path =
      std::filesystem::temp_directory_path() / "videosynth_audio_efm.efm";
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

  // efm-extension-format.md, "Binary Data File": every byte carries a pit or
  // land run length between T_min and T_max (IEC 60908-1999, clause 13) in bits
  // 3-0, and the producer's doubt about it in bits 7-4. The stream is
  // synthesised, so it is written at maximum confidence throughout.
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    ASSERT_EQ(efm::DoubtOfByte(bytes[index]), efm::kNoDoubt)
        << "T value " << index;
    ASSERT_GE(efm::TValueOfByte(bytes[index]), efm::kMinRunLengthT)
        << "T value " << index;
    ASSERT_LE(efm::TValueOfByte(bytes[index]), efm::kMaxRunLengthT)
        << "T value " << index;
  }

  // The run lengths tile the channel frames of the encoded stream, one channel
  // frame per six stereo samples plus the flushed CIRC pipeline.
  const std::size_t expected_frames =
      ((2U * kSamplesPerFrame) / efm::kStereoSamplesPerF1Frame) +
      efm::kCircDrainFrames;
  std::size_t total_bits = 0;
  for (const std::uint8_t byte : bytes) {
    total_bits += efm::TValueOfByte(byte);
  }
  EXPECT_GE(total_bits, expected_frames * efm::kChannelBitsPerFrame);

  std::filesystem::remove(expected_audio_path);
  std::filesystem::remove(std::filesystem::temp_directory_path() /
                          "videosynth_audio_efm.efm.meta");
}

TEST(AudioEfmWriterTest, WritesAConformantFrameIndexSidecar) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_sidecar.cvbs";
  const std::filesystem::path expected_audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_sidecar.efm";
  const std::filesystem::path expected_sidecar_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_sidecar.efm.meta";
  std::filesystem::remove(expected_audio_path);
  std::filesystem::remove(expected_sidecar_path);

  const Project project = MakeAudioProject(Standard::kPal, video_path.string());

  AudioEfmWriter writer;
  std::vector<std::string> errors;
  ASSERT_TRUE(writer.BeginWrite(project, 2, MakeTrackTable(), &errors))
      << (errors.empty() ? "" : errors.front());

  constexpr int kFrameCount = 3;
  constexpr std::size_t kSamplesPerFrame = 600;
  for (int frame = 0; frame < kFrameCount; ++frame) {
    ASSERT_TRUE(writer.AppendFrameAudio(
        MakeFrameSamples(kSamplesPerFrame, frame),
        MakeFrameSamples(kSamplesPerFrame, frame + 10), &errors))
        << (errors.empty() ? "" : errors.front());
  }
  ASSERT_TRUE(writer.FinalizeWrite(&errors))
      << (errors.empty() ? "" : errors.front());

  // The extension is a two-file pair sharing the CVBS basename.
  ASSERT_TRUE(std::filesystem::exists(expected_audio_path));
  ASSERT_TRUE(std::filesystem::exists(expected_sidecar_path));
  EXPECT_EQ(ReadUserVersion(expected_sidecar_path), 1);

  const std::vector<EfmFrameRow> rows = ReadEfmFrameRows(expected_sidecar_path);
  ASSERT_EQ(rows.size(), static_cast<std::size_t>(kFrameCount));

  // Frame validity rules 1-4 of efm-extension-format.md: 0-based contiguous
  // frame ids against the implicit default capture, with each offset equal to
  // the sum of the preceding counts.
  std::int64_t running_offset = 0;
  for (std::size_t index = 0; index < rows.size(); ++index) {
    EXPECT_EQ(rows[index].cvbs_file_id, 1);
    EXPECT_EQ(rows[index].frame_id, static_cast<std::int64_t>(index));
    EXPECT_EQ(rows[index].t_value_offset, running_offset);
    EXPECT_GT(rows[index].t_value_count, 0);
    running_offset += rows[index].t_value_count;
  }

  // Rule 6: the index accounts for every byte of the binary file, including
  // the CIRC residue flushed on finalize.
  const std::int64_t file_size = static_cast<std::int64_t>(
      std::filesystem::file_size(expected_audio_path));
  EXPECT_EQ(rows.back().t_value_offset + rows.back().t_value_count, file_size);

  std::filesystem::remove(expected_audio_path);
  std::filesystem::remove(expected_sidecar_path);
}

TEST(AudioEfmWriterTest, RejectsMismatchedChannelLengths) {
  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_mismatch.cvbs";
  const std::filesystem::path expected_audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_mismatch.efm";
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
      "videosynth_audio_efm_bad_table.cvbs";
  const std::filesystem::path expected_audio_path =
      std::filesystem::temp_directory_path() /
      "videosynth_audio_efm_bad_table.efm";
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
      "videosynth_audio_efm_abort.cvbs";
  const std::filesystem::path expected_audio_path =
      std::filesystem::temp_directory_path() / "videosynth_audio_efm_abort.efm";
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
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::temp_directory_path() /
                                       "videosynth_audio_efm_abort.efm.meta"));
  // A second abort is a no-op, and finalizing without a session fails.
  writer.AbortWrite();
  EXPECT_FALSE(writer.FinalizeWrite(&errors));
}

}  // namespace
}  // namespace videosynth
