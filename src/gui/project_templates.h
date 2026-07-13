/*
 * File:        project_templates.h
 * Module:      gui
 * Purpose:     Built-in project and section templates for File > New and the
 *              section list's typed add menu
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "videosynth/model.h"

namespace videosynth::gui {

// Thread-safety: all functions in this module are thread-safe pure functions.

// Returns a minimal project for `standard` that passes structural validation
// (ProjectValidator without a source probe): one progressive section with a
// placeholder source path the user is expected to replace. This is the
// starting point File > New / the New Project dialog hands to the editors.
Project MakeDefaultProject(Standard standard);

// Convenience wrapper for MakeDefaultProject(Standard::kPal).
Project MakeDefaultPalProject();

// Plain progressive section (no laserdisc VBI codes). `ordinal` seeds the
// default name ("Section <ordinal>").
Section MakeProgressiveSectionTemplate(int ordinal);

// Laserdisc section templates modelled on docs/examples/. Each carries a CAV
// laserdisc injection with the codes IEC 60856/60857 expects for its section
// type; NTSC and PAL-M additionally get the mandatory FM white flag and the
// virs VITS colour reference (IEC 60857 §9.1.3). Durations default to the
// IEC track-pitch minimums the validator checks (lead-in 938, lead-out 1250).
Section MakeLaserdiscLeadInSectionTemplate(Standard standard);
Section MakeLaserdiscProgrammeSectionTemplate(Standard standard);
Section MakeLaserdiscLeadOutSectionTemplate(Standard standard);

// Copy of `section` renamed "<name> (copy)" (then "(copy 2)", … if needed)
// so duplicates stay distinguishable in the section list.
Section MakeDuplicateSection(const Section& section,
                             const std::vector<Section>& existing_sections);

}  // namespace videosynth::gui
