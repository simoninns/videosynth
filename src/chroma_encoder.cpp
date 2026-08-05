/*
 * File:        chroma_encoder.cpp
 * Module:      chroma_encoder
 * Purpose:     Encodes fixed-format frame chroma into PAL or NTSC composite
 * subcarrier samples.
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
// SMPTE 170M-2004 Annex A eqs (4)/(5)/(10): NTSC encodes b-y onto the sin
// axis and r-y onto the cos axis, each scaled by 0.925 × reduction_factor.
// BT.601 normalises Cb as (B-Y)/(2*(1-Kb)) and Cr as (R-Y)/(2*(1-Kr)) into
// ±448 10-bit codes, so cb_norm = (B-Y)/0.886 and cr_norm = (R-Y)/0.701.
// Applying the NTSC reduction and 1000/140 mV-per-IRE conversion gives
// separate per-axis peak scales for cb_norm and cr_norm values in [−1, 1].
//   SMPTE 170M-2004 §A.3: b-y_factor = 0.492111, r-y_factor = 0.877283.
//   SMPTE 170M-2004 §A.5: overall luma+chroma scale factor = 0.925.
//   SMPTE 170M-2004 §15.4: 140 IRE ≡ 1 V → 1 IRE = 1000/140 mV.
constexpr double kNtscCbScaleMv =
    0.925 * 0.492111 * 0.886 * 100.0 * (1000.0 / 140.0);
constexpr double kNtscCrScaleMv =
    0.925 * 0.877283 * 0.701 * 100.0 * (1000.0 / 140.0);

// ITU-R BT.1700 Part B Table 1 items 8/9: PAL encodes E'_U onto the sin axis
// and E'_V onto the cos axis, where E'_U = 0.493*(B-Y) and E'_V = 0.877*(R-Y).
// PAL white level is 700 mV, so the peak chroma scale per cb_norm unit is:
//   U: 0.493 × (1-Kb) × 700 = 0.493 × 0.886 × 700 mV
//   V: 0.877 × (1-Kr) × 700 = 0.877 × 0.701 × 700 mV
constexpr double kPalUScaleMv = 0.493 * 0.886 * 700.0;
constexpr double kPalVScaleMv = 0.877 * 0.701 * 700.0;

// ITU-R BT.470-6 Table 2 (M/PAL): PAL-M uses the same PAL colour-difference
// equations (E'_U = 0.493*(B-Y), E'_V = 0.877*(R-Y)) but System M white level
// of 714.3 mV (100 IRE; 140 IRE = 1 V).
//   U: 0.493 × (1-Kb) × 714.3 = 0.493 × 0.886 × 714.3 mV
//   V: 0.877 × (1-Kr) × 714.3 = 0.877 × 0.701 × 714.3 mV
constexpr double kPalMUScaleMv = 0.493 * 0.886 * 714.3;
constexpr double kPalMVScaleMv = 0.877 * 0.701 * 714.3;

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

std::vector<double> DesignLowPassKernel(double cutoff_hz, double sample_rate_hz,
                                        int taps) {
  if (sample_rate_hz <= 0.0 || cutoff_hz <= 0.0 ||
      cutoff_hz >= (sample_rate_hz / 2.0) || taps < 3 || (taps % 2) == 0) {
    throw std::invalid_argument("Invalid low-pass filter design parameters");
  }

  const double normalized_cutoff = cutoff_hz / sample_rate_hz;
  const int half_width = taps / 2;
  std::vector<double> kernel(static_cast<std::size_t>(taps), 0.0);
  double gain_sum = 0.0;
  for (int tap = 0; tap < taps; ++tap) {
    const int offset = tap - half_width;
    const double sinc =
        (offset == 0)
            ? (2.0 * normalized_cutoff)
            : std::sin(2.0 * kPi * normalized_cutoff * offset) / (kPi * offset);
    const double window =
        0.54 - (0.46 * std::cos((2.0 * kPi * static_cast<double>(tap)) /
                                static_cast<double>(taps - 1)));
    kernel[static_cast<std::size_t>(tap)] = sinc * window;
    gain_sum += kernel[static_cast<std::size_t>(tap)];
  }

  for (double& tap : kernel) {
    tap /= gain_sum;
  }
  return kernel;
}

std::vector<std::int64_t> QuantizeKernelToFixed(
    const std::vector<double>& kernel) {
  std::vector<std::int64_t> fixed(kernel.size(), 0);
  for (std::size_t i = 0; i < kernel.size(); ++i) {
    fixed[i] = static_cast<std::int64_t>(
        std::llround(kernel[i] * static_cast<double>(kFirCoeffScale)));
  }
  return fixed;
}

void ExtractCbAxisFixed(const std::vector<YCbCr444Pixel>& source_samples,
                        std::vector<std::int64_t>* axis_fixed) {
  axis_fixed->resize(source_samples.size());
  for (std::size_t index = 0; index < source_samples.size(); ++index) {
    const int cb = ClampCode(source_samples[index].cb, 64, 960);
    (*axis_fixed)[index] =
        static_cast<std::int64_t>(cb - 512) * kChromaAxisScale / 448;
  }
}

void ExtractCrAxisFixed(const std::vector<YCbCr444Pixel>& source_samples,
                        std::vector<std::int64_t>* axis_fixed) {
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
  if (input.empty()) {
    output->clear();
    padded_workspace->clear();
    return;
  }

  const int kernel_size = static_cast<int>(kernel.size());
  const int half_width = kernel_size / 2;
  padded_workspace->resize(input.size() +
                           static_cast<std::size_t>(2 * half_width));
  std::fill_n(padded_workspace->begin(), half_width, input.front());
  std::copy(input.begin(), input.end(), padded_workspace->begin() + half_width);
  std::fill(padded_workspace->begin() + half_width +
                static_cast<std::ptrdiff_t>(input.size()),
            padded_workspace->end(), input.back());

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
                                      double sin_scale_mv, double cos_scale_mv,
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
    (*out_chroma_mv)[index] =
        MillivoltsToSampleFixed((axis_sin[index] * sin_scale_mv * sin_phase) +
                                (axis_cos[index] * cos_scale_mv * cos_phase));

    const double next_sin = cos_phase;
    const double next_cos = -sin_phase;
    sin_phase = next_sin;
    cos_phase = next_cos;
  }
}

}  // namespace

QuadratureChromaEncoder::QuadratureChromaEncoder(double sample_rate_hz,
                                                 double cutoff_hz, int taps,
                                                 double sin_scale_mv,
                                                 double cos_scale_mv)
    // Both colour-difference axes are filtered by an identically designed
    // low-pass kernel, so it is designed and quantised once and shared.
    : filter_taps_fixed_(QuantizeKernelToFixed(
          DesignLowPassKernel(cutoff_hz, sample_rate_hz, taps))),
      sin_scale_mv_(sin_scale_mv),
      cos_scale_mv_(cos_scale_mv) {}

void QuadratureChromaEncoder::EncodeLineFromPhaseStart(
    const std::vector<YCbCr444Pixel>& source_samples,
    double carrier_phase_start_rad,
    std::vector<SampleFixed>* out_chroma_mv) const {
  if (out_chroma_mv == nullptr) {
    throw std::invalid_argument("Output chroma line pointer must not be null");
  }

  ExtractCbAxisFixed(source_samples, &sin_axis_fixed_);
  ExtractCrAxisFixed(source_samples, &cos_axis_fixed_);
  ApplyFirFilterFixed(sin_axis_fixed_, filter_taps_fixed_, &filtered_sin_fixed_,
                      &fir_pad_fixed_);
  ApplyFirFilterFixed(cos_axis_fixed_, filter_taps_fixed_, &filtered_cos_fixed_,
                      &fir_pad_fixed_);

  filtered_sin_workspace_.resize(filtered_sin_fixed_.size());
  filtered_cos_workspace_.resize(filtered_cos_fixed_.size());
  for (std::size_t i = 0; i < filtered_sin_fixed_.size(); ++i) {
    filtered_sin_workspace_[i] = static_cast<double>(filtered_sin_fixed_[i]) /
                                 static_cast<double>(kChromaAxisScale);
    filtered_cos_workspace_[i] = static_cast<double>(filtered_cos_fixed_[i]) /
                                 static_cast<double>(kChromaAxisScale);
  }

  ModulateQuadratureFromPhaseStart(
      filtered_sin_workspace_, filtered_cos_workspace_, sin_scale_mv_,
      cos_scale_mv_, carrier_phase_start_rad, out_chroma_mv);
}

// ITU-R BT.1700 Part B Table 1 item 10d: PAL U on sin, V on cos.
PalChromaEncoder::PalChromaEncoder(double sample_rate_hz)
    : QuadratureChromaEncoder(sample_rate_hz, kPalUvCutoffHz, kPalFilterTaps,
                              kPalUScaleMv, kPalVScaleMv) {}

// SMPTE 170M-2004 §A.5 eq (10): NTSC b-y on sin axis, r-y on cos axis.
NtscChromaEncoder::NtscChromaEncoder(double sample_rate_hz)
    : QuadratureChromaEncoder(sample_rate_hz, kNtscCbCrCutoffHz,
                              kNtscFilterTaps, kNtscCbScaleMv, kNtscCrScaleMv) {
}

// ITU-R BT.470-6 Table 2 (M/PAL): same as 625-line PAL — U on sin, V on cos —
// but scaled to System M white level (714.3 mV) per kPalMUScaleMv.
PalMChromaEncoder::PalMChromaEncoder(double sample_rate_hz)
    : QuadratureChromaEncoder(sample_rate_hz, kPalUvCutoffHz, kPalFilterTaps,
                              kPalMUScaleMv, kPalMVScaleMv) {}

std::unique_ptr<IChromaEncoder> CreateChromaEncoder(Standard standard,
                                                    double sample_rate_hz) {
  if (standard == Standard::kPal) {
    return std::make_unique<PalChromaEncoder>(sample_rate_hz);
  }
  if (standard == Standard::kNtsc) {
    return std::make_unique<NtscChromaEncoder>(sample_rate_hz);
  }
  if (standard == Standard::kPalM) {
    return std::make_unique<PalMChromaEncoder>(sample_rate_hz);
  }
  return nullptr;
}

}  // namespace videosynth