/*
 * File:        output_stage.cpp
 * Module:      output_stage
 * Purpose:     Writes generated sample buffers to output and metadata files.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/output_stage.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>

#include "videosynth/model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

namespace {

struct QuantizationProfile {
  double millivolts_per_code = 1.0;
  int blanking_code = 0;
  int minimum_legal_code = 0;
  int maximum_legal_code = 1023;
};

bool BuildQuantizationProfile(Standard standard, QuantizationProfile* profile) {
  if (profile == nullptr) {
    return false;
  }

  if (standard == Standard::kPal) {
    *profile = QuantizationProfile{
        .millivolts_per_code = 1.1905,
        .blanking_code = 256,
        .minimum_legal_code = 4,
        .maximum_legal_code = 1019,
    };
    return true;
  }

  if (standard == Standard::kNtsc) {
    *profile = QuantizationProfile{
        .millivolts_per_code = 1.2755,
        .blanking_code = 240,
        .minimum_legal_code = 16,
        .maximum_legal_code = 1019,
    };
    return true;
  }

  return false;
}

int QuantizeCompositeMillivolts(double composite_mv, const QuantizationProfile& profile) {
  const int mapped =
      static_cast<int>(std::lround(composite_mv / profile.millivolts_per_code)) +
      profile.blanking_code;
  if (mapped < profile.minimum_legal_code) {
    return profile.minimum_legal_code;
  }
  if (mapped > profile.maximum_legal_code) {
    return profile.maximum_legal_code;
  }
  return mapped;
}

}  // namespace

bool OutputStage::Write(const Project& project,
                        const std::vector<double>& y_mv,
                        const std::vector<double>& c_mv,
                        const std::string& output_path,
                        const std::string& metadata_path,
                        std::vector<std::string>* errors) {
  if (errors == nullptr) {
    return false;
  }

  if (output_path.empty()) {
    errors->push_back("Output path must not be empty.");
    return false;
  }

  if (metadata_path.empty()) {
    errors->push_back("Metadata path must not be empty.");
    return false;
  }

  if (output_path == metadata_path) {
    errors->push_back("Output and metadata paths must be different files.");
    return false;
  }

  if (project.cvbs_presets.sample_rate != "4fsc") {
    errors->push_back("Output stage requires sample_rate='4fsc'.");
    return false;
  }

  if (!project.cvbs_presets.subcarrier_lock) {
    errors->push_back("Output stage requires subcarrier_lock=true for 4fsc output.");
    return false;
  }

  QuantizationProfile quantization;
  if (!BuildQuantizationProfile(project.cvbs_presets.standard, &quantization)) {
    errors->push_back("Output stage received unsupported or unknown video standard.");
    return false;
  }

  if (y_mv.size() != c_mv.size()) {
    errors->push_back("Internal error: Y and C sample vectors must be same size.");
    return false;
  }

  const TimingConstants timing = GetTimingConstants(project.cvbs_presets.standard);
  const std::size_t frame_span =
      static_cast<std::size_t>(timing.lines_per_frame * timing.samples_per_line_4fsc);
  if (frame_span == 0U || (y_mv.size() % frame_span) != 0U) {
    errors->push_back("Generated sample count does not align to whole-frame 4fsc timing.");
    return false;
  }

  std::ofstream video_stream(output_path, std::ios::binary);
  if (!video_stream) {
    errors->push_back("Failed to open output video file: " + output_path);
    return false;
  }

  std::size_t clipped_low_count = 0;
  std::size_t clipped_high_count = 0;
  for (std::size_t i = 0; i < y_mv.size(); ++i) {
    const double composite_mv = y_mv[i] + c_mv[i];
    const int mapped =
        static_cast<int>(std::lround(composite_mv / quantization.millivolts_per_code)) +
        quantization.blanking_code;
    if (mapped < quantization.minimum_legal_code) {
      ++clipped_low_count;
    }
    if (mapped > quantization.maximum_legal_code) {
      ++clipped_high_count;
    }

    const std::int16_t composite_code = static_cast<std::int16_t>(
        QuantizeCompositeMillivolts(composite_mv, quantization));
    video_stream.write(reinterpret_cast<const char*>(&composite_code),
                       sizeof(composite_code));
  }

  std::ofstream metadata_stream(metadata_path);
  if (!metadata_stream) {
    errors->push_back("Failed to open metadata file: " + metadata_path);
    return false;
  }

  const std::size_t frame_count = y_mv.size() / frame_span;

  metadata_stream << "format=videosynth_cvbs\n";
  metadata_stream << "signal_type=composite\n";
  metadata_stream << "video_standard_preset="
                  << StandardToString(project.cvbs_presets.standard) << "\n";
  metadata_stream << "sample_encoding_preset=CVBS_U10_4FSC\n";
  metadata_stream << "signal_state_preset=STANDARD_TBC_LOCKED\n";
  metadata_stream << "sample_rate_mode=" << project.cvbs_presets.sample_rate << "\n";
  metadata_stream << "sample_rate_hz="
                  << static_cast<std::uint64_t>(std::llround(timing.sample_rate_4fsc_hz)) << "\n";
  metadata_stream << "subcarrier_lock="
                  << (project.cvbs_presets.subcarrier_lock ? "true" : "false") << "\n";
  metadata_stream << "lines_per_frame=" << timing.lines_per_frame << "\n";
  metadata_stream << "samples_per_line=" << timing.samples_per_line_4fsc << "\n";
  metadata_stream << "frame_count=" << frame_count << "\n";
  metadata_stream << "sample_count=" << y_mv.size() << "\n";
  metadata_stream << "legal_code_min=" << quantization.minimum_legal_code << "\n";
  metadata_stream << "legal_code_max=" << quantization.maximum_legal_code << "\n";
  metadata_stream << "clipped_low_samples=" << clipped_low_count << "\n";
  metadata_stream << "clipped_high_samples=" << clipped_high_count << "\n";
  metadata_stream << "composite_only=true\n";

  return true;
}

}  // namespace videosynth
