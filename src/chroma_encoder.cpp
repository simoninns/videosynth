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
#include <memory>
#include <stdexcept>
#include <vector>

namespace videosynth {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kCompositeChromaScaleMillivolts = 350.0;
constexpr double kPalUvCutoffHz = 1.3e6;
constexpr double kNtscICutoffHz = 1.3e6;
constexpr double kNtscQCutoffHz = 0.5e6;
constexpr int kPalFilterTaps = 33;
constexpr int kNtscIFilterTaps = 33;
constexpr int kNtscQFilterTaps = 65;

int ClampCode(int code, int lo, int hi) {
  return std::max(lo, std::min(code, hi));
}

double NormalizedCb(const YCbCr444Pixel& pixel) {
  const int cb = ClampCode(pixel.cb, 64, 960);
  return static_cast<double>(cb - 512) / 448.0;
}

double NormalizedCr(const YCbCr444Pixel& pixel) {
  const int cr = ClampCode(pixel.cr, 64, 960);
  return static_cast<double>(cr - 512) / 448.0;
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

std::vector<double> ApplyFirFilter(const std::vector<double>& input,
                                   const std::vector<double>& kernel) {
  if (input.empty()) {
    return {};
  }

  const int kernel_size = static_cast<int>(kernel.size());
  const int half_width = kernel_size / 2;
  std::vector<double> output(input.size(), 0.0);
  for (std::size_t index = 0; index < input.size(); ++index) {
    double sum = 0.0;
    for (int tap = 0; tap < kernel_size; ++tap) {
      const int sample_index = static_cast<int>(index) + tap - half_width;
      const int clamped_index = std::max(0, std::min(static_cast<int>(input.size()) - 1, sample_index));
      sum += input[static_cast<std::size_t>(clamped_index)] * kernel[static_cast<std::size_t>(tap)];
    }
    output[index] = sum;
  }
  return output;
}

void ValidateLineArguments(const std::vector<YCbCr444Pixel>& source_samples,
                           const std::vector<double>& carrier_phases_rad,
                           std::vector<double>* out_chroma_mv) {
  if (out_chroma_mv == nullptr) {
    throw std::invalid_argument("Output chroma line pointer must not be null");
  }
  if (source_samples.size() != carrier_phases_rad.size()) {
    throw std::invalid_argument("Source samples and carrier phases must have the same length");
  }
}

std::vector<double> ExtractCbAxis(const std::vector<YCbCr444Pixel>& source_samples) {
  std::vector<double> axis;
  axis.reserve(source_samples.size());
  for (const YCbCr444Pixel& pixel : source_samples) {
    axis.push_back(NormalizedCb(pixel));
  }
  return axis;
}

std::vector<double> ExtractCrAxis(const std::vector<YCbCr444Pixel>& source_samples) {
  std::vector<double> axis;
  axis.reserve(source_samples.size());
  for (const YCbCr444Pixel& pixel : source_samples) {
    axis.push_back(NormalizedCr(pixel));
  }
  return axis;
}

std::vector<double> ExtractIAxis(const std::vector<YCbCr444Pixel>& source_samples) {
  std::vector<double> axis;
  axis.reserve(source_samples.size());
  for (const YCbCr444Pixel& pixel : source_samples) {
    const double cb_norm = NormalizedCb(pixel);
    const double cr_norm = NormalizedCr(pixel);
    axis.push_back((-0.27 * cb_norm) + (0.74 * cr_norm));
  }
  return axis;
}

std::vector<double> ExtractQAxis(const std::vector<YCbCr444Pixel>& source_samples) {
  std::vector<double> axis;
  axis.reserve(source_samples.size());
  for (const YCbCr444Pixel& pixel : source_samples) {
    const double cb_norm = NormalizedCb(pixel);
    const double cr_norm = NormalizedCr(pixel);
    axis.push_back((0.41 * cb_norm) + (0.48 * cr_norm));
  }
  return axis;
}

}  // namespace

PalChromaEncoder::PalChromaEncoder(double sample_rate_hz)
    : u_filter_taps_(DesignLowPassKernel(kPalUvCutoffHz, sample_rate_hz, kPalFilterTaps)),
      v_filter_taps_(DesignLowPassKernel(kPalUvCutoffHz, sample_rate_hz, kPalFilterTaps)) {}

void PalChromaEncoder::EncodeLine(const std::vector<YCbCr444Pixel>& source_samples,
                                  const std::vector<double>& carrier_phases_rad,
                                  std::vector<double>* out_chroma_mv) const {
  ValidateLineArguments(source_samples, carrier_phases_rad, out_chroma_mv);

  const std::vector<double> filtered_u = ApplyFirFilter(ExtractCbAxis(source_samples), u_filter_taps_);
  const std::vector<double> filtered_v = ApplyFirFilter(ExtractCrAxis(source_samples), v_filter_taps_);

  out_chroma_mv->assign(source_samples.size(), 0.0);
  for (std::size_t index = 0; index < source_samples.size(); ++index) {
    // The high-level design requires PAL chroma bandlimiting before quadrature
    // modulation. This encoder therefore low-passes the two colour-difference axes
    // symmetrically to roughly 1.3 MHz, then modulates them using the same carrier
    // phase sequence that drives burst generation. The timing model owns the line-
    // by-line PAL phase alternation, matching the composite split described by
    // ITU-R BT.470-6 Table 2 item 2.12 and ITU-R BT.1700 Annex 1 Part B.
    (*out_chroma_mv)[index] = kCompositeChromaScaleMillivolts *
                              ((filtered_v[index] * std::sin(carrier_phases_rad[index])) +
                               (filtered_u[index] * std::cos(carrier_phases_rad[index])));
  }
}

NtscChromaEncoder::NtscChromaEncoder(double sample_rate_hz)
    : i_filter_taps_(DesignLowPassKernel(kNtscICutoffHz, sample_rate_hz, kNtscIFilterTaps)),
      q_filter_taps_(DesignLowPassKernel(kNtscQCutoffHz, sample_rate_hz, kNtscQFilterTaps)) {}

void NtscChromaEncoder::EncodeLine(const std::vector<YCbCr444Pixel>& source_samples,
                                   const std::vector<double>& carrier_phases_rad,
                                   std::vector<double>* out_chroma_mv) const {
  ValidateLineArguments(source_samples, carrier_phases_rad, out_chroma_mv);

  const std::vector<double> filtered_i = ApplyFirFilter(ExtractIAxis(source_samples), i_filter_taps_);
  const std::vector<double> filtered_q = ApplyFirFilter(ExtractQAxis(source_samples), q_filter_taps_);

  out_chroma_mv->assign(source_samples.size(), 0.0);
  for (std::size_t index = 0; index < source_samples.size(); ++index) {
    // SMPTE 170M-2004 Section 6 defines the NTSC I/Q axes, and the high-level
    // design requires the Section 7 bandwidth split before quadrature modulation.
    // This path therefore rotates BT.601 Cb/Cr to I/Q, filters I to about 1.3 MHz
    // and Q to about 0.5 MHz, then modulates both using the same carrier phase
    // sequence that also drives the line's burst waveform.
    (*out_chroma_mv)[index] = kCompositeChromaScaleMillivolts *
                              ((filtered_i[index] * std::sin(carrier_phases_rad[index])) +
                               (filtered_q[index] * std::cos(carrier_phases_rad[index])));
  }
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