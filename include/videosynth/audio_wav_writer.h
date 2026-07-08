/*
 * File:        audio_wav_writer.h
 * Module:      audio_wav_writer
 * Purpose:     Streams frame-locked stereo 16-bit PCM audio to a RIFF/WAVE file
 *              alongside the generated CVBS output.
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

// Writes a single frame-locked audio track as a RIFF/WAVE file next to the
// CVBS output. The waveform is synthesised mono per frame and duplicated to
// both stereo channels; the file is always 2-channel, 16-bit signed
// little-endian PCM (format tag 0x0001) per the CVBS File Format Specification
// (Audio Data).
//
// The header sample rate (nSamplesPerSec) is taken from the video standard:
// 44100 for PAL, 44056 for the System M standards (NTSC, PAL-M). The first
// appended sample is synchronous with the first stored video frame; callers
// must append exactly samples_per_frame mono samples per stored output frame so
// the emitted track stays sample-accurately frame-locked.
//
// Streaming contract: BeginWrite (reserves the 44-byte header) -> repeated
// AppendFrameAudio -> FinalizeWrite (back-patches the RIFF and data chunk
// sizes). The output path is derived from output.video_path by stripping a
// trailing ".composite" or ".y" suffix and appending "_audio_00.wav".
//
// Thread-safety: AudioWavWriter is NOT thread-safe. Its file stream and byte
// counters are mutated by AppendFrameAudio; it must not be called concurrently
// from multiple threads.
class AudioWavWriter {
 public:
  explicit AudioWavWriter(ILogger* logger = nullptr);

  // Opens the derived WAV path, reserves the header, and records the standard's
  // integer sample rate for the fmt chunk. Creates parent directories as
  // needed. Returns false and appends a message to errors on any failure.
  bool BeginWrite(const Project& project, std::vector<std::string>* errors);

  // Duplicates mono_samples to left/right and appends the interleaved stereo
  // frame to the data chunk. Returns false and appends a message to errors if
  // the session is not open or the stream fails.
  bool AppendFrameAudio(const std::vector<std::int16_t>& mono_samples,
                        std::vector<std::string>* errors);

  // Back-patches the RIFF and data chunk sizes and closes the file. Returns
  // false and appends a message to errors on any failure.
  bool FinalizeWrite(std::vector<std::string>* errors);

  // Abandons the current write session: closes the stream and removes the
  // partially-written WAV file. No-op when no session is open.
  void AbortWrite();

  // Derives the audio track path from a CVBS output path. Strips a trailing
  // ".composite" or ".y" suffix (if present) and appends "_audio_00.wav".
  static std::string DeriveAudioPath(const std::string& video_path);

 private:
  ILogger* logger_;
  std::ofstream stream_;
  std::string audio_path_;
  bool session_open_ = false;
  int header_sample_rate_hz_ = 0;
  std::uint64_t data_bytes_ = 0;
};

}  // namespace videosynth
