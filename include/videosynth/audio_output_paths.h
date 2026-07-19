/*
 * File:        audio_output_paths.h
 * Module:      audio_output_paths
 * Purpose:     Derives the per-channel-pair audio output paths that sit beside
 *              the generated CVBS output.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <string_view>

namespace videosynth {

// Thread-safety: the functions in this module are thread-safe. They are
// stateless and operate only on their arguments.

// Derives the audio track path for `channel_pair` from a CVBS output path:
// strips a trailing ".composite" or ".y" suffix (if present) and appends
// "_audio_<pair><extension>", where `extension` includes its leading dot.
inline std::string DeriveAudioTrackPath(const std::string& video_path,
                                        int channel_pair,
                                        std::string_view extension) {
  constexpr std::string_view kCompositeSuffix = ".composite";
  constexpr std::string_view kLumaSuffix = ".y";

  std::string base = video_path;
  auto ends_with = [&](std::string_view suffix) {
    return base.size() >= suffix.size() &&
           base.compare(base.size() - suffix.size(), suffix.size(), suffix) ==
               0;
  };

  if (ends_with(kCompositeSuffix)) {
    base.resize(base.size() - kCompositeSuffix.size());
  } else if (ends_with(kLumaSuffix)) {
    base.resize(base.size() - kLumaSuffix.size());
  }
  return base + "_audio_" + std::to_string(channel_pair) +
         std::string(extension);
}

}  // namespace videosynth
