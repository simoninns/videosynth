/*
 * File:        audio_efm_writer.cpp
 * Module:      audio_efm_writer
 * Purpose:     Streams one audio channel pair to a LaserDisc digital audio
 *              (EFM) T-value file alongside the generated CVBS output.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/audio_efm_writer.h"

#include <filesystem>
#include <system_error>

#include "videosynth/audio_output_paths.h"
#include "videosynth/audio_sample_conversion.h"

namespace videosynth {

AudioEfmWriter::AudioEfmWriter(ILogger* logger) : logger_(logger) {}

std::string AudioEfmWriter::DeriveAudioPath(const std::string& video_path,
                                            int channel_pair) {
  return DeriveAudioTrackPath(video_path, channel_pair, ".efm");
}

bool AudioEfmWriter::BeginWrite(const Project& project, int channel_pair,
                                const efm::TrackTable& track_table,
                                std::vector<std::string>* errors) {
  if (errors == nullptr) {
    return false;
  }
  if (session_open_) {
    errors->push_back("EFM audio write session already open.");
    return false;
  }

  const std::string audio_path =
      DeriveAudioPath(project.output.video_path, channel_pair);
  if (audio_path.empty()) {
    errors->push_back(
        "EFM audio output path could not be derived from video_path.");
    return false;
  }

  if (!encoder_.Begin(track_table)) {
    errors->push_back("EFM track table is not a valid subcode layout for: " +
                      audio_path);
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(audio_path).parent_path(), ec);
  if (ec) {
    errors->push_back("Failed to create EFM audio output directory for: " +
                      audio_path + " (" + ec.message() + ")");
    encoder_.Reset();
    return false;
  }

  stream_.open(audio_path, std::ios::binary | std::ios::trunc);
  if (!stream_) {
    errors->push_back("Failed to open EFM audio output file: " + audio_path);
    encoder_.Reset();
    return false;
  }

  audio_path_ = audio_path;
  t_value_count_ = 0;
  session_open_ = true;

  if (logger_ != nullptr) {
    logger_->Trace("Opened EFM audio output file for writing: " + audio_path);
  }
  return true;
}

bool AudioEfmWriter::WriteTValues(const std::vector<std::uint8_t>& t_values,
                                  std::vector<std::string>* errors) {
  if (t_values.empty()) {
    return true;
  }
  stream_.write(reinterpret_cast<const char*>(t_values.data()),
                static_cast<std::streamsize>(t_values.size()));
  if (!stream_) {
    errors->push_back("Failed while writing EFM audio data to: " + audio_path_);
    return false;
  }
  t_value_count_ += t_values.size();
  return true;
}

bool AudioEfmWriter::AppendFrameAudio(const std::vector<std::int32_t>& left,
                                      const std::vector<std::int32_t>& right,
                                      std::vector<std::string>* errors) {
  if (errors == nullptr) {
    return false;
  }
  if (!session_open_) {
    errors->push_back("EFM audio write session is not open.");
    return false;
  }
  if (left.size() != right.size()) {
    errors->push_back(
        "EFM audio left/right channel buffers differ in length for: " +
        audio_path_);
    return false;
  }

  // IEC 60908-1999 clause 12: compact-disc samples are 16-bit two's complement.
  std::vector<std::uint8_t> t_values;
  if (!encoder_.PushSamples(ConvertSamples24To16(left),
                            ConvertSamples24To16(right), &t_values)) {
    errors->push_back("EFM audio encoder rejected samples for: " + audio_path_);
    return false;
  }
  return WriteTValues(t_values, errors);
}

bool AudioEfmWriter::FinalizeWrite(std::vector<std::string>* errors) {
  if (errors == nullptr) {
    return false;
  }
  if (!session_open_) {
    errors->push_back("EFM audio write session is not open.");
    return false;
  }

  std::vector<std::uint8_t> t_values;
  if (!encoder_.Flush(&t_values)) {
    errors->push_back("EFM audio encoder failed to flush for: " + audio_path_);
    stream_.close();
    session_open_ = false;
    return false;
  }
  if (!WriteTValues(t_values, errors)) {
    stream_.close();
    session_open_ = false;
    return false;
  }

  stream_.close();
  if (!stream_) {
    errors->push_back("Failed to close EFM audio output file: " + audio_path_);
    session_open_ = false;
    return false;
  }
  session_open_ = false;
  encoder_.Reset();

  if (logger_ != nullptr) {
    logger_->Info("Wrote EFM audio track: " + audio_path_ + " (" +
                  std::to_string(t_value_count_) + " T values)");
  }
  return true;
}

void AudioEfmWriter::AbortWrite() {
  if (!session_open_) {
    return;
  }

  stream_.close();
  // Removal failures are ignored: cleanup is best-effort on abort.
  std::error_code ec;
  std::filesystem::remove(audio_path_, ec);

  if (logger_ != nullptr) {
    logger_->Info("EFM audio write session aborted; removed " + audio_path_);
  }

  session_open_ = false;
  t_value_count_ = 0;
  encoder_.Reset();
}

}  // namespace videosynth
