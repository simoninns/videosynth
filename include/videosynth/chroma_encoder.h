/*
 * File:        chroma_encoder.h
 * Module:      chroma_encoder
 * Purpose:     Defines standard-specific chroma encoders for fixed-format frame
 * data.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <memory>
#include <vector>

#include "videosynth/fixed_point.h"
#include "videosynth/model.h"
#include "videosynth/progressive_frame_source.h"

namespace videosynth {

// Thread-safety: Implementations of IChromaEncoder are NOT thread-safe.
// Callers must ensure sequential access. Concurrent calls to
// EncodeLineFromPhaseStart from multiple threads will result in undefined
// behavior due to mutable workspace state.
class IChromaEncoder {
 public:
  virtual ~IChromaEncoder() = default;
  // Ownership: out_chroma_mv is an output parameter. The caller owns the
  // pointed-to vector and must ensure the pointer is valid (non-null).
  // The implementation clears and populates this vector but does not take
  // ownership.
  virtual void EncodeLineFromPhaseStart(
      const std::vector<YCbCr444Pixel>& source_samples,
      double carrier_phase_start_rad,
      std::vector<SampleFixed>* out_chroma_mv) const = 0;
};

// Thread-safety: PalChromaEncoder is NOT thread-safe due to mutable
// workspace buffers (filtered_u_workspace_, filtered_v_workspace_).
// Callers must serialize access or create one encoder per thread.
class PalChromaEncoder final : public IChromaEncoder {
 public:
  // Constructs a PAL chroma encoder with the given sample rate.
  //
  // Args:
  //   sample_rate_hz: The output sample rate, used to compute filter tap
  //   counts.
  explicit PalChromaEncoder(double sample_rate_hz);

  // Encodes a line of source pixels into chroma samples with color subcarrier.
  //
  // Args:
  //   source_samples: Input pixels in YCbCr 4:4:4 format.
  //   carrier_phase_start_rad: Phase of the color subcarrier at the start
  //     of the line in radians.
  //   out_chroma_mv: Output vector for chroma samples (C channel).
  void EncodeLineFromPhaseStart(
      const std::vector<YCbCr444Pixel>& source_samples,
      double carrier_phase_start_rad,
      std::vector<SampleFixed>* out_chroma_mv) const override;

 private:
  std::vector<double> u_filter_taps_;
  std::vector<double> v_filter_taps_;
  mutable std::vector<double> filtered_u_workspace_;
  mutable std::vector<double> filtered_v_workspace_;
};

// Thread-safety: NtscChromaEncoder is NOT thread-safe due to mutable
// workspace buffers (filtered_cb_workspace_, filtered_cr_workspace_).
// Callers must serialize access or create one encoder per thread.
class NtscChromaEncoder final : public IChromaEncoder {
 public:
  // Constructs an NTSC chroma encoder with the given sample rate.
  //
  // Args:
  //   sample_rate_hz: The output sample rate, used to compute filter tap
  //   counts.
  explicit NtscChromaEncoder(double sample_rate_hz);

  // Encodes a line of source pixels into chroma samples with color subcarrier.
  //
  // Args:
  //   source_samples: Input pixels in YCbCr 4:4:4 format.
  //   carrier_phase_start_rad: Phase of the color subcarrier at the start
  //     of the line in radians.
  //   out_chroma_mv: Output vector for chroma samples (C channel).
  void EncodeLineFromPhaseStart(
      const std::vector<YCbCr444Pixel>& source_samples,
      double carrier_phase_start_rad,
      std::vector<SampleFixed>* out_chroma_mv) const override;

 private:
  std::vector<double> cb_filter_taps_;
  std::vector<double> cr_filter_taps_;
  mutable std::vector<double> filtered_cb_workspace_;
  mutable std::vector<double> filtered_cr_workspace_;
};

// PAL-M chroma encoder: PAL UV quadrature encoding with System M signal levels.
//
// ITU-R BT.470-6 Table 2 (M/PAL): PAL-M uses the same PAL encoding algorithm
// (U on sin, V on cos, phase-alternating V) as 625-line PAL, but with System M
// white level (714.3 mV) instead of 625-line PAL white level (700 mV).
//
// Thread-safety: NOT thread-safe due to mutable workspace buffers.
// Callers must serialize access or create one encoder per thread.
class PalMChromaEncoder final : public IChromaEncoder {
 public:
  explicit PalMChromaEncoder(double sample_rate_hz);

  void EncodeLineFromPhaseStart(
      const std::vector<YCbCr444Pixel>& source_samples,
      double carrier_phase_start_rad,
      std::vector<SampleFixed>* out_chroma_mv) const override;

 private:
  std::vector<double> u_filter_taps_;
  std::vector<double> v_filter_taps_;
  mutable std::vector<double> filtered_u_workspace_;
  mutable std::vector<double> filtered_v_workspace_;
};

// Thread-safety: CreateChromaEncoder returns a new encoder instance. The
// caller owns the returned unique_ptr and is responsible for thread-safety.
std::unique_ptr<IChromaEncoder> CreateChromaEncoder(Standard standard,
                                                    double sample_rate_hz);

}  // namespace videosynth