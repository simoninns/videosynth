/*
 * File:        test_output_stage.cpp
 * Module:      output_stage_tests
 * Purpose:     Validates composite quantization, metadata, and output
 * constraints.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "videosynth/fixed_point.h"
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

std::vector<std::int16_t> ReadSamples(const std::filesystem::path& path,
                                      std::size_t count) {
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

int QuantizePalReferenceCode(double composite_mv) {
  constexpr double kMillivoltsPerCode = 1.1905;
  constexpr int kBlankingCode = 256;
  constexpr int kMinCode = 4;
  constexpr int kMaxCode = 1019;
  const int mapped =
      static_cast<int>(std::lround(composite_mv / kMillivoltsPerCode)) +
      kBlankingCode;
  return std::max(kMinCode, std::min(kMaxCode, mapped));
}

int QuantizeNtscReferenceCode(double composite_mv) {
  constexpr double kMillivoltsPerCode = 1.2755;
  constexpr int kBlankingCode = 240;
  constexpr int kMinCode = 16;
  constexpr int kMaxCode = 1019;
  const int mapped =
      static_cast<int>(std::lround(composite_mv / kMillivoltsPerCode)) +
      kBlankingCode;
  return std::max(kMinCode, std::min(kMaxCode, mapped));
}

struct CvbsMetadata {
  std::string preset;
  std::string sample_encoding_preset;
  std::string signal_state_preset;
  std::string signal_type;
  std::string decoder;
  int64_t number_of_sequential_frames = 0;
  int32_t black_level = 0;
  bool has_black_level = false;
  bool has_nonstandard_values = false;
};

bool ReadCvbsMetadata(const std::filesystem::path& path,
                      CvbsMetadata* metadata) {
  if (metadata == nullptr) {
    return false;
  }

  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    sqlite3_close(db);
    return false;
  }

  const char* query_sql =
      "SELECT preset, sample_encoding_preset, signal_state_preset, "
      "signal_type, decoder, number_of_sequential_frames, black_level, "
      "has_nonstandard_values FROM cvbs_file LIMIT 1;";
  sqlite3_stmt* stmt = nullptr;

  if (sqlite3_prepare_v2(db, query_sql, -1, &stmt, nullptr) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return false;
  }

  bool result = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    metadata->preset = std::string(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    metadata->sample_encoding_preset = std::string(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
    metadata->signal_state_preset = std::string(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    metadata->signal_type = std::string(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
    metadata->decoder = std::string(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
    metadata->number_of_sequential_frames = sqlite3_column_int64(stmt, 5);
    metadata->has_black_level = sqlite3_column_type(stmt, 6) != SQLITE_NULL;
    if (metadata->has_black_level) {
      metadata->black_level = sqlite3_column_int(stmt, 6);
    }
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
  const std::size_t frame_span =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));

  std::vector<SampleFixed> y(frame_span, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c(frame_span, MillivoltsToSampleFixed(0.0));
  y[0] = MillivoltsToSampleFixed(0.0);
  y[1] = MillivoltsToSampleFixed(700.0);
  y[2] = MillivoltsToSampleFixed(-300.0);

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_pal.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_pal.meta";
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
  EXPECT_FALSE(metadata.has_black_level);
  EXPECT_FALSE(metadata.has_nonstandard_values);

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

TEST(OutputStageTest, WritesCompositeSamplesUsingTpg21EncodingPreset) {
  OutputStage output;
  Project project = MakeProject(Standard::kPal);
  project.cvbs_presets.sample_encoding_preset = "CVBS_TPG21_4FSC";
  const std::size_t frame_span =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));

  std::vector<SampleFixed> y(frame_span, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c(frame_span, MillivoltsToSampleFixed(0.0));
  y[0] = MillivoltsToSampleFixed(0.0);
  y[1] = MillivoltsToSampleFixed(700.0);
  y[2] = MillivoltsToSampleFixed(-300.0);

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_tpg21.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_tpg21.meta";
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

TEST(OutputStageTest, WritesCompositeSamplesUsingUint16EncodingPreset) {
  OutputStage output;
  Project project = MakeProject(Standard::kPal);
  project.cvbs_presets.sample_encoding_preset = "CVBS_U16_4FSC";
  const std::size_t frame_span =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));

  std::vector<SampleFixed> y(frame_span, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c(frame_span, MillivoltsToSampleFixed(0.0));
  y[0] = MillivoltsToSampleFixed(0.0);
  y[1] = MillivoltsToSampleFixed(700.0);
  y[2] = MillivoltsToSampleFixed(-300.0);

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_u16.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_u16.meta";
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
  EXPECT_EQ(std::filesystem::file_size(video_path),
            frame_span * sizeof(std::int16_t));

  CvbsMetadata metadata;
  ASSERT_TRUE(ReadCvbsMetadata(metadata_path, &metadata));
  EXPECT_EQ(metadata.sample_encoding_preset, "CVBS_U16_4FSC");

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

TEST(OutputStageTest, ResamplesRawSamplesUsingTwentyEightMegasamplePreset) {
  OutputStage output;
  Project project = MakeProject(Standard::kPal);
  project.cvbs_presets.sample_encoding_preset = "RAW_S16_28M";
  const std::size_t input_frame_span =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));
  const std::size_t output_frame_span =
      SamplesPerFrameForEncodingPreset(Standard::kPal, "RAW_S16_28M");

  std::vector<SampleFixed> y(input_frame_span, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c(input_frame_span, MillivoltsToSampleFixed(0.0));
  for (std::size_t i = 0; i < input_frame_span; ++i) {
    const double ramp = 1000.0 * static_cast<double>(i) /
                        static_cast<double>(input_frame_span - 1U);
    y[i] = MillivoltsToSampleFixed(ramp);
  }

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_raw_28m.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_raw_28m.meta";
  project.output.video_path = video_path.string();
  project.output.metadata_path = metadata_path.string();
  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);

  std::vector<std::string> errors;
  ASSERT_TRUE(output.Write(project, y, c, &errors));

  EXPECT_EQ(std::filesystem::file_size(video_path),
            output_frame_span * sizeof(std::int16_t));

  CvbsMetadata metadata;
  ASSERT_TRUE(ReadCvbsMetadata(metadata_path, &metadata));
  EXPECT_EQ(metadata.sample_encoding_preset, "RAW_S16_28M");

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

TEST(OutputStageTest, SumsYAndCBeforeQuantizationInNtscProfile) {
  OutputStage output;
  Project project = MakeProject(Standard::kNtsc);
  const std::size_t frame_span =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kNtsc));

  std::vector<SampleFixed> y(frame_span, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c(frame_span, MillivoltsToSampleFixed(0.0));
  c[0] = MillivoltsToSampleFixed(127.55);

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_ntsc.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_ntsc.meta";
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
  EXPECT_FALSE(metadata.has_black_level);

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

TEST(OutputStageTest, WritesExplicitBlackLevelOverrideForNtscZeroIreSetup) {
  OutputStage output;
  Project project = MakeProject(Standard::kNtsc);
  project.cvbs_presets.ntsc_black_setup_ire = 0.0;
  project.cvbs_presets.ntsc_black_setup_ire_specified = true;
  const std::size_t frame_span =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kNtsc));

  std::vector<SampleFixed> y(frame_span, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c(frame_span, MillivoltsToSampleFixed(0.0));

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_ntsc_zero_black.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_ntsc_zero_black.meta";
  project.output.video_path = video_path.string();
  project.output.metadata_path = metadata_path.string();
  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);

  std::vector<std::string> errors;
  ASSERT_TRUE(output.Write(project, y, c, &errors));

  CvbsMetadata metadata;
  ASSERT_TRUE(ReadCvbsMetadata(metadata_path, &metadata));
  EXPECT_TRUE(metadata.has_black_level);
  EXPECT_EQ(metadata.black_level, 240);

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

TEST(OutputStageTest, ClampsOutOfRangeValuesToLegalCodeSpace) {
  OutputStage output;
  Project project = MakeProject(Standard::kPal);
  const std::size_t frame_span =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));

  std::vector<SampleFixed> y(frame_span, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c(frame_span, MillivoltsToSampleFixed(0.0));
  y[0] = MillivoltsToSampleFixed(-1000.0);
  y[1] = MillivoltsToSampleFixed(2000.0);

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_clamp.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_clamp.meta";
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

  std::vector<SampleFixed> y(16, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c(16, MillivoltsToSampleFixed(0.0));
  std::vector<std::string> errors;

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_invalid.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_invalid.meta";
  project.output.video_path = video_path.string();
  project.output.metadata_path = metadata_path.string();

  EXPECT_FALSE(output.Write(project, y, c, &errors));
  EXPECT_FALSE(errors.empty());

  errors.clear();
  project.cvbs_presets.sample_encoding_preset = "NOT_A_SUPPORTED_PRESET";
  const std::size_t frame_span =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));
  y.assign(frame_span, MillivoltsToSampleFixed(0.0));
  c.assign(frame_span, MillivoltsToSampleFixed(0.0));
  EXPECT_FALSE(output.Write(project, y, c, &errors));
  EXPECT_FALSE(errors.empty());
}

TEST(OutputStageTest, KeepsFixedPointQuantizationEquivalentToReferenceProfile) {
  OutputStage output;
  Project project = MakeProject(Standard::kPal);
  const std::size_t frame_span =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));

  std::vector<SampleFixed> y(frame_span, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c(frame_span, MillivoltsToSampleFixed(0.0));
  constexpr std::size_t kWindowSamples = 4096;
  ASSERT_LE(kWindowSamples, frame_span);

  for (std::size_t i = 0; i < kWindowSamples; ++i) {
    const double ramp = -900.0 + (2200.0 * static_cast<double>(i) /
                                  static_cast<double>(kWindowSamples - 1));
    y[i] = MillivoltsToSampleFixed(ramp);
    c[i] =
        MillivoltsToSampleFixed(75.0 * std::sin(static_cast<double>(i) * 0.17));
  }

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_fixed_equiv.composite";
  const std::filesystem::path metadata_path =
      std::filesystem::temp_directory_path() /
      "videosynth_output_stage_fixed_equiv.meta";
  project.output.video_path = video_path.string();
  project.output.metadata_path = metadata_path.string();
  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);

  std::vector<std::string> errors;
  ASSERT_TRUE(output.Write(project, y, c, &errors));

  const std::vector<std::int16_t> samples =
      ReadSamples(video_path, kWindowSamples);
  ASSERT_EQ(samples.size(), kWindowSamples);

  int max_abs_delta = 0;
  double sum_squared = 0.0;
  for (std::size_t i = 0; i < kWindowSamples; ++i) {
    const int expected =
        QuantizePalReferenceCode(SampleFixedToMillivolts(y[i] + c[i]));
    const int observed = static_cast<int>(samples[i]);
    const int delta = observed - expected;
    max_abs_delta = std::max(max_abs_delta, std::abs(delta));
    sum_squared += static_cast<double>(delta * delta);
  }

  const double rms_error =
      std::sqrt(sum_squared / static_cast<double>(kWindowSamples));
  EXPECT_LE(max_abs_delta, 1);
  EXPECT_LT(rms_error, 0.5);

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

}  // namespace
}  // namespace videosynth
