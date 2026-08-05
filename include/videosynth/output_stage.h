/*
 * File:        output_stage.h
 * Module:      output_stage
 * Purpose:     Writes generated sample buffers to output and metadata files.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <fstream>
#include <vector>

#include "videosynth/cvbs_quantization.h"
#include "videosynth/interfaces.h"

namespace videosynth {

// Sample encoding applied to generated millivolt samples on the way out, as
// selected by the project's sample_encoding_preset.
enum class OutputSampleEncoding {
  kCvbsU10,
  kCvbsU16,
  kCvbsTpg21,
  kCvbsS16Fsc,
  kRawS16,
};

// Thread-safety: OutputStage is NOT thread-safe. Inherits the NOT thread-safe
// guarantee from IOutputStage. Concurrent calls to any public method will
// result in undefined behavior. The ofstream member (video_stream_) is not
// protected by any mutex.
class OutputStage final : public IOutputStage {
 public:
  explicit OutputStage(ILogger* logger = nullptr);

  // Opens the output video file and initializes the write session.
  //
  // Args:
  //   project: The project configuration containing output paths.
  //   expected_frame_count: Total number of frames to be written.
  //   errors: Output vector for any error messages.
  //
  // Returns:
  //   true on success, false on any error.
  bool BeginWrite(const Project& project, std::size_t expected_frame_count,
                  std::vector<std::string>* errors) override;

  // Appends Y and C sample buffers for one or more frames to the output file.
  //
  // Args:
  //   y_mv: Luma samples to write.
  //   c_mv: Chroma samples to write.
  //   errors: Output vector for any error messages.
  //
  // Returns:
  //   true on success, false on any error.
  bool AppendSamples(const std::vector<SampleFixed>& y_mv,
                     const std::vector<SampleFixed>& c_mv,
                     std::vector<std::string>* errors) override;

  // Finalizes the write session, closing the output file.
  //
  // Args:
  //   errors: Output vector for any error messages.
  //
  // Returns:
  //   true on success, false on any error.
  bool FinalizeWrite(std::vector<std::string>* errors) override;

  // Abandons the current write session: closes the video (and chroma)
  // streams and removes the partially-written video, chroma, and metadata
  // files. No-op when no session is open.
  void AbortWrite() override;

  // Convenience method that performs BeginWrite, AppendSamples, and
  // FinalizeWrite in sequence for a complete single-frame write.
  //
  // Args:
  //   project: The project configuration containing output paths.
  //   y_mv: Luma samples to write.
  //   c_mv: Chroma samples to write.
  //   errors: Output vector for any error messages.
  //
  // Returns:
  //   true on success, false on any error.
  bool Write(const Project& project, const std::vector<SampleFixed>& y_mv,
             const std::vector<SampleFixed>& c_mv,
             std::vector<std::string>* errors) override;

 private:
  bool IsSessionOpen() const;

  // Encodes one frame of input samples into the reusable code buffers and
  // writes them with a single stream write per file. Resampling to a different
  // output frame span, when the encoding preset needs it, happens first.
  //
  // Args:
  //   y_mv: Luma samples of the whole append, indexed from frame_start.
  //   c_mv: Chroma samples of the whole append, indexed from frame_start.
  //   frame_start: Index of the first sample of the frame to encode.
  bool WriteEncodedFrame(const std::vector<SampleFixed>& y_mv,
                         const std::vector<SampleFixed>& c_mv,
                         std::size_t frame_start);

  ILogger* logger_;
  bool write_session_open_ = false;
  Project current_project_;
  std::ofstream video_stream_;
  std::ofstream chroma_stream_;
  std::size_t expected_frame_count_ = 0;
  std::size_t written_samples_ = 0;
  std::size_t input_frame_span_ = 0;
  std::size_t output_frame_span_ = 0;
  bool has_nonstandard_ = false;

  // Session-invariant encoding state, resolved once in BeginWrite.
  QuantizationProfile quantization_{};
  OutputSampleEncoding output_encoding_ = OutputSampleEncoding::kCvbsU10;
  bool is_yc_output_ = false;

  // Per-frame scratch, allocated on the first frame and reused thereafter, so
  // steady-state writing performs no allocation and one write per file.
  std::vector<SampleFixed> composite_scratch_;
  std::vector<SampleFixed> resampled_luma_;
  std::vector<SampleFixed> resampled_chroma_;
  std::vector<std::int16_t> luma_codes_;
  std::vector<std::int16_t> chroma_codes_;
};

}  // namespace videosynth
