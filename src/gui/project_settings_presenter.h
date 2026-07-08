/*
 * File:        project_settings_presenter.h
 * Module:      gui
 * Purpose:     Widget-free mapping layer for the project settings editor
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>

#include "videosynth/model.h"

namespace videosynth::gui {

// Thread-safety: all functions in this module are thread-safe pure functions
// over their inputs; ProjectSettingsFormState is a plain data container.

// View state for the project settings form: the option catalogues plus the
// standard-dependent enablement flags. Enablement mirrors the rules
// ProjectValidator applies (pilot burst PAL-only, VBI burst NTSC-only, black
// setup IRE System-M-only) so invalid combinations cannot be entered.
struct ProjectSettingsFormState {
  std::vector<std::string> standard_options;
  std::vector<std::string> sample_encoding_options;
  std::vector<std::string> signal_state_options;
  std::vector<std::string> signal_type_options;
  std::vector<double> ntsc_black_setup_ire_options;

  // Standard-dependent enablement for the current project.
  bool pilot_burst_editable = false;
  bool vbi_burst_editable = false;
  bool setup_ire_editable = false;

  // True when signal_type is "yc" and video_path must carry a ".y" suffix.
  bool video_path_requires_y_suffix = false;
};

// Builds the form state (catalogues + enablement) for the given project.
ProjectSettingsFormState BuildProjectSettingsFormState(const Project& project);

// Clears preset flags the selected standard cannot carry, mirroring
// ProjectValidator: pilot burst outside PAL, VBI burst outside NTSC, and the
// black setup IRE override outside NTSC/PAL-M. Returns the adjusted presets.
CvbsPresets NormalizeCvbsPresetsForStandard(CvbsPresets presets);

// Derives the metadata path from a video path: strips a trailing
// ".composite" or ".y" (else any final extension) and appends ".meta".
// Returns an empty string for an empty video path.
std::string DeriveMetadataPath(const std::string& video_path);

// Adjusts a video path to the selected signal type: for "yc" the path must
// end in ".y" (a trailing ".composite" is rewritten, otherwise ".y" is
// appended); for "composite" a trailing ".y" is rewritten to ".composite".
std::string EnforceSignalTypeVideoPath(std::string video_path,
                                       const std::string& signal_type);

}  // namespace videosynth::gui
