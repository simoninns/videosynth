/*
 * File:        audio_wav_writer.h
 * Module:      audio_wav_writer
 * Purpose:     Streams one frame-locked stereo 24-bit PCM channel pair to a
 *              RIFF/WAVE file alongside the generated CVBS output.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "videosynth/interfaces.h"
#include "videosynth/model.h"

namespace videosynth {

// Writes a single audio channel pair as a RIFF/WAVE file next to the CVBS
// output. Each channel pair is two independent channels (left = SMPTE 272M odd
// channel, right = even channel); the file is always 2-channel, 24-bit signed
// little-endian PCM (format tag 0x0001) at 48000 Hz per the CVBS File Format
// Specification (Audio Data / WAV File Format).
//
// The first appended sample is synchronous with the first stored video frame;
// callers must append exactly the per-frame sample count (AudioSamplesForFrame)
// per stored output frame so the emitted track stays sample-accurately
// frame-locked. Left and right buffers must be the same length.
//
// Streaming contract: BeginWrite (reserves the 44-byte header) -> repeated
// AppendFrameAudio -> FinalizeWrite (back-patches the RIFF and data chunk
// sizes). The output path is derived from output.video_path by stripping a
// trailing ".composite" or ".y" suffix and appending "_audio_<pair>.wav".
//
// Thread-safety: AudioWavWriter is NOT thread-safe. Its file stream and byte
// counters are mutated by AppendFrameAudio; it must not be called concurrently
// from multiple threads.
class AudioWavWriter {
 public:
  explicit AudioWavWriter(ILogger* logger = nullptr);

  // Opens the derived WAV path for channel pair `channel_pair` (0–7), reserves
  // the header, and records the 48000 Hz rate for the fmt chunk. Creates parent
  // directories as needed. Returns false and appends a message to errors on any
  // failure.
  bool BeginWrite(const Project& project, int channel_pair,
                  std::vector<std::string>* errors);

  // Appends one interleaved stereo frame (left/right 24-bit samples) to the
  // data chunk. left and right must be the same length. Returns false and
  // appends a message to errors if the session is not open, the buffers differ
  // in length, or the stream fails.
  bool AppendFrameAudio(const std::vector<std::int32_t>& left,
                        const std::vector<std::int32_t>& right,
                        std::vector<std::string>* errors);

  // Back-patches the RIFF and data chunk sizes and closes the file. Returns
  // false and appends a message to errors on any failure.
  bool FinalizeWrite(std::vector<std::string>* errors);

  // Abandons the current write session: closes the stream and removes the
  // partially-written WAV file. No-op when no session is open.
  void AbortWrite();

  // Derives the audio track path for `channel_pair` from a CVBS output path.
  // Strips a trailing ".composite" or ".y" suffix (if present) and appends
  // "_audio_<pair>.wav".
  static std::string DeriveAudioPath(const std::string& video_path,
                                     int channel_pair);

 private:
  ILogger* logger_;
  std::ofstream stream_;
  std::string audio_path_;
  bool session_open_ = false;
  int header_sample_rate_hz_ = 0;
  std::uint64_t data_bytes_ = 0;
};

}  // namespace videosynth
