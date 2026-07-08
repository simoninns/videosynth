/*
 * File:        project_settings_presenter.cpp
 * Module:      gui
 * Purpose:     Widget-free mapping layer for the project settings editor
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "project_settings_presenter.h"

#include <utility>

namespace videosynth::gui {

namespace {

bool EndsWith(const std::string& value, const std::string& suffix) {
  if (suffix.size() > value.size()) {
    return false;
  }
  return value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
         0;
}

// Strips a known output suffix (".composite" or ".y"), else any final
// extension within the last path component, returning the path stem.
std::string StripOutputSuffix(const std::string& path) {
  if (EndsWith(path, ".composite")) {
    return path.substr(0, path.size() - std::string(".composite").size());
  }
  if (EndsWith(path, ".y")) {
    return path.substr(0, path.size() - std::string(".y").size());
  }

  const std::size_t last_separator = path.find_last_of('/');
  const std::size_t last_dot = path.find_last_of('.');
  const bool dot_in_last_component =
      last_dot != std::string::npos &&
      (last_separator == std::string::npos || last_dot > last_separator);
  if (dot_in_last_component) {
    return path.substr(0, last_dot);
  }
  return path;
}

}  // namespace

ProjectSettingsFormState BuildProjectSettingsFormState(const Project& project) {
  ProjectSettingsFormState state;

  state.standard_options = {"PAL", "NTSC", "PAL_M"};
  // The supported preset set from model.h (IsSupportedSampleEncodingPreset).
  state.sample_encoding_options = {"CVBS_U10_4FSC",   "CVBS_U16_4FSC",
                                   "CVBS_TPG21_4FSC", "CVBS_S16_FSC",
                                   "RAW_S16_28M",     "RAW_S16_40M"};
  // ProjectValidator only accepts the locked TBC state.
  state.signal_state_options = {"STANDARD_TBC_LOCKED"};
  state.signal_type_options = {"composite", "yc"};
  // ProjectValidator: ntsc_black_setup_ire must be 7.5 or 0.0.
  state.ntsc_black_setup_ire_options = {7.5, 0.0};

  const Standard standard = project.cvbs_presets.video_standard_preset;
  state.pilot_burst_editable = standard == Standard::kPal;
  state.vbi_burst_editable = standard == Standard::kNtsc;
  state.setup_ire_editable =
      standard == Standard::kNtsc || standard == Standard::kPalM;
  state.video_path_requires_y_suffix = project.output.signal_type == "yc";

  return state;
}

CvbsPresets NormalizeCvbsPresetsForStandard(CvbsPresets presets) {
  if (presets.video_standard_preset != Standard::kPal) {
    presets.pal_laserdisc_pilot_burst = false;
  }
  if (presets.video_standard_preset != Standard::kNtsc) {
    presets.ntsc_laserdisc_vbi_burst = false;
  }
  if (presets.video_standard_preset != Standard::kNtsc &&
      presets.video_standard_preset != Standard::kPalM) {
    presets.ntsc_black_setup_ire_specified = false;
    presets.ntsc_black_setup_ire = 7.5;
  }
  return presets;
}

std::string DeriveMetadataPath(const std::string& video_path) {
  if (video_path.empty()) {
    return {};
  }
  return StripOutputSuffix(video_path) + ".meta";
}

std::string EnforceSignalTypeVideoPath(std::string video_path,
                                       const std::string& signal_type) {
  if (video_path.empty()) {
    return video_path;
  }

  if (signal_type == "yc") {
    if (EndsWith(video_path, ".y")) {
      return video_path;
    }
    return StripOutputSuffix(video_path) + ".y";
  }

  if (signal_type == "composite" && EndsWith(video_path, ".y")) {
    return StripOutputSuffix(std::move(video_path)) + ".composite";
  }
  return video_path;
}

}  // namespace videosynth::gui
