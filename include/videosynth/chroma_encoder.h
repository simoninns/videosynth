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

#include "videosynth/frame_source.h"
#include "videosynth/model.h"

namespace videosynth {

class IChromaEncoder {
 public:
  virtual ~IChromaEncoder() = default;
  virtual void EncodeLine(const std::vector<YCbCr444Pixel>& source_samples,
                          const std::vector<double>& carrier_phases_rad,
                          std::vector<double>* out_chroma_mv) const = 0;
};

class PalChromaEncoder final : public IChromaEncoder {
 public:
  explicit PalChromaEncoder(double sample_rate_hz);

  void EncodeLine(const std::vector<YCbCr444Pixel>& source_samples,
                  const std::vector<double>& carrier_phases_rad,
                  std::vector<double>* out_chroma_mv) const override;

 private:
  std::vector<double> u_filter_taps_;
  std::vector<double> v_filter_taps_;
};

class NtscChromaEncoder final : public IChromaEncoder {
 public:
  explicit NtscChromaEncoder(double sample_rate_hz);

  void EncodeLine(const std::vector<YCbCr444Pixel>& source_samples,
                  const std::vector<double>& carrier_phases_rad,
                  std::vector<double>* out_chroma_mv) const override;

 private:
  std::vector<double> cb_filter_taps_;
  std::vector<double> cr_filter_taps_;
};

std::unique_ptr<IChromaEncoder> CreateChromaEncoder(Standard standard, double sample_rate_hz);

}  // namespace videosynth