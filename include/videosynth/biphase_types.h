/*
 * File:        biphase_types.h
 * Module:      biphase_types
 * Purpose:     Defines enumerated types for LaserDisc biphase injection.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>

namespace videosynth {

// Thread-safety: All types and functions in this module are thread-safe.
// They are stateless pure functions and plain enumerations.

// LaserDisc disc section types per IEC 60856 (PAL) / IEC 60857 (NTSC).
enum class SectionType {
  kLeadIn,
  kProgrammeArea,
  kLeadOut,
  kUnknown,
};

inline SectionType SectionTypeFromString(const std::string& value) {
  if (value == "lead_in") {
    return SectionType::kLeadIn;
  }
  if (value == "programme_area") {
    return SectionType::kProgrammeArea;
  }
  if (value == "lead_out") {
    return SectionType::kLeadOut;
  }
  return SectionType::kUnknown;
}

inline std::string SectionTypeToString(SectionType section_type) {
  switch (section_type) {
    case SectionType::kLeadIn:
      return "lead_in";
    case SectionType::kProgrammeArea:
      return "programme_area";
    case SectionType::kLeadOut:
      return "lead_out";
    default:
      return "unknown";
  }
}

// LaserDisc disc format types per IEC 60856 / IEC 60857.
enum class DiscType {
  kCAV,
  kCLV,
  kUnknown,
};

inline DiscType DiscTypeFromString(const std::string& value) {
  if (value == "CAV") {
    return DiscType::kCAV;
  }
  if (value == "CLV") {
    return DiscType::kCLV;
  }
  return DiscType::kUnknown;
}

inline std::string DiscTypeToString(DiscType disc_type) {
  switch (disc_type) {
    case DiscType::kCAV:
      return "CAV";
    case DiscType::kCLV:
      return "CLV";
    default:
      return "unknown";
  }
}

// Returns true if code_type is a valid biphase code type for CAV discs.
inline bool IsValidCavCodeType(const std::string& code_type) {
  return code_type == "lead_in" || code_type == "lead_out" ||
         code_type == "picture_number" || code_type == "picture_stop" ||
         code_type == "chapter_number" || code_type == "programme_status" ||
         code_type == "users_code" || code_type == "fm_picture_number" ||
         code_type == "fm_white_flag";
}

// Returns true if code_type is a valid biphase code type for CLV discs.
inline bool IsValidClvCodeType(const std::string& code_type) {
  return code_type == "lead_in" || code_type == "lead_out" ||
         code_type == "programme_time_code" || code_type == "clv_code" ||
         code_type == "clv_picture_number" || code_type == "chapter_number" ||
         code_type == "programme_status" || code_type == "users_code" ||
         code_type == "fm_programme_time" || code_type == "fm_white_flag";
}

// Returns true if code_type is a known laserdisc code type for any disc type.
inline bool IsKnownLaserdiscCodeType(const std::string& code_type) {
  return IsValidCavCodeType(code_type) || IsValidClvCodeType(code_type);
}

}  // namespace videosynth
