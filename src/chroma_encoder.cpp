/*
 * File:        chroma_encoder.cpp
 * Module:      chroma_encoder
 * Purpose:     Encodes fixed-format frame chroma into PAL or NTSC composite subcarrier samples.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/chroma_encoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace videosynth {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kCompositeChromaScaleMillivolts = 350.0;
constexpr double kPalUvCutoffHz = 1.3e6;
constexpr double kNtscCbCrCutoffHz = 1.2e6;
constexpr int kPalFilterTaps = 33;
constexpr int kNtscFilterTaps = 17;
constexpr int kChromaAxisFractionBits = 20;
constexpr std::int64_t kChromaAxisScale = (1LL << kChromaAxisFractionBits);
constexpr int kFirCoeffFractionBits = 30;
constexpr std::int64_t kFirCoeffScale = (1LL << kFirCoeffFractionBits);

int ClampCode(int code, int lo, int hi) {
  return std::max(lo, std::min(code, hi));
}

std::vector<double> DesignLowPassKernel(double cutoff_hz, double sample_rate_hz, int taps) {
  if (sample_rate_hz <= 0.0 || cutoff_hz <= 0.0 || cutoff_hz >= (sample_rate_hz / 2.0) ||
      taps < 3 || (taps % 2) == 0) {
    throw std::invalid_argument("Invalid low-pass filter design parameters");
  }

  const double normalized_cutoff = cutoff_hz / sample_rate_hz;
  const int half_width = taps / 2;
  std::vector<double> kernel(static_cast<std::size_t>(taps), 0.0);
  double gain_sum = 0.0;
  for (int tap = 0; tap < taps; ++tap) {
    const int offset = tap - half_width;
    const double sinc = (offset == 0)
                            ? (2.0 * normalized_cutoff)
                            : std::sin(2.0 * kPi * normalized_cutoff * offset) / (kPi * offset);
    const double window = 0.54 -
                          (0.46 * std::cos((2.0 * kPi * static_cast<double>(tap)) /
                                           static_cast<double>(taps - 1)));
    kernel[static_cast<std::size_t>(tap)] = sinc * window;
    gain_sum += kernel[static_cast<std::size_t>(tap)];
  }

  for (double& tap : kernel) {
    tap /= gain_sum;
  }
  return kernel;
}


std::vector<std::int64_t> QuantizeKernelToFixed(const std::vector<double>& kernel) {
  std::vector<std::int64_t> fixed(kernel.size(), 0);
  for (std::size_t i = 0; i < kernel.size(); ++i) {
    fixed[i] = static_cast<std::int64_t>(
        std::llround(kernel[i] * static_cast<double>(kFirCoeffScale)));
  }
  return fixed;
}

void ExtractCbAxisFixed(const std::vector<YCbCr444Pixel>& source_samples,
                        std::vector<std::int64_t>* axis_fixed) {
  if (axis_fixed == nullptr) {
    throw std::invalid_argument("Cb axis fixed output pointer must not be null");
  }

  axis_fixed->resize(source_samples.size());
  for (std::size_t index = 0; index < source_samples.size(); ++index) {
    const int cb = ClampCode(source_samples[index].cb, 64, 960);
    (*axis_fixed)[index] =
        static_cast<std::int64_t>(cb - 512) * kChromaAxisScale / 448;
  }
}

void ExtractCrAxisFixed(const std::vector<YCbCr444Pixel>& source_samples,
                        std::vector<std::int64_t>* axis_fixed) {
  if (axis_fixed == nullptr) {
    throw std::invalid_argument("Cr axis fixed output pointer must not be null");
  }

  axis_fixed->resize(source_samples.size());
  for (std::size_t index = 0; index < source_samples.size(); ++index) {
    const int cr = ClampCode(source_samples[index].cr, 64, 960);
    (*axis_fixed)[index] =
        static_cast<std::int64_t>(cr - 512) * kChromaAxisScale / 448;
  }
}

void ApplyFirFilterFixed(const std::vector<std::int64_t>& input,
                         const std::vector<std::int64_t>& kernel,
                         std::vector<std::int64_t>* output,
                         std::vector<std::int64_t>* padded_workspace) {
  if (output == nullptr || padded_workspace == nullptr) {
    throw std::invalid_argument("Fixed FIR output/workspace pointer must not be null");
  }

  if (input.empty()) {
    output->clear();
    padded_workspace->clear();
    return;
  }

  const int kernel_size = static_cast<int>(kernel.size());
  const int half_width = kernel_size / 2;
  padded_workspace->resize(input.size() + static_cast<std::size_t>(2 * half_width));
  std::fill_n(padded_workspace->begin(), half_width, input.front());
  std::copy(input.begin(), input.end(), padded_workspace->begin() + half_width);
  std::fill(padded_workspace->begin() + half_width + static_cast<std::ptrdiff_t>(input.size()),
            padded_workspace->end(),
            input.back());

  output->assign(input.size(), 0);
  for (std::size_t index = 0; index < input.size(); ++index) {
    std::int64_t acc = 0;
    for (int tap = 0; tap < kernel_size; ++tap) {
      acc += (*padded_workspace)[index + static_cast<std::size_t>(tap)] *
             kernel[static_cast<std::size_t>(tap)];
    }
    (*output)[index] = RoundShiftRightSigned(acc, kFirCoeffFractionBits);
  }
}

void ModulateQuadratureFromPhaseStart(const std::vector<double>& axis_sin,
                                      const std::vector<double>& axis_cos,
                                      double carrier_phase_start_rad,
                                      std::vector<SampleFixed>* out_chroma_mv) {
  if (out_chroma_mv == nullptr) {
    throw std::invalid_argument("Output chroma line pointer must not be null");
  }

  out_chroma_mv->assign(axis_sin.size(), MillivoltsToSampleFixed(0.0));
  if (axis_sin.empty()) {
    return;
  }

  double sin_phase = std::sin(carrier_phase_start_rad);
  double cos_phase = std::cos(carrier_phase_start_rad);
  for (std::size_t index = 0; index < axis_sin.size(); ++index) {
    (*out_chroma_mv)[index] = MillivoltsToSampleFixed(
        kCompositeChromaScaleMillivolts *
        ((axis_sin[index] * sin_phase) + (axis_cos[index] * cos_phase)));

    const double next_sin = cos_phase;
    const double next_cos = -sin_phase;
    sin_phase = next_sin;
    cos_phase = next_cos;
  }
}

}  // namespace

PalChromaEncoder::PalChromaEncoder(double sample_rate_hz)
    : u_filter_taps_(DesignLowPassKernel(kPalUvCutoffHz, sample_rate_hz, kPalFilterTaps)),
      v_filter_taps_(DesignLowPassKernel(kPalUvCutoffHz, sample_rate_hz, kPalFilterTaps)) {}

void PalChromaEncoder::EncodeLineFromPhaseStart(const std::vector<YCbCr444Pixel>& source_samples,
                                                double carrier_phase_start_rad,
                                                std::vector<SampleFixed>* out_chroma_mv) const {
  if (out_chroma_mv == nullptr) {
    throw std::invalid_argument("Output chroma line pointer must not be null");
  }

  std::vector<std::int64_t> cb_axis_fixed;
  std::vector<std::int64_t> cr_axis_fixed;
  std::vector<std::int64_t> filtered_u_fixed;
  std::vector<std::int64_t> filtered_v_fixed;
  std::vector<std::int64_t> fir_pad_fixed;
  const std::vector<std::int64_t> u_taps_fixed = QuantizeKernelToFixed(u_filter_taps_);
  const std::vector<std::int64_t> v_taps_fixed = QuantizeKernelToFixed(v_filter_taps_);

  ExtractCbAxisFixed(source_samples, &cb_axis_fixed);
  ExtractCrAxisFixed(source_samples, &cr_axis_fixed);
  ApplyFirFilterFixed(cb_axis_fixed, u_taps_fixed, &filtered_u_fixed, &fir_pad_fixed);
  ApplyFirFilterFixed(cr_axis_fixed, v_taps_fixed, &filtered_v_fixed, &fir_pad_fixed);

  filtered_u_workspace_.resize(filtered_u_fixed.size());
  filtered_v_workspace_.resize(filtered_v_fixed.size());
  for (std::size_t i = 0; i < filtered_u_fixed.size(); ++i) {
    filtered_u_workspace_[i] =
        static_cast<double>(filtered_u_fixed[i]) / static_cast<double>(kChromaAxisScale);
    filtered_v_workspace_[i] =
        static_cast<double>(filtered_v_fixed[i]) / static_cast<double>(kChromaAxisScale);
  }

  ModulateQuadratureFromPhaseStart(filtered_u_workspace_,
                                   filtered_v_workspace_,
                                   carrier_phase_start_rad,
                                   out_chroma_mv);
}

NtscChromaEncoder::NtscChromaEncoder(double sample_rate_hz)
    : cb_filter_taps_(DesignLowPassKernel(kNtscCbCrCutoffHz, sample_rate_hz, kNtscFilterTaps)),
      cr_filter_taps_(DesignLowPassKernel(kNtscCbCrCutoffHz, sample_rate_hz, kNtscFilterTaps)) {}

void NtscChromaEncoder::EncodeLineFromPhaseStart(const std::vector<YCbCr444Pixel>& source_samples,
                                                 double carrier_phase_start_rad,
                                                 std::vector<SampleFixed>* out_chroma_mv) const {
  if (out_chroma_mv == nullptr) {
    throw std::invalid_argument("Output chroma line pointer must not be null");
  }

  std::vector<std::int64_t> cb_axis_fixed;
  std::vector<std::int64_t> cr_axis_fixed;
  std::vector<std::int64_t> filtered_cb_fixed;
  std::vector<std::int64_t> filtered_cr_fixed;
  std::vector<std::int64_t> fir_pad_fixed;
  const std::vector<std::int64_t> cb_taps_fixed = QuantizeKernelToFixed(cb_filter_taps_);
  const std::vector<std::int64_t> cr_taps_fixed = QuantizeKernelToFixed(cr_filter_taps_);

  ExtractCbAxisFixed(source_samples, &cb_axis_fixed);
  ExtractCrAxisFixed(source_samples, &cr_axis_fixed);
  ApplyFirFilterFixed(cb_axis_fixed, cb_taps_fixed, &filtered_cb_fixed, &fir_pad_fixed);
  ApplyFirFilterFixed(cr_axis_fixed, cr_taps_fixed, &filtered_cr_fixed, &fir_pad_fixed);

  filtered_cb_workspace_.resize(filtered_cb_fixed.size());
  filtered_cr_workspace_.resize(filtered_cr_fixed.size());
  for (std::size_t i = 0; i < filtered_cb_fixed.size(); ++i) {
    filtered_cb_workspace_[i] =
        static_cast<double>(filtered_cb_fixed[i]) / static_cast<double>(kChromaAxisScale);
    filtered_cr_workspace_[i] =
        static_cast<double>(filtered_cr_fixed[i]) / static_cast<double>(kChromaAxisScale);
  }

  ModulateQuadratureFromPhaseStart(filtered_cb_workspace_,
                                   filtered_cr_workspace_,
                                   carrier_phase_start_rad,
                                   out_chroma_mv);
}

std::unique_ptr<IChromaEncoder> CreateChromaEncoder(Standard standard, double sample_rate_hz) {
  if (standard == Standard::kPal) {
    return std::make_unique<PalChromaEncoder>(sample_rate_hz);
  }
  if (standard == Standard::kNtsc) {
    return std::make_unique<NtscChromaEncoder>(sample_rate_hz);
  }
  return nullptr;
}

}  // namespace videosynth