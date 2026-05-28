/*
 * File:        output_stage.cpp
 * Module:      output_stage
 * Purpose:     Writes generated sample buffers to output and metadata files.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/output_stage.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>

#include <sqlite3.h>

#include "videosynth/model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

OutputStage::OutputStage(ILogger* logger) : logger_(logger) {}

namespace {

struct QuantizationProfile {
  double millivolts_per_code = 1.0;
  int blanking_code = 0;
  int minimum_legal_code = 0;
  int maximum_legal_code = 1023;
};

bool BuildQuantizationProfile(Standard standard, QuantizationProfile* profile) {
  if (profile == nullptr) {
    return false;
  }

  if (standard == Standard::kPal) {
    *profile = QuantizationProfile{
        .millivolts_per_code = 1.1905,
        .blanking_code = 256,
        .minimum_legal_code = 4,
        .maximum_legal_code = 1019,
    };
    return true;
  }

  if (standard == Standard::kNtsc) {
    *profile = QuantizationProfile{
        .millivolts_per_code = 1.2755,
        .blanking_code = 240,
        .minimum_legal_code = 16,
        .maximum_legal_code = 1019,
    };
    return true;
  }

  return false;
}

int QuantizeCompositeMillivolts(double composite_mv, const QuantizationProfile& profile) {
  const int mapped =
      static_cast<int>(std::lround(composite_mv / profile.millivolts_per_code)) +
      profile.blanking_code;
  if (mapped < profile.minimum_legal_code) {
    return profile.minimum_legal_code;
  }
  if (mapped > profile.maximum_legal_code) {
    return profile.maximum_legal_code;
  }
  return mapped;
}

bool EncodeCompositeSample(const std::string& preset,
                           int quantized_code,
                           std::int16_t* encoded_sample) {
  if (encoded_sample == nullptr) {
    return false;
  }

  if (preset == "CVBS_U10_4FSC") {
    *encoded_sample = static_cast<std::int16_t>(quantized_code);
    return true;
  }

  if (preset == "CVBS_TPG21_4FSC") {
    const int tpg21_encoded = (quantized_code - 508) * 64;
    *encoded_sample = static_cast<std::int16_t>(tpg21_encoded);
    return true;
  }

  return false;
}

}  // namespace

bool OutputStage::Write(const Project& project,
                        const std::vector<double>& y_mv,
                        const std::vector<double>& c_mv,
                        std::vector<std::string>* errors) {
  if (errors == nullptr) {
    return false;
  }

  if (logger_ != nullptr) {
    logger_->Debug("Preparing to write output files: video='" + project.output.video_path +
                   "', metadata='" + project.output.metadata_path + "'.");
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

  if (!Is4fscSampleEncodingPreset(project.cvbs_presets.sample_encoding_preset)) {
    errors->push_back("Output stage requires a 4fsc sample_encoding_preset.");
    return false;
  }

  if (!IsLockedSignalStatePreset(project.cvbs_presets.signal_state_preset)) {
    errors->push_back("Output stage requires a locked signal_state_preset for 4fsc output.");
    return false;
  }

  QuantizationProfile quantization;
  if (!BuildQuantizationProfile(project.cvbs_presets.video_standard_preset, &quantization)) {
    errors->push_back("Output stage received unsupported or unknown video standard.");
    return false;
  }

  if (y_mv.size() != c_mv.size()) {
    errors->push_back("Internal error: Y and C sample vectors must be same size.");
    return false;
  }

  const TimingConstants timing = GetTimingConstants(project.cvbs_presets.video_standard_preset);
    const std::size_t frame_span =
      static_cast<std::size_t>(SamplesPerFrame4fsc(project.cvbs_presets.video_standard_preset));
  if (frame_span == 0U || (y_mv.size() % frame_span) != 0U) {
    errors->push_back("Generated sample count does not align to whole-frame 4fsc timing.");
    return false;
  }

  std::ofstream video_stream(output_path, std::ios::binary);
  if (!video_stream) {
    errors->push_back("Failed to open output video file: " + output_path);
    return false;
  }

  if (logger_ != nullptr) {
    logger_->Trace("Opened output video file for writing: " + output_path);
  }

  std::size_t clipped_low_count = 0;
  std::size_t clipped_high_count = 0;
  for (std::size_t i = 0; i < y_mv.size(); ++i) {
    const double composite_mv = y_mv[i] + c_mv[i];
    const int mapped =
        static_cast<int>(std::lround(composite_mv / quantization.millivolts_per_code)) +
        quantization.blanking_code;
    if (mapped < quantization.minimum_legal_code) {
      ++clipped_low_count;
    }
    if (mapped > quantization.maximum_legal_code) {
      ++clipped_high_count;
    }

    const int quantized_code = QuantizeCompositeMillivolts(composite_mv, quantization);
    std::int16_t encoded_sample = 0;
    if (!EncodeCompositeSample(project.cvbs_presets.sample_encoding_preset,
                   quantized_code,
                   &encoded_sample)) {
      errors->push_back("Output stage does not support sample_encoding_preset: " +
              project.cvbs_presets.sample_encoding_preset);
      return false;
    }
    video_stream.write(reinterpret_cast<const char*>(&encoded_sample),
               sizeof(encoded_sample));
  }

  // Write metadata as SQLite database per CVBS specification
  // Remove existing metadata file if present
  std::remove(metadata_path.c_str());

  sqlite3* db = nullptr;
  const int open_result = sqlite3_open(metadata_path.c_str(), &db);
  if (open_result != SQLITE_OK) {
    errors->push_back("Failed to create metadata SQLite database: " + metadata_path);
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return false;
  }

  if (logger_ != nullptr) {
    logger_->Trace("Opened metadata database for writing: " + metadata_path);
  }

  // Set pragma for CVBS spec compliance (user_version = 7)
  char* error_msg = nullptr;
  if (sqlite3_exec(db, "PRAGMA user_version = 7;", nullptr, nullptr, &error_msg) != SQLITE_OK) {
    errors->push_back(std::string("Failed to set PRAGMA user_version: ") + error_msg);
    sqlite3_free(error_msg);
    sqlite3_close(db);
    return false;
  }

  // Create cvbs_file table per CVBS specification
  const char* create_table_sql =
      "CREATE TABLE cvbs_file ("
      "    cvbs_file_id                INTEGER PRIMARY KEY,"
      "    preset                      TEXT    NOT NULL"
      "        CHECK (preset IN ('NTSC', 'PAL', 'PAL_M')),"
      "    sample_encoding_preset      TEXT    NOT NULL"
      "        CHECK (sample_encoding_preset IN ('CVBS_U10_4FSC', 'CVBS_U16_4FSC', 'RAW_S16_28M', 'RAW_S16_40M', 'CVBS_TPG21_4FSC')),"
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
      "        CHECK (number_of_sequential_frames IS NULL OR number_of_sequential_frames >= 1),"
      "    black_level                 INTEGER,"
      "    has_nonstandard_values      BOOLEAN,"
      "    capture_notes               TEXT"
      ");";

  if (sqlite3_exec(db, create_table_sql, nullptr, nullptr, &error_msg) != SQLITE_OK) {
    errors->push_back(std::string("Failed to create cvbs_file table: ") + error_msg);
    sqlite3_free(error_msg);
    sqlite3_close(db);
    return false;
  }

  const std::size_t frame_count = y_mv.size() / frame_span;

  // Convert video standard to CVBS spec string
  std::string preset_str;
  if (project.cvbs_presets.video_standard_preset == Standard::kPal) {
    preset_str = "PAL";
  } else if (project.cvbs_presets.video_standard_preset == Standard::kNtsc) {
    preset_str = "NTSC";
  } else {
    errors->push_back("Unknown video standard preset");
    sqlite3_close(db);
    return false;
  }

  // Check for nonstandard values
  bool has_nonstandard = false;
  for (std::size_t i = 0; i < y_mv.size(); ++i) {
    const double composite_mv = y_mv[i] + c_mv[i];
    const int mapped =
        static_cast<int>(std::lround(composite_mv / quantization.millivolts_per_code)) +
        quantization.blanking_code;
    if (mapped < quantization.minimum_legal_code || mapped > quantization.maximum_legal_code) {
      has_nonstandard = true;
      break;
    }
  }

  // Insert metadata row into cvbs_file table
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

  if (sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr) != SQLITE_OK) {
    errors->push_back("Failed to prepare insert statement");
    sqlite3_close(db);
    return false;
  }

  // Bind values
  sqlite3_bind_text(insert_stmt, 1, preset_str.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(insert_stmt, 2, project.cvbs_presets.sample_encoding_preset.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(insert_stmt, 3, project.cvbs_presets.signal_state_preset.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(insert_stmt, 4, "composite", -1, SQLITE_STATIC);
  sqlite3_bind_text(insert_stmt, 5, "videosynth", -1, SQLITE_STATIC);
  sqlite3_bind_null(insert_stmt, 6);  // git_branch (NULL)
  sqlite3_bind_null(insert_stmt, 7);  // git_commit (NULL)
  sqlite3_bind_int64(insert_stmt, 8, static_cast<sqlite3_int64>(frame_count));
  sqlite3_bind_int(insert_stmt, 9, quantization.blanking_code);
  sqlite3_bind_int(insert_stmt, 10, has_nonstandard ? 1 : 0);
  sqlite3_bind_null(insert_stmt, 11);  // capture_notes (NULL)

  if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
    errors->push_back("Failed to insert metadata row");
    sqlite3_finalize(insert_stmt);
    sqlite3_close(db);
    return false;
  }

  sqlite3_finalize(insert_stmt);
  sqlite3_close(db);

  if (logger_ != nullptr) {
    logger_->Info("Wrote " + std::to_string(frame_count) + " frame(s) to output files.");
  }

  return true;
}

}  // namespace videosynth
