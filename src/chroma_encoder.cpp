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
constexpr double kNtscCbCrCutoffHz = 1.2e6;
constexpr int kPalFilterTaps = 33;
constexpr int kNtscFilterTaps = 17;

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

template <std::size_t kTapCount>
void ApplyFixedTapFir(const std::vector<double>& input,
                      const std::vector<double>& kernel,
                      std::vector<double>* output,
                      std::vector<double>* padded_workspace) {
  if (output == nullptr) {
    throw std::invalid_argument("Output FIR buffer pointer must not be null");
  }
  if (padded_workspace == nullptr) {
    throw std::invalid_argument("FIR workspace pointer must not be null");
  }
  if (kernel.size() != kTapCount) {
    throw std::invalid_argument("Unexpected fixed-tap kernel size");
  }

  if (input.empty()) {
    output->clear();
    padded_workspace->clear();
    return;
  }

  constexpr std::size_t kHalfWidth = kTapCount / 2;
  const std::size_t input_size = input.size();

  padded_workspace->resize(input_size + (2 * kHalfWidth));
  std::fill_n(padded_workspace->begin(), kHalfWidth, input.front());
  std::copy(input.begin(), input.end(), padded_workspace->begin() + static_cast<std::ptrdiff_t>(kHalfWidth));
  std::fill(padded_workspace->begin() + static_cast<std::ptrdiff_t>(kHalfWidth + input_size),
            padded_workspace->end(),
            input.back());

  output->assign(input_size, 0.0);
  const double* kernel_data = kernel.data();
  const double* padded_data = padded_workspace->data();

  for (std::size_t index = 0; index < input_size; ++index) {
    const double* sample = padded_data + index;
    double sum = 0.0;

    std::size_t tap = 0;
    for (; tap + 3 < kTapCount; tap += 4) {
      sum += (sample[tap] * kernel_data[tap]) + (sample[tap + 1] * kernel_data[tap + 1]) +
             (sample[tap + 2] * kernel_data[tap + 2]) + (sample[tap + 3] * kernel_data[tap + 3]);
    }
    for (; tap < kTapCount; ++tap) {
      sum += sample[tap] * kernel_data[tap];
    }
    (*output)[index] = sum;
  }
}

void ApplyFirFilter(const std::vector<double>& input,
                    const std::vector<double>& kernel,
                    std::vector<double>* output,
                    std::vector<double>* padded_workspace) {
  if (kernel.size() == static_cast<std::size_t>(kPalFilterTaps)) {
    ApplyFixedTapFir<static_cast<std::size_t>(kPalFilterTaps)>(
        input, kernel, output, padded_workspace);
    return;
  }
  if (kernel.size() == static_cast<std::size_t>(kNtscFilterTaps)) {
    ApplyFixedTapFir<static_cast<std::size_t>(kNtscFilterTaps)>(
        input, kernel, output, padded_workspace);
    return;
  }

  if (output == nullptr) {
    throw std::invalid_argument("Output FIR buffer pointer must not be null");
  }

  if (input.empty()) {
    output->clear();
    return;
  }

  const int kernel_size = static_cast<int>(kernel.size());
  const int half_width = kernel_size / 2;
  output->assign(input.size(), 0.0);
  for (std::size_t index = 0; index < input.size(); ++index) {
    double sum = 0.0;
    for (int tap = 0; tap < kernel_size; ++tap) {
      const int sample_index = static_cast<int>(index) + tap - half_width;
      const int clamped_index = std::max(0, std::min(static_cast<int>(input.size()) - 1, sample_index));
      sum += input[static_cast<std::size_t>(clamped_index)] * kernel[static_cast<std::size_t>(tap)];
    }
    (*output)[index] = sum;
  }
}

bool IsQuarterWaveStep(double phase_step) {
  constexpr double kQuarterWave = kPi / 2.0;
  constexpr double kTolerance = 1e-9;
  const double wrapped = std::remainder(phase_step - kQuarterWave, 2.0 * kPi);
  return std::fabs(wrapped) <= kTolerance;
}

void ModulateQuadrature(const std::vector<double>& axis_sin,
                        const std::vector<double>& axis_cos,
                        const std::vector<double>& carrier_phases_rad,
                        std::vector<SampleFixed>* out_chroma_mv) {
  if (out_chroma_mv == nullptr) {
    throw std::invalid_argument("Output chroma line pointer must not be null");
  }

  out_chroma_mv->assign(carrier_phases_rad.size(), MillivoltsToSampleFixed(0.0));
  if (carrier_phases_rad.empty()) {
    return;
  }

  const bool fast_quarter_wave =
      carrier_phases_rad.size() < 2 || IsQuarterWaveStep(carrier_phases_rad[1] - carrier_phases_rad[0]);

  if (!fast_quarter_wave) {
    for (std::size_t index = 0; index < carrier_phases_rad.size(); ++index) {
      const double phase = carrier_phases_rad[index];
      (*out_chroma_mv)[index] = MillivoltsToSampleFixed(
          kCompositeChromaScaleMillivolts *
          ((axis_sin[index] * std::sin(phase)) + (axis_cos[index] * std::cos(phase))));
    }
    return;
  }

  double sin_phase = std::sin(carrier_phases_rad.front());
  double cos_phase = std::cos(carrier_phases_rad.front());
  for (std::size_t index = 0; index < carrier_phases_rad.size(); ++index) {
    (*out_chroma_mv)[index] = MillivoltsToSampleFixed(
      kCompositeChromaScaleMillivolts *
      ((axis_sin[index] * sin_phase) + (axis_cos[index] * cos_phase)));

    const double next_sin = cos_phase;
    const double next_cos = -sin_phase;
    sin_phase = next_sin;
    cos_phase = next_cos;
  }
}

void ValidateLineArguments(const std::vector<YCbCr444Pixel>& source_samples,
                           const std::vector<double>& carrier_phases_rad,
                           std::vector<SampleFixed>* out_chroma_mv) {
  if (out_chroma_mv == nullptr) {
    throw std::invalid_argument("Output chroma line pointer must not be null");
  }
  if (source_samples.size() != carrier_phases_rad.size()) {
    throw std::invalid_argument("Source samples and carrier phases must have the same length");
  }
}

void ExtractCbAxis(const std::vector<YCbCr444Pixel>& source_samples,
                   std::vector<double>* axis) {
  if (axis == nullptr) {
    throw std::invalid_argument("Cb axis output pointer must not be null");
  }

  axis->resize(source_samples.size());
  for (std::size_t index = 0; index < source_samples.size(); ++index) {
    (*axis)[index] = NormalizedCb(source_samples[index]);
  }
}

void ExtractCrAxis(const std::vector<YCbCr444Pixel>& source_samples,
                   std::vector<double>* axis) {
  if (axis == nullptr) {
    throw std::invalid_argument("Cr axis output pointer must not be null");
  }

  axis->resize(source_samples.size());
  for (std::size_t index = 0; index < source_samples.size(); ++index) {
    (*axis)[index] = NormalizedCr(source_samples[index]);
  }
}

}  // namespace

PalChromaEncoder::PalChromaEncoder(double sample_rate_hz)
    : u_filter_taps_(DesignLowPassKernel(kPalUvCutoffHz, sample_rate_hz, kPalFilterTaps)),
      v_filter_taps_(DesignLowPassKernel(kPalUvCutoffHz, sample_rate_hz, kPalFilterTaps)) {}

void PalChromaEncoder::EncodeLine(const std::vector<YCbCr444Pixel>& source_samples,
                                  const std::vector<double>& carrier_phases_rad,
                                  std::vector<SampleFixed>* out_chroma_mv) const {
  ValidateLineArguments(source_samples, carrier_phases_rad, out_chroma_mv);

  ExtractCbAxis(source_samples, &cb_axis_workspace_);
  ExtractCrAxis(source_samples, &cr_axis_workspace_);
  ApplyFirFilter(cb_axis_workspace_, u_filter_taps_, &filtered_u_workspace_, &fir_pad_workspace_);
  ApplyFirFilter(cr_axis_workspace_, v_filter_taps_, &filtered_v_workspace_, &fir_pad_workspace_);

  // The high-level design requires PAL chroma bandlimiting before quadrature
  // modulation. This encoder therefore low-passes the two colour-difference axes
  // symmetrically to roughly 1.3 MHz, then modulates them using the same carrier
  // phase sequence that drives burst generation. The timing model owns the line-
  // by-line PAL phase alternation, matching the composite split described by
  // ITU-R BT.470-6 Table 2 item 2.12 and ITU-R BT.1700 Annex 1 Part B.
  // ITU-R BT.1700 Annex 1 Part B Table 1 item 10d defines PAL chroma as
  // E'U * sin(wt) + E'V * cos(wt), with line-sequence V-sign handling owned
  // by the timing model path in generation_stage.
  ModulateQuadrature(filtered_u_workspace_, filtered_v_workspace_, carrier_phases_rad, out_chroma_mv);
}

NtscChromaEncoder::NtscChromaEncoder(double sample_rate_hz)
    : cb_filter_taps_(DesignLowPassKernel(kNtscCbCrCutoffHz, sample_rate_hz, kNtscFilterTaps)),
      cr_filter_taps_(DesignLowPassKernel(kNtscCbCrCutoffHz, sample_rate_hz, kNtscFilterTaps)) {}

void NtscChromaEncoder::EncodeLine(const std::vector<YCbCr444Pixel>& source_samples,
                                   const std::vector<double>& carrier_phases_rad,
                                   std::vector<SampleFixed>* out_chroma_mv) const {
  ValidateLineArguments(source_samples, carrier_phases_rad, out_chroma_mv);

  ExtractCbAxis(source_samples, &cb_axis_workspace_);
  ExtractCrAxis(source_samples, &cr_axis_workspace_);
  ApplyFirFilter(cb_axis_workspace_, cb_filter_taps_, &filtered_cb_workspace_, &fir_pad_workspace_);
  ApplyFirFilter(cr_axis_workspace_, cr_filter_taps_, &filtered_cr_workspace_, &fir_pad_workspace_);

  // NTSC chroma is synthesized directly on the two colour-difference axes so
  // bar-pattern saturation stays visually uniform in the composite envelope.
  ModulateQuadrature(filtered_cb_workspace_, filtered_cr_workspace_, carrier_phases_rad, out_chroma_mv);
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