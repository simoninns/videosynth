/*
 * File:        project_templates.cpp
 * Module:      gui
 * Purpose:     Built-in project templates for File > New
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "project_templates.h"

namespace videosynth::gui {

Project MakeDefaultPalProject() {
  Project project;
  project.name = "New Project";
  project.version = "1.0";
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  project.output.video_path = "output/new_project.composite";
  project.output.metadata_path = "output/new_project.meta";

  Section section;
  section.name = "Section 1";
  section.type = "progressive";
  // Placeholder source; existence is only checked when generation probes the
  // file, so a fresh template still validates structurally.
  section.source = "assets/source.exr";
  section.duration_frames = 25;
  project.sections.push_back(section);

  return project;
}

}  // namespace videosynth::gui
