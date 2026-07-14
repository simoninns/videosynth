/*
 * File:        osd_token_resolver.cpp
 * Module:      osd
 * Purpose:     Resolves per-frame template tokens in OSD overlay text strings.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/osd_token_resolver.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>

#include "videosynth/biphase_injection_manager.h"

namespace videosynth {

namespace {

// NOLINTBEGIN(readability-magic-numbers)
std::string FormatPictureNumber(int picture_number) {
  if (picture_number <= 0) {
    return "-----";
  }
  // Buffer sized for sign + 10 digits + NUL; the 5-digit zero-pad is the
  // normal case (max valid picture number is 99999) but we don't truncate.
  char buf[12];
  std::snprintf(buf, sizeof(buf), "%05d", picture_number);
  return std::string(buf);
}

std::string FormatBiphaseHex(const std::vector<uint32_t>& words) {
  if (words.empty()) {
    return "--------";
  }
  std::string result;
  bool first = true;
  for (uint32_t word : words) {
    if (!first) {
      result += ' ';
    }
    first = false;
    char buf[7];
    std::snprintf(buf, sizeof(buf), "%06X", word);
    result += buf;
  }
  return result;
}

std::string FormatClvTimecode(const PerFrameContext& ctx) {
  if (!ctx.has_clv_timecode) {
    return "--:--:--:--";
  }
  // HH:MM:SS:FF; buffer sized for four zero-padded fields plus separators.
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%02d", ctx.clv_hours,
                ctx.clv_minutes, ctx.clv_seconds, ctx.clv_frames);
  return std::string(buf);
}
// NOLINTEND(readability-magic-numbers)

const std::set<std::string>& KnownTokens() {
  static const std::set<std::string> kTokens = {
      "picture_number", "biphase_hex", "phase_id",
      "section_name",   "timecode",    "frame_number"};
  return kTokens;
}

}  // namespace

std::string OsdTokenResolver::Resolve(const std::string& text,
                                      const PerFrameContext& ctx,
                                      const std::string& section_name) const {
  std::string result;
  result.reserve(text.size());

  for (std::size_t i = 0; i < text.size();) {
    if (text[i] != '{') {
      result += text[i];
      ++i;
      continue;
    }
    const std::size_t close = text.find('}', i);
    if (close == std::string::npos) {
      result += text[i];
      ++i;
      continue;
    }
    const std::string token = text.substr(i + 1, close - i - 1);
    if (token == "picture_number") {
      result += FormatPictureNumber(ctx.picture_number);
    } else if (token == "biphase_hex") {
      result += FormatBiphaseHex(ctx.biphase_words);
    } else if (token == "phase_id") {
      result += std::to_string(ctx.colour_frame_index);
    } else if (token == "section_name") {
      result += section_name;
    } else if (token == "timecode") {
      result += FormatClvTimecode(ctx);
    } else if (token == "frame_number") {
      result += std::to_string(ctx.frame_number);
    } else {
      result += text.substr(i, close - i + 1);
    }
    i = close + 1;
  }

  return result;
}

bool OsdTokenResolver::HasOnlyKnownTokens(const std::string& text,
                                          std::string* unknown_token) {
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] != '{') {
      ++i;
      continue;
    }
    const std::size_t close = text.find('}', i);
    if (close == std::string::npos) {
      ++i;
      continue;
    }
    const std::string token = text.substr(i + 1, close - i - 1);
    if (!token.empty() && KnownTokens().find(token) == KnownTokens().end()) {
      if (unknown_token != nullptr) {
        *unknown_token = token;
      }
      return false;
    }
    i = close + 1;
  }
  return true;
}

}  // namespace videosynth
