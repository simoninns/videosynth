/*
 * File:        project_validator.cpp
 * Module:      project_validator
 * Purpose:     Validates project constraints for supported generation profiles.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/project_validator.h"

#include <string>

namespace {

bool IsSupportedPattern(const std::string& pattern) {
  return pattern == "colour_bars_75" || pattern == "grayscale_ramp" ||
         pattern == "pluge_basic";
}

}  // namespace

namespace videosynth {

ValidationResult ProjectValidator::Validate(const Project& project) {
  ValidationResult result;
  result.is_valid = true;

  if (project.cvbs_presets.standard == Standard::kUnknown) {
    result.is_valid = false;
    result.errors.push_back("Standard must be 'PAL' or 'NTSC'.");
  }

  if (project.cvbs_presets.sample_rate != "4fsc") {
    result.is_valid = false;
    result.errors.push_back(
        "MVP constraint violation: sample_rate must be '4fsc'.");
  }

  if (!project.cvbs_presets.subcarrier_lock) {
    result.is_valid = false;
    result.errors.push_back(
        "subcarrier_lock can only be enabled for 4fsc sample rate.");
    result.errors.push_back("MVP constraint violation: subcarrier_lock must be true.");
  }

  if (project.sections.empty()) {
    result.is_valid = false;
    result.errors.push_back("Project must contain at least one section.");
  }

  for (const Section& section : project.sections) {
    if (section.type != "software_generated") {
      result.is_valid = false;
      result.errors.push_back(
          "MVP constraint violation: section type must be 'software_generated'.");
      break;
    }

    if (section.pattern.empty()) {
      result.is_valid = false;
      result.errors.push_back(
          "MVP constraint violation: software_generated sections must define a pattern.");
      break;
    }

    if (!IsSupportedPattern(section.pattern)) {
      result.is_valid = false;
      result.errors.push_back(
          "MVP constraint violation: unsupported pattern. Supported patterns are "
          "'colour_bars_75', 'grayscale_ramp', and 'pluge_basic'.");
      break;
    }
  }

  return result;
}

}  // namespace videosynth
