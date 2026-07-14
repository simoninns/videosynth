/*
 * File:        line_injection_presenter.h
 * Module:      gui
 * Purpose:     Widget-free catalogues and helpers for the line-injection and
 *              disc-skip editors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>

#include "videosynth/biphase_types.h"
#include "videosynth/model.h"

namespace videosynth::gui {

// Thread-safety: all functions in this module are thread-safe pure functions.

// Injection types offered by the editor. "vitc" and "line_content" parse but
// are rejected by the runtime as unimplemented, so only the generatable
// types are offered.
std::vector<std::string> AvailableInjectionTypes();

// Standard-filtered vits_type catalogue (VitsDefinitionProvider order).
std::vector<std::string> AvailableVitsTypes(Standard standard);

// Recommended (mandatory, per the validator's strict placement policy) frame
// line for a vits_type, or 0 when the type has no fixed placement.
int RecommendedVitsLine(Standard standard, const std::string& vits_type);

// Disc types offered for laserdisc injections.
std::vector<std::string> AvailableDiscTypes();

// Laserdisc code types valid for the disc type, section type, and standard,
// filtered through the same predicates ProjectValidator applies
// (IsValidCav/ClvCodeType, IsCodeTypeValidForSectionType,
// IsSystemMOnlyCodeType).
std::vector<std::string> AvailableLaserdiscCodeTypes(DiscType disc_type,
                                                     SectionType section_type,
                                                     Standard standard);

// The "expected" codes for a section — the subset of
// AvailableLaserdiscCodeTypes an editor should pre-tick so a section of this
// type starts with the codes it normally carries (lead-in → lead_in,
// CAV programme → picture_number + chapter_number, CLV programme →
// programme_time_code + clv_code + chapter_number, lead-out → lead_out, plus
// the System-M FM codes where applicable). Returned in AvailableLaserdiscCode-
// Types order.
std::vector<std::string> RecommendedLaserdiscCodeTypes(DiscType disc_type,
                                                       SectionType section_type,
                                                       Standard standard);

// True when a VITS type has a fixed placement line (the validator forbids any
// other line), so its target line is not user-editable.
bool VitsHasFixedLine(Standard standard, const std::string& vits_type);

// Default target lines when a VITS type is first selected: its fixed
// recommended line, or the conventional both-field virs lines (System-M
// 19/282) for the free-placement virs colour reference.
std::vector<int> DefaultVitsLines(Standard standard,
                                  const std::string& vits_type);

// Which optional parameter each code type carries (biphase-design.md §10).
bool CodeTypeUsesStartValue(const std::string& code_type);
bool CodeTypeUsesChapter(const std::string& code_type);
bool CodeTypeUsesProgrammeStatus(const std::string& code_type);
bool CodeTypeUsesUsersCode(const std::string& code_type);

// One-sentence description of what a code type does at generation time —
// shown in the editor so the effect of adding a code (and of its value, if
// any) is visible. Explains the disc-global auto-progressing clocks (CLV
// picture number / time code) and the continue-across-sections behaviour of
// the CAV picture number. Empty for an unknown code type.
std::string CodeTypeHelp(const std::string& code_type);

// Parses a comma/space-separated list of 1-based frame lines ("19, 282").
// Returns false on any non-numeric token; the output holds the values parsed
// so far.
bool ParseTargetLines(const std::string& text, std::vector<int>* out_lines);

// Formats target lines as "19, 282" for line edits.
std::string FormatTargetLines(const std::vector<int>& lines);

// Total disc frames across all sections — the valid at_frame range for disc
// skips (matches the validator's range check).
int TotalDiscFrames(const Project& project);

}  // namespace videosynth::gui
