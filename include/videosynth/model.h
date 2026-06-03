/*
 * File:        model.h
 * Module:      model
 * Purpose:     Defines project data models and video-standard enums.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace videosynth {

// Thread-safety: All types, enums, and functions in this module are thread-safe.
// They are plain data containers or stateless functions. May be accessed
// concurrently from multiple threads.
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
  bool pal_laserdisc_pilot_burst = false;
  bool ntsc_laserdisc_vbi_burst = false;
  double ntsc_black_setup_ire = 7.5;
  bool ntsc_black_setup_ire_specified = false;
};

inline bool IsSupportedNtscBlackSetupIre(double setup_ire) {
  constexpr double kEpsilon = 1e-9;
  return std::abs(setup_ire - 7.5) < kEpsilon || std::abs(setup_ire) < kEpsilon;
}

inline bool Is4fscSampleEncodingPreset(const std::string& preset) {
  return preset == "CVBS_U10_4FSC" || preset == "CVBS_U16_4FSC" ||
         preset == "CVBS_TPG21_4FSC";
}

inline bool IsSupportedSampleEncodingPreset(const std::string& preset) {
  return Is4fscSampleEncodingPreset(preset) || preset == "RAW_S16_28M" ||
         preset == "RAW_S16_40M";
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
  struct LineInjectionCode {
    std::string code_type;
    int start_value = 0;
    bool start_value_specified = false;
    int chapter = 0;
    bool chapter_specified = false;
    std::string programme_status;
    bool programme_status_specified = false;
  };

  struct LineInjection {
    std::string type;
    std::vector<int> target_lines;
    std::string vits_type;
    std::string disc_type;
    std::vector<LineInjectionCode> codes;
  };

  std::string name;
  std::string type;
  std::vector<LineInjection> line_injections;
  std::string source;
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
