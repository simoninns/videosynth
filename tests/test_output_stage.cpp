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

TEST(OutputStageTest, WritesCompositeSamplesUsingPalQuantizationProfile) {
  OutputStage output;
  Project project = MakeProject(Standard::kPal);
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const std::size_t frame_span =
      static_cast<std::size_t>(pal.lines_per_frame * pal.samples_per_line_4fsc);

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

  const std::string metadata = ReadTextFile(metadata_path);
  EXPECT_NE(metadata.find("signal_type=composite"), std::string::npos);
  EXPECT_NE(metadata.find("video_standard_preset=PAL"), std::string::npos);
  EXPECT_NE(metadata.find("sample_encoding_preset=CVBS_U10_4FSC"), std::string::npos);
  EXPECT_NE(metadata.find("sample_rate_mode=4fsc"), std::string::npos);
  EXPECT_NE(metadata.find("subcarrier_lock=true"), std::string::npos);
  EXPECT_NE(metadata.find("composite_only=true"), std::string::npos);

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

TEST(OutputStageTest, WritesCompositeSamplesUsingTpg21EncodingPreset) {
  OutputStage output;
  Project project = MakeProject(Standard::kPal);
  project.cvbs_presets.sample_encoding_preset = "CVBS_TPG21_4FSC";
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const std::size_t frame_span =
    static_cast<std::size_t>(pal.lines_per_frame * pal.samples_per_line_4fsc);

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

  const std::string metadata = ReadTextFile(metadata_path);
  EXPECT_NE(metadata.find("sample_encoding_preset=CVBS_TPG21_4FSC"), std::string::npos);
  EXPECT_NE(metadata.find("sample_rate_mode=4fsc"), std::string::npos);

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

TEST(OutputStageTest, SumsYAndCBeforeQuantizationInNtscProfile) {
  OutputStage output;
  Project project = MakeProject(Standard::kNtsc);
  const TimingConstants ntsc = GetTimingConstants(Standard::kNtsc);
  const std::size_t frame_span =
      static_cast<std::size_t>(ntsc.lines_per_frame * ntsc.samples_per_line_4fsc);

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

  std::filesystem::remove(video_path);
  std::filesystem::remove(metadata_path);
}

TEST(OutputStageTest, ClampsOutOfRangeValuesToLegalCodeSpace) {
  OutputStage output;
  Project project = MakeProject(Standard::kPal);
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const std::size_t frame_span =
      static_cast<std::size_t>(pal.lines_per_frame * pal.samples_per_line_4fsc);

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

  const std::string metadata = ReadTextFile(metadata_path);
  EXPECT_NE(metadata.find("clipped_low_samples=1"), std::string::npos);
  EXPECT_NE(metadata.find("clipped_high_samples=1"), std::string::npos);

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
  const TimingConstants pal = GetTimingConstants(Standard::kPal);
  const std::size_t frame_span =
      static_cast<std::size_t>(pal.lines_per_frame * pal.samples_per_line_4fsc);
  y.assign(frame_span, 0.0);
  c.assign(frame_span, 0.0);
  EXPECT_FALSE(output.Write(project, y, c, &errors));
  EXPECT_FALSE(errors.empty());
}

}  // namespace
}  // namespace videosynth
