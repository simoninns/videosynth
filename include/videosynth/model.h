/*
 * File:        model.h
 * Module:      model
 * Purpose:     Defines project data models and video-standard enums.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>

namespace videosynth {

enum class Standard {
  kPal,
  kNtsc,
  kUnknown,
};

inline Standard StandardFromString(const std::string& value) {
  if (value == "PAL") {
    return Standard::kPal;
  }
  if (value == "NTSC") {
    return Standard::kNtsc;
  }
  return Standard::kUnknown;
}

inline std::string StandardToString(Standard standard) {
  switch (standard) {
    case Standard::kPal:
      return "PAL";
    case Standard::kNtsc:
      return "NTSC";
    default:
      return "UNKNOWN";
  }
}

struct CvbsPresets {
  Standard video_standard_preset = Standard::kUnknown;
  std::string sample_encoding_preset = "CVBS_U10_4FSC";
  std::string signal_state_preset = "STANDARD_TBC_LOCKED";
};

inline bool Is4fscSampleEncodingPreset(const std::string& preset) {
  return preset == "CVBS_U10_4FSC" || preset == "CVBS_U16_4FSC" ||
         preset == "CVBS_TPG21_4FSC";
}

inline std::string SampleRateModeFromEncodingPreset(const std::string& preset) {
  if (Is4fscSampleEncodingPreset(preset)) {
    return "4fsc";
  }
  if (preset == "RAW_S16_28M") {
    return "28M";
  }
  if (preset == "RAW_S16_40M") {
    return "40M";
  }
  return "unknown";
}

inline bool IsLockedSignalStatePreset(const std::string& preset) {
  return preset == "STANDARD_TBC_LOCKED";
}

struct Section {
  std::string name;
  std::string type;
  std::string pattern;
  std::string source;
  std::string source_pixel_format;
  bool duration_frames_all = false;
  int duration_frames = 0;
  int start_frame = 0;
};

struct OutputTargets {
  std::string video_path;
  std::string metadata_path;
};

struct Project {
  std::string name;
  std::string version;
  CvbsPresets cvbs_presets;
  OutputTargets output;
  std::vector<Section> sections;
};

}  // namespace videosynth
