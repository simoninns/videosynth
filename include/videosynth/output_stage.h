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

// Thread-safety: OutputStage is NOT thread-safe, except for EncodeFrame.
// Inherits the guarantee from IOutputStage: the ofstream members and the
// session counters are unsynchronised, so every session method must be called
// from one thread. EncodeFrame only reads the session state resolved by
// BeginWrite and writes into the caller's buffer (its resampling scratch is
// thread-local), so synthesis workers may encode concurrently while the
// session owner writes an earlier frame.
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

  // Encodes exactly one frame of Y and C samples into output codes.
  //
  // Reads only the session encoding state resolved by BeginWrite, so it is
  // const and safe to call concurrently from synthesis workers while the
  // session owner writes an earlier frame.
  //
  // Args:
  //   y_mv: Luma samples of one frame (one input frame span long).
  //   c_mv: Chroma samples of the same frame.
  //   out_frame: Receives the encoded codes and the frame's nonstandard flag.
  //   errors: Output vector for any error messages.
  //
  // Returns:
  //   true on success, false on any error.
  bool EncodeFrame(const std::vector<SampleFixed>& y_mv,
                   const std::vector<SampleFixed>& c_mv,
                   EncodedFrame* out_frame,
                   std::vector<std::string>* errors) const override;

  // Writes one encoded frame produced by EncodeFrame. Frames must be appended
  // in output order.
  //
  // Args:
  //   frame: The encoded frame to write.
  //   errors: Output vector for any error messages.
  //
  // Returns:
  //   true on success, false on any error.
  bool AppendEncodedFrame(const EncodedFrame& frame,
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

  // Encodes the frame that starts at frame_start into out_frame's code
  // buffers. Resampling to a different output frame span, when the encoding
  // preset needs it, happens first, through thread-local scratch so concurrent
  // encoders never share a buffer.
  //
  // Args:
  //   y_mv: Luma samples of the whole append, indexed from frame_start.
  //   c_mv: Chroma samples of the whole append, indexed from frame_start.
  //   frame_start: Index of the first sample of the frame to encode.
  //   out_frame: Receives the encoded codes and the frame's nonstandard flag.
  void EncodeFrameInto(const std::vector<SampleFixed>& y_mv,
                       const std::vector<SampleFixed>& c_mv,
                       std::size_t frame_start, EncodedFrame* out_frame) const;

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

  // Encoding target for AppendSamples, allocated on the first frame and reused
  // thereafter, so writing a batch performs no allocation and one write per
  // file per frame. Callers that encode on their own threads pass their own
  // EncodedFrame to EncodeFrame instead.
  EncodedFrame append_scratch_;
};

}  // namespace videosynth
