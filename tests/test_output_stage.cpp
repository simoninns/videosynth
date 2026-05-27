/*
 * File:        test_output_stage.cpp
 * Module:      output_stage_tests
 * Purpose:     Validates composite quantization, metadata, and output constraints.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <gtest/gtest.h>

#include "videosynth/output_stage.h"
#include "videosynth/timing_constants.h"

namespace videosynth {
namespace {

Project MakeProject(Standard standard) {
  Project project;
  project.cvbs_presets.video_standard_preset = standard;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  return project;
}

std::vector<std::int16_t> ReadSamples(const std::filesystem::path& path, std::size_t count) {
  std::ifstream stream(path, std::ios::binary);
  std::vector<std::int16_t> samples(count, 0);
  stream.read(reinterpret_cast<char*>(samples.data()),
              static_cast<std::streamsize>(count * sizeof(std::int16_t)));
  return samples;
}

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream stream(path);
  return std::string((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());
}

struct CvbsMetadata {
  std::string preset;
  std::string sample_encoding_preset;
  std::string signal_state_preset;
  std::string signal_type;
  std::string decoder;
  int64_t number_of_sequential_frames = 0;
  int32_t black_level = 0;
  bool has_nonstandard_values = false;
};

bool ReadCvbsMetadata(const std::filesystem::path& path, CvbsMetadata* metadata) {
  if (metadata == nullptr) {
    return false;
  }
  
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    sqlite3_close(db);
    return false;
  }

  const char* query_sql = "SELECT preset, sample_encoding_preset, signal_state_preset, signal_type, decoder, number_of_sequential_frames, black_level, has_nonstandard_values FROM cvbs_file LIMIT 1;";
  sqlite3_stmt* stmt = nullptr;

  if (sqlite3_prepare_v2(db, query_sql, -1, &stmt, nullptr) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return false;
  }

  bool result = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    metadata->preset = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    metadata->sample_encoding_preset = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
    metadata->signal_state_preset = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    metadata->signal_type = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
    metadata->decoder = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
    metadata->number_of_sequential_frames = sqlite3_column_int64(stmt, 5);
    metadata->black_level = sqlite3_column_int(stmt, 6);
    metadata->has_nonstandard_values = sqlite3_column_int(stmt, 7) != 0;
    result = true;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return result;
}

TEST(OutputStageTest, WritesCompositeSamplesUsingPalQuantizationProfile) {
  OutputStage output;
  Project project = MakeProject(Standard::kPal);
  const std::size_t frame_span = static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));

  std::vector<double> y(frame_span, 0.0);
  std::vector<double> c(frame_span, 0.0);
  y[0] = 0.0;
  y[1] = 700.0;
  y[2] = -300.0;

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() / "videosynth_output_stage_pal.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() / "videosynth_output_stage_pal.meta";
  project.output.video_path = video_path.string();
  project.output.metadata_path = metadata_path.string();
  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);

  std::vector<std::string> errors;
  ASSERT_TRUE(output.Write(project, y, c, &errors));

  const std::vector<std::int16_t> samples = ReadSamples(video_path, 3);
  ASSERT_EQ(samples.size(), 3U);
  EXPECT_EQ(samples[0], 256);
  EXPECT_EQ(samples[1], 844);
  EXPECT_EQ(samples[2], 4);

  CvbsMetadata metadata;
  ASSERT_TRUE(ReadCvbsMetadata(metadata_path, &metadata));
  EXPECT_EQ(metadata.preset, "PAL");
  EXPECT_EQ(metadata.signal_type, "composite");
  EXPECT_EQ(metadata.sample_encoding_preset, "CVBS_U10_4FSC");
  EXPECT_EQ(metadata.signal_state_preset, "STANDARD_TBC_LOCKED");
  EXPECT_EQ(metadata.decoder, "videosynth");
  EXPECT_EQ(metadata.number_of_sequential_frames, 1);
  EXPECT_EQ(metadata.black_level, 256);
  EXPECT_FALSE(metadata.has_nonstandard_values);

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

TEST(OutputStageTest, WritesCompositeSamplesUsingTpg21EncodingPreset) {
  OutputStage output;
  Project project = MakeProject(Standard::kPal);
  project.cvbs_presets.sample_encoding_preset = "CVBS_TPG21_4FSC";
  const std::size_t frame_span = static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));

  std::vector<double> y(frame_span, 0.0);
  std::vector<double> c(frame_span, 0.0);
  y[0] = 0.0;
  y[1] = 700.0;
  y[2] = -300.0;

  const std::filesystem::path video_path =
    std::filesystem::temp_directory_path() / "videosynth_output_stage_tpg21.composite";
  const std::filesystem::path metadata_path =
    std::filesystem::temp_directory_path() / "videosynth_output_stage_tpg21.meta";
  project.output.video_path = video_path.string();
  project.output.metadata_path = metadata_path.string();
  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);

  std::vector<std::string> errors;
  ASSERT_TRUE(output.Write(project, y, c, &errors));

  const std::vector<std::int16_t> samples = ReadSamples(video_path, 3);
  ASSERT_EQ(samples.size(), 3U);
  EXPECT_EQ(samples[0], -16128);
  EXPECT_EQ(samples[1], 21504);
  EXPECT_EQ(samples[2], -32256);

  CvbsMetadata metadata;
  ASSERT_TRUE(ReadCvbsMetadata(metadata_path, &metadata));
  EXPECT_EQ(metadata.sample_encoding_preset, "CVBS_TPG21_4FSC");
  EXPECT_EQ(metadata.preset, "PAL");

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

TEST(OutputStageTest, SumsYAndCBeforeQuantizationInNtscProfile) {
  OutputStage output;
  Project project = MakeProject(Standard::kNtsc);
  const std::size_t frame_span = static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kNtsc));

  std::vector<double> y(frame_span, 0.0);
  std::vector<double> c(frame_span, 0.0);
  c[0] = 127.55;

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() / "videosynth_output_stage_ntsc.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() / "videosynth_output_stage_ntsc.meta";
  project.output.video_path = video_path.string();
  project.output.metadata_path = metadata_path.string();
  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);

  std::vector<std::string> errors;
  ASSERT_TRUE(output.Write(project, y, c, &errors));

  const std::vector<std::int16_t> samples = ReadSamples(video_path, 2);
  ASSERT_EQ(samples.size(), 2U);
  EXPECT_EQ(samples[0], 340);
  EXPECT_EQ(samples[1], 240);

  CvbsMetadata metadata;
  ASSERT_TRUE(ReadCvbsMetadata(metadata_path, &metadata));
  EXPECT_EQ(metadata.preset, "NTSC");
  EXPECT_EQ(metadata.black_level, 240);

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

TEST(OutputStageTest, ClampsOutOfRangeValuesToLegalCodeSpace) {
  OutputStage output;
  Project project = MakeProject(Standard::kPal);
  const std::size_t frame_span = static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));

  std::vector<double> y(frame_span, 0.0);
  std::vector<double> c(frame_span, 0.0);
  y[0] = -1000.0;
  y[1] = 2000.0;

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() / "videosynth_output_stage_clamp.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() / "videosynth_output_stage_clamp.meta";
  project.output.video_path = video_path.string();
  project.output.metadata_path = metadata_path.string();
  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);

  std::vector<std::string> errors;
  ASSERT_TRUE(output.Write(project, y, c, &errors));

  const std::vector<std::int16_t> samples = ReadSamples(video_path, 2);
  ASSERT_EQ(samples.size(), 2U);
  EXPECT_EQ(samples[0], 4);
  EXPECT_EQ(samples[1], 1019);

  CvbsMetadata metadata;
  ASSERT_TRUE(ReadCvbsMetadata(metadata_path, &metadata));
  EXPECT_TRUE(metadata.has_nonstandard_values);

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

TEST(OutputStageTest, RejectsInvalidOutputConstraints) {
  OutputStage output;
  Project project = MakeProject(Standard::kPal);

  std::vector<double> y(16, 0.0);
  std::vector<double> c(16, 0.0);
  std::vector<std::string> errors;

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() / "videosynth_output_stage_invalid.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() / "videosynth_output_stage_invalid.meta";
  project.output.video_path = video_path.string();
  project.output.metadata_path = metadata_path.string();

  EXPECT_FALSE(output.Write(project, y, c, &errors));
  EXPECT_FALSE(errors.empty());

  errors.clear();
  project.cvbs_presets.sample_encoding_preset = "RAW_S16_40M";
  const std::size_t frame_span = static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));
  y.assign(frame_span, 0.0);
  c.assign(frame_span, 0.0);
  EXPECT_FALSE(output.Write(project, y, c, &errors));
  EXPECT_FALSE(errors.empty());
}

}  // namespace
}  // namespace videosynth
