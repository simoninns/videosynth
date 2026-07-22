/*
 * File:        output_stage.cpp
 * Module:      output_stage
 * Purpose:     Writes generated sample buffers to output and metadata files.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/output_stage.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "videosynth/cvbs_quantization.h"
#include "videosynth/fixed_point.h"
#include "videosynth/model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

OutputStage::OutputStage(ILogger* logger) : logger_(logger) {}

namespace {

enum class OutputEncoding {
  kCvbsU10,
  kCvbsU16,
  kCvbsTpg21,
  kCvbsS16Fsc,
  kRawS16,
};

bool ResolveOutputEncoding(const std::string& preset,
                           OutputEncoding* output_encoding) {
  if (output_encoding == nullptr) {
    return false;
  }

  if (preset == "CVBS_U10_4FSC") {
    *output_encoding = OutputEncoding::kCvbsU10;
    return true;
  }

  if (preset == "CVBS_U16_4FSC") {
    *output_encoding = OutputEncoding::kCvbsU16;
    return true;
  }

  if (preset == "CVBS_TPG21_4FSC") {
    *output_encoding = OutputEncoding::kCvbsTpg21;
    return true;
  }

  if (preset == "CVBS_S16_4FSC") {
    *output_encoding = OutputEncoding::kCvbsS16Fsc;
    return true;
  }

  if (preset == "RAW_S16_28M" || preset == "RAW_S16_40M") {
    *output_encoding = OutputEncoding::kRawS16;
    return true;
  }

  return false;
}

std::vector<SampleFixed> ResampleFrame(const std::vector<SampleFixed>& input,
                                       std::size_t target_count) {
  if (target_count == 0U) {
    return {};
  }
  if (input.empty()) {
    return std::vector<SampleFixed>(target_count, 0);
  }
  if (input.size() == target_count) {
    return input;
  }
  if (target_count == 1U) {
    return std::vector<SampleFixed>(1U, input.front());
  }
  if (input.size() == 1U) {
    return std::vector<SampleFixed>(target_count, input.front());
  }

  std::vector<SampleFixed> output(target_count, 0);
  const double source_last_index = static_cast<double>(input.size() - 1U);
  const double target_last_index = static_cast<double>(target_count - 1U);

  for (std::size_t i = 0; i < target_count; ++i) {
    const double source_position =
        source_last_index * (static_cast<double>(i) / target_last_index);
    const std::size_t left_index = static_cast<std::size_t>(source_position);
    const std::size_t right_index =
        std::min(left_index + 1U, input.size() - 1U);
    const double fraction = source_position - static_cast<double>(left_index);
    const double interpolated =
        (1.0 - fraction) * static_cast<double>(input[left_index]) +
        fraction * static_cast<double>(input[right_index]);
    output[i] = static_cast<SampleFixed>(std::llround(interpolated));
  }

  return output;
}

std::int16_t EncodeCompositeSample(OutputEncoding encoding,
                                   SampleFixed composite_mv_fixed,
                                   const QuantizationProfile& profile) {
  if (encoding == OutputEncoding::kRawS16) {
    const int raw_mv = static_cast<int>(
        std::lround(SampleFixedToMillivolts(composite_mv_fixed)));
    const int clamped_raw_mv = std::max(
        static_cast<int>(std::numeric_limits<std::int16_t>::min()),
        std::min(static_cast<int>(std::numeric_limits<std::int16_t>::max()),
                 raw_mv));
    return static_cast<std::int16_t>(clamped_raw_mv);
  }

  const int mapped = MapCompositeFixedToCode(composite_mv_fixed, profile);
  const int quantized_code = ClampToLegalCodeRange(mapped, profile);
  if (encoding == OutputEncoding::kCvbsTpg21) {
    const int tpg21_encoded = (quantized_code - 508) * 64;
    return static_cast<std::int16_t>(tpg21_encoded);
  }

  if (encoding == OutputEncoding::kCvbsS16Fsc) {
    // S16_4FSC can represent sub-sync excursions (e.g. pilot burst troughs at
    // −600 mV) that lie below the 10-bit legal-code floor. Use the unclamped
    // mapped code and saturate only to prevent int16 overflow.
    const int s16_4fsc_raw = (mapped - profile.blanking_code) * 32;
    const int s16_4fsc_clamped =
        std::clamp(s16_4fsc_raw,
                   static_cast<int>(std::numeric_limits<std::int16_t>::min()),
                   static_cast<int>(std::numeric_limits<std::int16_t>::max()));
    return static_cast<std::int16_t>(s16_4fsc_clamped);
  }

  return static_cast<std::int16_t>(quantized_code);
}

bool IsNonstandardMappedCode(int mapped_code,
                             const QuantizationProfile& profile) {
  return mapped_code < (profile.minimum_legal_code - 1) ||
         mapped_code > (profile.maximum_legal_code + 1);
}

std::int16_t EncodeChromaSample(OutputEncoding encoding,
                                SampleFixed chroma_mv_fixed,
                                const QuantizationProfile& profile) {
  if (encoding == OutputEncoding::kRawS16) {
    const int raw_mv =
        static_cast<int>(std::lround(SampleFixedToMillivolts(chroma_mv_fixed)));
    const int clamped_raw_mv = std::clamp(
        raw_mv, static_cast<int>(std::numeric_limits<std::int16_t>::min()),
        static_cast<int>(std::numeric_limits<std::int16_t>::max()));
    return static_cast<std::int16_t>(clamped_raw_mv);
  }

  const int chroma_code = MapChromaFixedToCode(chroma_mv_fixed, profile);
  const int quantized_code = ClampToLegalCodeRange(chroma_code, profile);

  if (encoding == OutputEncoding::kCvbsTpg21) {
    const int tpg21_encoded = (quantized_code - 508) * 64;
    return static_cast<std::int16_t>(tpg21_encoded);
  }

  if (encoding == OutputEncoding::kCvbsS16Fsc) {
    const int s16_4fsc_raw = (chroma_code - profile.blanking_code) * 32;
    const int s16_4fsc_clamped =
        std::clamp(s16_4fsc_raw,
                   static_cast<int>(std::numeric_limits<std::int16_t>::min()),
                   static_cast<int>(std::numeric_limits<std::int16_t>::max()));
    return static_cast<std::int16_t>(s16_4fsc_clamped);
  }

  return static_cast<std::int16_t>(quantized_code);
}

// Derives the chroma file path from the luma path by replacing the ".y"
// suffix with ".c". Returns empty string if video_path does not end in ".y".
std::string DeriveChromaPath(const std::string& luma_path) {
  constexpr std::string_view kLumaSuffix = ".y";
  constexpr std::string_view kChromaSuffix = ".c";
  if (luma_path.size() < kLumaSuffix.size()) {
    return {};
  }
  if (luma_path.compare(luma_path.size() - kLumaSuffix.size(),
                        kLumaSuffix.size(), kLumaSuffix) != 0) {
    return {};
  }
  return luma_path.substr(0, luma_path.size() - kLumaSuffix.size()) +
         std::string(kChromaSuffix);
}

bool WriteMetadataDatabase(const Project& project, std::size_t frame_count,
                           const QuantizationProfile& quantization,
                           bool has_nonstandard,
                           std::vector<std::string>* errors, ILogger* logger) {
  const std::string& metadata_path = project.output.metadata_path;

  // Write metadata as SQLite database per CVBS specification.
  std::remove(metadata_path.c_str());

  sqlite3* db = nullptr;
  const int open_result = sqlite3_open(metadata_path.c_str(), &db);
  if (open_result != SQLITE_OK) {
    errors->push_back("Failed to create metadata SQLite database: " +
                      metadata_path);
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return false;
  }

  if (logger != nullptr) {
    logger->Trace("Opened metadata database for writing: " + metadata_path);
  }

  char* error_msg = nullptr;
  if (sqlite3_exec(db, "PRAGMA user_version = 10;", nullptr, nullptr,
                   &error_msg) != SQLITE_OK) {
    errors->push_back(std::string("Failed to set PRAGMA user_version: ") +
                      error_msg);
    sqlite3_free(error_msg);
    sqlite3_close(db);
    return false;
  }

  const char* create_table_sql =
      "CREATE TABLE cvbs_file ("
      "    cvbs_file_id                INTEGER PRIMARY KEY,"
      "    preset                      TEXT    NOT NULL"
      "        CHECK (preset IN ('NTSC', 'PAL', 'PAL_M')),"
      "    sample_encoding_preset      TEXT    NOT NULL"
      "        CHECK (sample_encoding_preset IN ('CVBS_U10_4FSC', "
      "'CVBS_U16_4FSC', 'RAW_S16_28M', 'RAW_S16_40M', 'CVBS_TPG21_4FSC', "
      "'CVBS_S16_4FSC')),"
      "    signal_state_preset         TEXT    NOT NULL"
      "        CHECK (signal_state_preset IN ("
      "            'STANDARD_TBC_LOCKED',"
      "            'STANDARD_TBC_UNLOCKED',"
      "            'STANDARD_RAW',"
      "            'NONSTANDARD_TBC_LOCKED',"
      "            'NONSTANDARD_TBC_UNLOCKED',"
      "            'NONSTANDARD_RAW'"
      "        )),"
      "    signal_type                 TEXT    NOT NULL"
      "        CHECK (signal_type IN ('composite', 'yc')),"
      "    decoder                     TEXT    NOT NULL,"
      "    git_branch                  TEXT,"
      "    git_commit                  TEXT,"
      "    number_of_sequential_frames INTEGER"
      "        CHECK (number_of_sequential_frames IS NULL OR "
      "number_of_sequential_frames >= 1),"
      "    black_level                 INTEGER,"
      "    has_nonstandard_values      BOOLEAN,"
      "    capture_notes               TEXT"
      ");";

  if (sqlite3_exec(db, create_table_sql, nullptr, nullptr, &error_msg) !=
      SQLITE_OK) {
    errors->push_back(std::string("Failed to create cvbs_file table: ") +
                      error_msg);
    sqlite3_free(error_msg);
    sqlite3_close(db);
    return false;
  }

  // Per-channel-pair audio metadata (CVBS File Format Specification,
  // audio_channel_pair table): one row per emitted `_audio_<pair>.wav` file.
  const char* create_audio_table_sql =
      "CREATE TABLE audio_channel_pair ("
      "    channel_pair                INTEGER PRIMARY KEY"
      "        CHECK (channel_pair BETWEEN 0 AND 7),"
      "    description                 TEXT"
      ");";

  if (sqlite3_exec(db, create_audio_table_sql, nullptr, nullptr, &error_msg) !=
      SQLITE_OK) {
    errors->push_back(
        std::string("Failed to create audio_channel_pair table: ") + error_msg);
    sqlite3_free(error_msg);
    sqlite3_close(db);
    return false;
  }

  // No EFM table here: the LaserDisc digital audio stream is a separate
  // extension format (CVBS File Format Specification, Producer Extension
  // Metadata) and carries its own `<basename>.efm.meta` sidecar, written by
  // AudioEfmWriter. The core schema has no EFM concept.

  const std::string preset_str =
      StandardToString(project.cvbs_presets.video_standard_preset);
  if (project.cvbs_presets.video_standard_preset == Standard::kUnknown) {
    errors->push_back("Unknown video standard preset");
    sqlite3_close(db);
    return false;
  }

  sqlite3_stmt* insert_stmt = nullptr;
  const char* insert_sql =
      "INSERT INTO cvbs_file ("
      "    preset,"
      "    sample_encoding_preset,"
      "    signal_state_preset,"
      "    signal_type,"
      "    decoder,"
      "    git_branch,"
      "    git_commit,"
      "    number_of_sequential_frames,"
      "    black_level,"
      "    has_nonstandard_values,"
      "    capture_notes"
      ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

  if (sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr) !=
      SQLITE_OK) {
    errors->push_back("Failed to prepare insert statement");
    sqlite3_close(db);
    return false;
  }

  sqlite3_bind_text(insert_stmt, 1, preset_str.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(insert_stmt, 2,
                    project.cvbs_presets.sample_encoding_preset.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(insert_stmt, 3,
                    project.cvbs_presets.signal_state_preset.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(insert_stmt, 4, project.output.signal_type.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_stmt, 5, "videosynth", -1, SQLITE_STATIC);
  sqlite3_bind_null(insert_stmt, 6);
  sqlite3_bind_null(insert_stmt, 7);
  sqlite3_bind_int64(insert_stmt, 8, static_cast<sqlite3_int64>(frame_count));

  const bool has_explicit_black_level_override =
      project.cvbs_presets.video_standard_preset == Standard::kNtsc &&
      project.cvbs_presets.ntsc_black_setup_ire_specified &&
      std::abs(project.cvbs_presets.ntsc_black_setup_ire) < 1e-9;
  if (has_explicit_black_level_override) {
    const SignalLevels levels = GetSignalLevels(project.cvbs_presets);
    const int black_level_code =
        MapCompositeMillivoltsToCode(levels.black_mv, quantization);
    sqlite3_bind_int(insert_stmt, 9, black_level_code);
  } else {
    sqlite3_bind_null(insert_stmt, 9);
  }

  sqlite3_bind_int(insert_stmt, 10, has_nonstandard ? 1 : 0);
  sqlite3_bind_null(insert_stmt, 11);  // capture_notes

  if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
    errors->push_back("Failed to insert metadata row");
    sqlite3_finalize(insert_stmt);
    sqlite3_close(db);
    return false;
  }

  sqlite3_finalize(insert_stmt);

  // One audio_channel_pair row per emitted channel-pair WAV file. Channel-pair
  // numbers are the union of those declared across sections; the description is
  // the first non-empty label recorded for that pair (NULL when none).
  const std::vector<int> channel_pairs = ProjectAudioChannelPairs(project);
  if (!channel_pairs.empty()) {
    sqlite3_stmt* audio_stmt = nullptr;
    const char* audio_insert_sql =
        "INSERT INTO audio_channel_pair (channel_pair, description) "
        "VALUES (?, ?);";
    if (sqlite3_prepare_v2(db, audio_insert_sql, -1, &audio_stmt, nullptr) !=
        SQLITE_OK) {
      errors->push_back("Failed to prepare audio_channel_pair insert");
      sqlite3_close(db);
      return false;
    }
    for (const int pair : channel_pairs) {
      sqlite3_reset(audio_stmt);
      sqlite3_bind_int(audio_stmt, 1, pair);
      const std::string description =
          AudioChannelPairDescription(project, pair);
      if (description.empty()) {
        sqlite3_bind_null(audio_stmt, 2);
      } else {
        sqlite3_bind_text(audio_stmt, 2, description.c_str(), -1,
                          SQLITE_TRANSIENT);
      }
      if (sqlite3_step(audio_stmt) != SQLITE_DONE) {
        errors->push_back("Failed to insert audio_channel_pair row");
        sqlite3_finalize(audio_stmt);
        sqlite3_close(db);
        return false;
      }
    }
    sqlite3_finalize(audio_stmt);
  }

  sqlite3_close(db);
  return true;
}

}  // namespace

bool OutputStage::IsSessionOpen() const { return write_session_open_; }

bool OutputStage::BeginWrite(const Project& project,
                             std::size_t expected_frame_count,
                             std::vector<std::string>* errors) {
  if (errors == nullptr) {
    return false;
  }

  if (IsSessionOpen()) {
    errors->push_back("Output session already open.");
    return false;
  }

  if (logger_ != nullptr) {
    logger_->Debug("Preparing to write output files: video='" +
                   project.output.video_path + "', metadata='" +
                   project.output.metadata_path + "'.");
  }

  const std::string& output_path = project.output.video_path;
  const std::string& metadata_path = project.output.metadata_path;
  if (output_path.empty()) {
    errors->push_back("Output path must not be empty.");
    return false;
  }
  if (metadata_path.empty()) {
    errors->push_back("Metadata path must not be empty.");
    return false;
  }
  if (output_path == metadata_path) {
    errors->push_back("Output and metadata paths must be different files.");
    return false;
  }
  if (!IsSupportedSampleEncodingPreset(
          project.cvbs_presets.sample_encoding_preset)) {
    errors->push_back(
        "Output stage requires a supported sample_encoding_preset.");
    return false;
  }
  if (!IsLockedSignalStatePreset(project.cvbs_presets.signal_state_preset)) {
    errors->push_back("Output stage requires a locked signal_state_preset.");
    return false;
  }

  const std::size_t input_frame_span = static_cast<std::size_t>(
      SamplesPerFrame4fsc(project.cvbs_presets.video_standard_preset));
  const std::size_t output_frame_span = SamplesPerFrameForEncodingPreset(
      project.cvbs_presets.video_standard_preset,
      project.cvbs_presets.sample_encoding_preset);
  if (input_frame_span == 0U || output_frame_span == 0U) {
    errors->push_back(
        "Generated sample count does not align to supported output timing.");
    return false;
  }

  // Create parent directories so callers don't need to pre-create them.
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(output_path).parent_path(), ec);
  if (ec) {
    errors->push_back("Failed to create output directory for: " + output_path +
                      " (" + ec.message() + ")");
    return false;
  }
  std::filesystem::create_directories(
      std::filesystem::path(metadata_path).parent_path(), ec);
  if (ec) {
    errors->push_back("Failed to create output directory for: " +
                      metadata_path + " (" + ec.message() + ")");
    return false;
  }

  const bool is_yc = (project.output.signal_type == "yc");
  if (is_yc) {
    const std::string chroma_path = DeriveChromaPath(output_path);
    if (chroma_path.empty()) {
      errors->push_back("Y/C output requires video_path to end in '.y': " +
                        output_path);
      return false;
    }
    std::ofstream ystream(output_path, std::ios::binary);
    if (!ystream) {
      errors->push_back("Failed to open luma output file: " + output_path);
      return false;
    }
    std::ofstream cstream(chroma_path, std::ios::binary);
    if (!cstream) {
      errors->push_back("Failed to open chroma output file: " + chroma_path);
      return false;
    }
    current_project_ = project;
    video_stream_ = std::move(ystream);
    chroma_stream_ = std::move(cstream);
    if (logger_ != nullptr) {
      logger_->Trace("Opened Y/C output files for writing: luma=" +
                     output_path + ", chroma=" + chroma_path);
    }
  } else {
    std::ofstream stream(output_path, std::ios::binary);
    if (!stream) {
      errors->push_back("Failed to open output video file: " + output_path);
      return false;
    }
    current_project_ = project;
    video_stream_ = std::move(stream);
    if (logger_ != nullptr) {
      logger_->Trace("Opened output video file for writing: " + output_path);
    }
  }

  expected_frame_count_ = expected_frame_count;
  written_samples_ = 0U;
  input_frame_span_ = input_frame_span;
  output_frame_span_ = output_frame_span;
  has_nonstandard_ = false;
  write_session_open_ = true;

  return true;
}

bool OutputStage::AppendSamples(const std::vector<SampleFixed>& y_mv,
                                const std::vector<SampleFixed>& c_mv,
                                std::vector<std::string>* errors) {
  if (errors == nullptr) {
    return false;
  }

  if (!IsSessionOpen()) {
    errors->push_back("Output session is not open.");
    return false;
  }

  if (y_mv.size() != c_mv.size()) {
    errors->push_back(
        "Internal error: Y and C sample vectors must be same size.");
    return false;
  }
  if (input_frame_span_ == 0U || (y_mv.size() % input_frame_span_) != 0U) {
    errors->push_back(
        "Generated sample count does not align to supported input timing.");
    return false;
  }

  QuantizationProfile quantization;
  if (!BuildQuantizationProfile(
          current_project_.cvbs_presets.video_standard_preset, &quantization)) {
    errors->push_back(
        "Output stage received unsupported or unknown video standard.");
    return false;
  }

  OutputEncoding output_encoding = OutputEncoding::kCvbsU10;
  if (!ResolveOutputEncoding(
          current_project_.cvbs_presets.sample_encoding_preset,
          &output_encoding)) {
    errors->push_back("Output stage does not support sample_encoding_preset: " +
                      current_project_.cvbs_presets.sample_encoding_preset);
    return false;
  }

  const bool is_yc = (current_project_.output.signal_type == "yc");

  for (std::size_t frame_start = 0U; frame_start < y_mv.size();
       frame_start += input_frame_span_) {
    if (is_yc) {
      std::vector<SampleFixed> y_frame;
      std::vector<SampleFixed> c_frame;
      y_frame.reserve(input_frame_span_);
      c_frame.reserve(input_frame_span_);
      for (std::size_t i = 0; i < input_frame_span_; ++i) {
        y_frame.push_back(y_mv[frame_start + i]);
        c_frame.push_back(c_mv[frame_start + i]);
      }
      if (output_frame_span_ != input_frame_span_) {
        y_frame = ResampleFrame(y_frame, output_frame_span_);
        c_frame = ResampleFrame(c_frame, output_frame_span_);
      }
      for (std::size_t i = 0; i < output_frame_span_; ++i) {
        if (output_encoding != OutputEncoding::kRawS16) {
          const int mapped = MapCompositeFixedToCode(y_frame[i], quantization);
          if (IsNonstandardMappedCode(mapped, quantization)) {
            has_nonstandard_ = true;
          }
        }
        const std::int16_t y_sample =
            EncodeCompositeSample(output_encoding, y_frame[i], quantization);
        video_stream_.write(reinterpret_cast<const char*>(&y_sample),
                            sizeof(y_sample));
        const std::int16_t c_sample =
            EncodeChromaSample(output_encoding, c_frame[i], quantization);
        chroma_stream_.write(reinterpret_cast<const char*>(&c_sample),
                             sizeof(c_sample));
      }
    } else {
      std::vector<SampleFixed> composite_frame;
      composite_frame.reserve(input_frame_span_);
      for (std::size_t i = 0; i < input_frame_span_; ++i) {
        composite_frame.push_back(y_mv[frame_start + i] +
                                  c_mv[frame_start + i]);
      }
      if (output_frame_span_ != input_frame_span_) {
        composite_frame = ResampleFrame(composite_frame, output_frame_span_);
      }
      for (const SampleFixed composite_mv_fixed : composite_frame) {
        if (output_encoding != OutputEncoding::kRawS16) {
          const int mapped =
              MapCompositeFixedToCode(composite_mv_fixed, quantization);
          if (IsNonstandardMappedCode(mapped, quantization)) {
            has_nonstandard_ = true;
          }
        }
        const std::int16_t encoded_sample = EncodeCompositeSample(
            output_encoding, composite_mv_fixed, quantization);
        video_stream_.write(reinterpret_cast<const char*>(&encoded_sample),
                            sizeof(encoded_sample));
      }
    }
  }

  if (!video_stream_) {
    errors->push_back("Failed while writing output video samples.");
    return false;
  }

  written_samples_ += (y_mv.size() / input_frame_span_) * output_frame_span_;
  return true;
}

bool OutputStage::FinalizeWrite(std::vector<std::string>* errors) {
  if (errors == nullptr) {
    return false;
  }

  if (!IsSessionOpen()) {
    errors->push_back("Output session is not open.");
    return false;
  }

  const std::size_t expected_samples =
      expected_frame_count_ * output_frame_span_;
  if (written_samples_ != expected_samples) {
    errors->push_back(
        "Output session sample count mismatch before finalization.");
    video_stream_.close();
    if (chroma_stream_.is_open()) {
      chroma_stream_.close();
    }
    write_session_open_ = false;
    return false;
  }

  video_stream_.close();
  if (chroma_stream_.is_open()) {
    chroma_stream_.close();
  }

  QuantizationProfile quantization;
  if (!BuildQuantizationProfile(
          current_project_.cvbs_presets.video_standard_preset, &quantization)) {
    errors->push_back(
        "Output stage received unsupported or unknown video standard.");
    write_session_open_ = false;
    return false;
  }

  if (!WriteMetadataDatabase(current_project_, expected_frame_count_,
                             quantization, has_nonstandard_, errors, logger_)) {
    write_session_open_ = false;
    return false;
  }

  if (logger_ != nullptr) {
    logger_->Info("Wrote " + std::to_string(expected_frame_count_) +
                  " frame(s) to output files.");
  }

  write_session_open_ = false;
  return true;
}

void OutputStage::AbortWrite() {
  if (!IsSessionOpen()) {
    return;
  }

  video_stream_.close();
  if (chroma_stream_.is_open()) {
    chroma_stream_.close();
  }

  // Removal failures are ignored: the session is discarded either way and a
  // best-effort cleanup is all a cancelled run requires.
  std::error_code ec;
  std::filesystem::remove(current_project_.output.video_path, ec);
  const std::string chroma_path =
      DeriveChromaPath(current_project_.output.video_path);
  if (current_project_.output.signal_type == "yc" && !chroma_path.empty()) {
    std::filesystem::remove(chroma_path, ec);
  }
  std::filesystem::remove(current_project_.output.metadata_path, ec);

  if (logger_ != nullptr) {
    logger_->Info("Output write session aborted; removed in-progress files.");
  }

  write_session_open_ = false;
  written_samples_ = 0U;
}

bool OutputStage::Write(const Project& project,
                        const std::vector<SampleFixed>& y_mv,
                        const std::vector<SampleFixed>& c_mv,
                        std::vector<std::string>* errors) {
  if (errors == nullptr) {
    return false;
  }

  if (y_mv.size() != c_mv.size()) {
    errors->push_back(
        "Internal error: Y and C sample vectors must be same size.");
    return false;
  }

  const std::size_t frame_span = static_cast<std::size_t>(
      SamplesPerFrame4fsc(project.cvbs_presets.video_standard_preset));
  if (frame_span == 0U || (y_mv.size() % frame_span) != 0U) {
    errors->push_back(
        "Generated sample count does not align to whole-frame 4fsc timing.");
    return false;
  }

  const std::size_t frame_count = y_mv.size() / frame_span;
  if (!BeginWrite(project, frame_count, errors)) {
    return false;
  }
  if (!AppendSamples(y_mv, c_mv, errors)) {
    write_session_open_ = false;
    video_stream_.close();
    if (chroma_stream_.is_open()) {
      chroma_stream_.close();
    }
    return false;
  }
  return FinalizeWrite(errors);
}

}  // namespace videosynth
