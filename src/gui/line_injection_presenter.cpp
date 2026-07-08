/*
 * File:        line_injection_presenter.cpp
 * Module:      gui
 * Purpose:     Widget-free catalogues and helpers for the line-injection and
 *              disc-skip editors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "line_injection_presenter.h"

#include <cctype>
#include <cstddef>

#include "videosynth/vits_definition.h"
#include "videosynth/vits_definition_provider.h"

namespace videosynth::gui {

std::vector<std::string> AvailableInjectionTypes() {
  return {"vits", "laserdisc"};
}

std::vector<std::string> AvailableVitsTypes(Standard standard) {
  const VitsDefinitionProvider provider;
  return provider.ListVitsTypes(standard);
}

int RecommendedVitsLine(Standard standard, const std::string& vits_type) {
  const VitsDefinitionProvider provider;
  VitsDefinition definition;
  std::string error;
  if (!provider.TryGetDefinition(standard, vits_type, &definition, &error)) {
    return 0;
  }
  return definition.recommended_frame_line;
}

std::vector<std::string> AvailableDiscTypes() { return {"CAV", "CLV"}; }

std::vector<std::string> AvailableLaserdiscCodeTypes(DiscType disc_type,
                                                     SectionType section_type,
                                                     Standard standard) {
  const bool system_m =
      standard == Standard::kNtsc || standard == Standard::kPalM;

  std::vector<std::string> code_types;
  for (const std::string& code_type : AllLaserdiscCodeTypes()) {
    if (disc_type == DiscType::kCAV && !IsValidCavCodeType(code_type)) {
      continue;
    }
    if (disc_type == DiscType::kCLV && !IsValidClvCodeType(code_type)) {
      continue;
    }
    if (!IsCodeTypeValidForSectionType(code_type, section_type)) {
      continue;
    }
    if (IsSystemMOnlyCodeType(code_type) && !system_m) {
      continue;
    }
    code_types.push_back(code_type);
  }
  return code_types;
}

bool CodeTypeUsesStartValue(const std::string& code_type) {
  // The validator range-checks start_value for exactly these two counters.
  return code_type == "picture_number" || code_type == "fm_picture_number";
}

bool CodeTypeUsesChapter(const std::string& code_type) {
  return code_type == "chapter_number";
}

bool CodeTypeUsesProgrammeStatus(const std::string& code_type) {
  return code_type == "programme_status";
}

bool CodeTypeUsesUsersCode(const std::string& code_type) {
  return code_type == "users_code";
}

bool ParseTargetLines(const std::string& text, std::vector<int>* out_lines) {
  out_lines->clear();

  std::size_t position = 0;
  while (position < text.size()) {
    // Skip separators (commas and whitespace).
    while (position < text.size() &&
           (text[position] == ',' ||
            std::isspace(static_cast<unsigned char>(text[position])) != 0)) {
      ++position;
    }
    if (position >= text.size()) {
      break;
    }

    int value = 0;
    bool any_digit = false;
    while (position < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[position])) != 0) {
      value = value * 10 + (text[position] - '0');
      any_digit = true;
      ++position;
    }

    const bool at_separator =
        position >= text.size() || text[position] == ',' ||
        std::isspace(static_cast<unsigned char>(text[position])) != 0;
    if (!any_digit || !at_separator) {
      return false;
    }
    out_lines->push_back(value);
  }
  return true;
}

std::string FormatTargetLines(const std::vector<int>& lines) {
  std::string text;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      text += ", ";
    }
    text += std::to_string(lines[i]);
  }
  return text;
}

int TotalDiscFrames(const Project& project) {
  int total = 0;
  for (const Section& section : project.sections) {
    total += section.duration_frames;
  }
  return total;
}

}  // namespace videosynth::gui
