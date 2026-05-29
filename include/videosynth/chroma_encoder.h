/*
 * File:        chroma_encoder.h
 * Module:      chroma_encoder
 * Purpose:     Defines standard-specific chroma encoders for fixed-format frame data.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <memory>
#include <vector>

#include "videosynth/fixed_point.h"
#include "videosynth/progressive_frame_source.h"
#include "videosynth/model.h"

namespace videosynth {

class IChromaEncoder {
 public:
  virtual ~IChromaEncoder() = default;
  virtual void EncodeLineFromPhaseStart(const std::vector<YCbCr444Pixel>& source_samples,
                                        double carrier_phase_start_rad,
                                        std::vector<SampleFixed>* out_chroma_mv) const = 0;
};

class PalChromaEncoder final : public IChromaEncoder {
 public:
  explicit PalChromaEncoder(double sample_rate_hz);

  void EncodeLineFromPhaseStart(const std::vector<YCbCr444Pixel>& source_samples,
                                double carrier_phase_start_rad,
                                std::vector<SampleFixed>* out_chroma_mv) const override;

 private:
  std::vector<double> u_filter_taps_;
  std::vector<double> v_filter_taps_;
  mutable std::vector<double> filtered_u_workspace_;
  mutable std::vector<double> filtered_v_workspace_;
};

class NtscChromaEncoder final : public IChromaEncoder {
 public:
  explicit NtscChromaEncoder(double sample_rate_hz);

  void EncodeLineFromPhaseStart(const std::vector<YCbCr444Pixel>& source_samples,
                                double carrier_phase_start_rad,
                                std::vector<SampleFixed>* out_chroma_mv) const override;

 private:
  std::vector<double> cb_filter_taps_;
  std::vector<double> cr_filter_taps_;
  mutable std::vector<double> filtered_cb_workspace_;
  mutable std::vector<double> filtered_cr_workspace_;
};

std::unique_ptr<IChromaEncoder> CreateChromaEncoder(Standard standard, double sample_rate_hz);

}  // namespace videosynth