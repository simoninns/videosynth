/*
 * File:        biphase_utils.h
 * Module:      biphase_utils
 * Purpose:     Hex parsing and validation utilities for LaserDisc biphase codes.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>

namespace videosynth {

// Thread-safety: All functions in this module are thread-safe (stateless pure
// functions). They may be called concurrently from multiple threads.

// Parses a 24-bit biphase hex code string (e.g. "88FFFF" or "0x88FFFF").
// Returns the 24-bit code value, or std::nullopt if the string is not a valid
// 6-digit hex code.
inline std::optional<uint32_t> ParseBiphaseHexCode(const std::string& hex) {
  std::string cleaned = hex;
  if (cleaned.size() >= 2 && cleaned[0] == '0' &&
      (cleaned[1] == 'x' || cleaned[1] == 'X')) {
    cleaned = cleaned.substr(2);
  }
  if (cleaned.size() != 6) {
    return std::nullopt;
  }
  for (char c : cleaned) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) {
      return std::nullopt;
    }
  }
  return static_cast<uint32_t>(std::stoul(cleaned, nullptr, 16));
}

// Returns true if the 24-bit biphase code has a valid key nibble.
// Per IEC 60856/60857, the key nibble is the top 4 bits of the 24-bit code
// and must begin with logic '1' (i.e., bit 23 must be set).
inline bool IsValidBiphaseKeyNibble(uint32_t code_24bit) {
  return (code_24bit & 0x800000u) != 0u;
}

// Returns true if the given hex string is a syntactically valid biphase code
// with a valid key nibble.
inline bool IsValidBiphaseHexCode(const std::string& hex) {
  const auto code = ParseBiphaseHexCode(hex);
  return code.has_value() && IsValidBiphaseKeyNibble(*code);
}

}  // namespace videosynth
