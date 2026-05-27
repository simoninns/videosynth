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
  Standard standard = Standard::kUnknown;
  std::string sample_rate;
  bool subcarrier_lock = false;
};

struct Section {
  std::string name;
  std::string type;
};

struct Project {
  std::string name;
  std::string version;
  CvbsPresets cvbs_presets;
  std::vector<Section> sections;
};

}  // namespace videosynth
