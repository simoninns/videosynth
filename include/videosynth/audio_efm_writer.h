/*
 * File:        audio_efm_writer.h
 * Module:      audio_efm_writer
 * Purpose:     Streams one audio channel pair to a LaserDisc digital audio
 *              (EFM) T-value file alongside the generated CVBS output.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "videosynth/efm/efm_stream_encoder.h"
#include "videosynth/efm/subcode_generator.h"
#include "videosynth/interfaces.h"
#include "videosynth/model.h"

namespace videosynth {

// Writes a single audio channel pair as a LaserDisc digital audio channel
// stream next to the CVBS output. The file holds one unsigned byte per pit or
// land run length (T3 to T11, IEC 60908-1999, clause 13), starting at the first
// frame sync, and is the only core component that drives the EFM module.
//
// The first appended sample is synchronous with the first stored video frame;
// callers must append exactly the per-frame sample count
// (EfmAudioSamplesForFrame) per stored output frame, at 44 100 Hz
// (IEC 60856:1986 Amd 2, 13.2 / IEC 60857:1986 Amd 2, 13.2), so the encoded
// stream stays frame-locked. Samples arrive in the 24-bit domain of
// AudioSynthesizer and are converted to the 16-bit CD sample domain here. Left
// and right buffers must be the same length.
//
// Streaming contract: BeginWrite (opens the file and adopts the track table) ->
// repeated AppendFrameAudio -> FinalizeWrite (flushes the CIRC pipeline and
// closes the file). The output path is derived from output.video_path by
// stripping a trailing ".composite" or ".y" suffix and appending
// "_audio_<pair>.efm".
//
// Thread-safety: AudioEfmWriter is NOT thread-safe. Its file stream and encoder
// state are mutated by AppendFrameAudio; it must not be called concurrently
// from multiple threads.
class AudioEfmWriter {
 public:
  explicit AudioEfmWriter(ILogger* logger = nullptr);

  // Opens the derived EFM path for channel pair `channel_pair` (0-7) and
  // adopts `track_table` as the subcode layout of the stream. Creates parent
  // directories as needed. Returns false and appends a message to errors on any
  // failure, including a track table the EFM module rejects.
  bool BeginWrite(const Project& project, int channel_pair,
                  const efm::TrackTable& track_table,
                  std::vector<std::string>* errors);

  // Appends one video frame worth of 24-bit stereo samples. left and right must
  // be the same length. Returns false and appends a message to errors if the
  // session is not open, the buffers differ in length, or the stream fails.
  bool AppendFrameAudio(const std::vector<std::int32_t>& left,
                        const std::vector<std::int32_t>& right,
                        std::vector<std::string>* errors);

  // Flushes the encoder pipeline, writes the remaining T values and closes the
  // file. Returns false and appends a message to errors on any failure.
  bool FinalizeWrite(std::vector<std::string>* errors);

  // Abandons the current write session: closes the stream and removes the
  // partially-written EFM file. No-op when no session is open.
  void AbortWrite();

  // Derives the EFM track path for `channel_pair` from a CVBS output path.
  // Strips a trailing ".composite" or ".y" suffix (if present) and appends
  // "_audio_<pair>.efm".
  static std::string DeriveAudioPath(const std::string& video_path,
                                     int channel_pair);

 private:
  // Writes `t_values` to the open stream. Returns false and appends a message
  // to errors when the stream fails.
  bool WriteTValues(const std::vector<std::uint8_t>& t_values,
                    std::vector<std::string>* errors);

  ILogger* logger_;
  std::ofstream stream_;
  std::string audio_path_;
  efm::EfmStreamEncoder encoder_;
  bool session_open_ = false;
  std::uint64_t t_value_count_ = 0;
};

}  // namespace videosynth
