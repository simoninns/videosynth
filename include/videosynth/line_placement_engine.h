/*
 * File:        line_placement_engine.h
 * Module:      line_placement_engine
 * Purpose:     Field-aware VBI line placement for LaserDisc biphase and 40-bit
 *              FM code injection per IEC 60856 (PAL) and IEC 60857 (NTSC).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <set>
#include <string>
#include <vector>

#include "videosynth/biphase_types.h"
#include "videosynth/model.h"

namespace videosynth {

// Describes the biphase or FM code injection assignment for a single VBI line.
//
// is_fm and is_white_flag are mutually exclusive with a 24-bit biphase
// assignment (assigned=true, is_fm=false, is_white_flag=false).
// When is_fm=true the injection layer must use the 40-bit FM encoder.
// When is_white_flag=true the injection layer must fill the line at 100 IRE.
// uses_172h_offset=true means the signal must begin at 0.172 × line_period
// rather than at sample 0 of the active region. This applies to
// programme_status (PAL and NTSC) and clv_code (NTSC only).
struct LineCodeAssignment {
  bool assigned = false;
  std::string code_type;
  bool is_fm = false;
  bool is_white_flag = false;
  bool uses_172h_offset = false;
};

// Determines which biphase or FM code type should be injected on each VBI line,
// given a disc configuration and the set of code types present in the section.
//
// Usage pattern:
//   LinePlacementEngine engine(standard, disc_type, section_type, codes_present);
//   for each line in [1..lines_per_frame]:
//     bool f1 = LinePlacementEngine::IsFieldOne(standard, line);
//     auto a = engine.GetAssignment(line, f1);
//     if (a.assigned) { ... inject a.code_type ... }
//
// Line ranges queried outside the biphase-reserved VBI range always return
// an unassigned result (assigned=false).
//
// Priority rules (highest to lowest) per IEC 60856/60857:
//   1. lead_in / lead_out codes have absolute priority in their sections.
//   2. picture_stop > programme_status on all lines (CAV).
//   3. picture_number > picture_stop > chapter_number on conflict lines (CAV).
//   4. picture_number > chapter_number on remaining lines (CAV).
//   5. clv_picture_number > chapter_number on lines 16/279/329 (CLV).
//   6. programme_time_code > clv_code > nothing on lines 17/280/330 (CLV).
//   7. programme_time_code > chapter_number on lines 18/281/331 (CLV).
//   8. fm_picture_number / fm_programme_time occupy FM lines 10/273 (NTSC).
//   9. fm_white_flag occupies lines 11/274 (NTSC); placement varies by section.
//
// Thread-safety: LinePlacementEngine is immutable after construction and may be
// called concurrently from multiple threads once constructed.
class LinePlacementEngine {
 public:
  // Constructs the engine.
  //   standard:       PAL or NTSC.
  //   disc_type:      CAV or CLV.
  //   section_type:   lead_in, programme_area, or lead_out.
  //   codes_present:  All code type strings declared for this section.
  //                   Drives priority resolution — only listed codes are
  //                   considered for placement.
  LinePlacementEngine(Standard standard, DiscType disc_type,
                      SectionType section_type,
                      const std::vector<std::string>& codes_present);

  // Returns the code assignment for the given VBI line.
  //   line_number:  1-based line number within the frame.
  //   field_one:    true when line_number is in field 1.  Typically derived
  //                 from IsFieldOne(); accepted as a parameter so callers can
  //                 pass a precomputed value or override for testing.
  //                 Only material for NTSC white_flag placement.
  //
  // Returns an assignment with assigned=false if no biphase or FM code should
  // be injected on this line (line is outside reserved range, or the section
  // configuration does not call for any code here).
  LineCodeAssignment GetAssignment(int line_number, bool field_one) const;

  // Returns true if line_number falls within the IEC-defined biphase VBI
  // reserved range for the given standard:
  //   PAL:  6–18  (field 1)  or  319–331 (field 2)
  //   NTSC: 10–18 (field 1)  or  273–281 (field 2)
  static bool IsInBiphaseReservedRange(Standard standard, int line_number);

  // Returns true if line_number is in field 1.
  //   PAL:  field 1 = lines 1–312
  //   NTSC: field 1 = lines 1–262
  static bool IsFieldOne(Standard standard, int line_number);

 private:
  Standard standard_;
  DiscType disc_type_;
  SectionType section_type_;
  std::set<std::string> codes_present_;

  // Returns true if code_type is in codes_present_.
  bool Has(const std::string& code_type) const;

  // Factory helpers — construct a LineCodeAssignment quickly.
  static LineCodeAssignment Biphase(const std::string& code_type,
                                    bool uses_172h_offset = false);
  static LineCodeAssignment Fm(const std::string& code_type);
  static LineCodeAssignment WhiteFlag();
  static LineCodeAssignment None();

  // Per-standard dispatch.
  LineCodeAssignment GetPalAssignment(int line) const;
  LineCodeAssignment GetNtscAssignment(int line, bool field_one) const;

  // Section-type and disc-type handlers.
  LineCodeAssignment GetPalLeadInOut(int line) const;
  LineCodeAssignment GetNtscLeadInOut(int line, bool field_one) const;
  LineCodeAssignment GetPalCavProgramme(int line) const;
  LineCodeAssignment GetPalClvProgramme(int line) const;
  LineCodeAssignment GetNtscCavProgramme(int line, bool field_one) const;
  LineCodeAssignment GetNtscClvProgramme(int line, bool field_one) const;
};

}  // namespace videosynth
