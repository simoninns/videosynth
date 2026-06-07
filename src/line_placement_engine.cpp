/*
 * File:        line_placement_engine.cpp
 * Module:      line_placement_engine
 * Purpose:     Field-aware VBI line placement for LaserDisc biphase and 40-bit
 *              FM code injection per IEC 60856 (PAL) and IEC 60857 (NTSC).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/line_placement_engine.h"

#include <algorithm>

namespace videosynth {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

LinePlacementEngine::LinePlacementEngine(
    Standard standard, DiscType disc_type, SectionType section_type,
    const std::vector<std::string>& codes_present)
    : standard_(standard),
      disc_type_(disc_type),
      section_type_(section_type),
      codes_present_(codes_present.begin(), codes_present.end()) {}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

bool LinePlacementEngine::IsInBiphaseReservedRange(Standard standard,
                                                   int line_number) {
  if (standard == Standard::kPal) {
    return (line_number >= 6 && line_number <= 18) ||
           (line_number >= 319 && line_number <= 331);
  }
  // NTSC: IEC 60857 §10 — lines 10–18 (field 1), 273–281 (field 2).
  return (line_number >= 10 && line_number <= 18) ||
         (line_number >= 273 && line_number <= 281);
}

bool LinePlacementEngine::IsFieldOne(Standard standard, int line_number) {
  // PAL: 625-line frame; field 1 = lines 1–312 (ITU-R BT.1700).
  // NTSC: 525-line frame; field 1 = lines 1–262 (SMPTE 170M-2004).
  if (standard == Standard::kPal) {
    return line_number <= 312;
  }
  return line_number <= 262;
}

// ---------------------------------------------------------------------------
// Private factory helpers
// ---------------------------------------------------------------------------

LineCodeAssignment LinePlacementEngine::None() { return LineCodeAssignment{}; }

LineCodeAssignment LinePlacementEngine::Biphase(const std::string& code_type,
                                                bool uses_172h_offset) {
  return LineCodeAssignment{
      .assigned = true,
      .code_type = code_type,
      .is_fm = false,
      .is_white_flag = false,
      .uses_172h_offset = uses_172h_offset,
  };
}

LineCodeAssignment LinePlacementEngine::Fm(const std::string& code_type) {
  return LineCodeAssignment{
      .assigned = true,
      .code_type = code_type,
      .is_fm = true,
      .is_white_flag = false,
      .uses_172h_offset = false,
  };
}

LineCodeAssignment LinePlacementEngine::WhiteFlag() {
  return LineCodeAssignment{
      .assigned = true,
      .code_type = "fm_white_flag",
      .is_fm = false,
      .is_white_flag = true,
      .uses_172h_offset = false,
  };
}

bool LinePlacementEngine::Has(const std::string& code_type) const {
  return codes_present_.count(code_type) > 0;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

LineCodeAssignment LinePlacementEngine::GetAssignment(int line_number,
                                                      bool field_one) const {
  if (!IsInBiphaseReservedRange(standard_, line_number)) {
    return None();
  }

  if (standard_ == Standard::kPal) {
    return GetPalAssignment(line_number);
  }
  return GetNtscAssignment(line_number, field_one);
}

// ---------------------------------------------------------------------------
// PAL dispatch
// ---------------------------------------------------------------------------

LineCodeAssignment LinePlacementEngine::GetPalAssignment(int line) const {
  if (section_type_ == SectionType::kLeadIn ||
      section_type_ == SectionType::kLeadOut) {
    return GetPalLeadInOut(line);
  }
  if (section_type_ == SectionType::kProgrammeArea) {
    if (disc_type_ == DiscType::kCAV) {
      return GetPalCavProgramme(line);
    }
    if (disc_type_ == DiscType::kCLV) {
      return GetPalClvProgramme(line);
    }
  }
  return None();
}

// ---------------------------------------------------------------------------
// PAL lead-in / lead-out
// PAL field 1: 16 → users_code, 17–18 → section marker
// PAL field 2: 329 → users_code, 330–331 → section marker
// ---------------------------------------------------------------------------

LineCodeAssignment LinePlacementEngine::GetPalLeadInOut(int line) const {
  const std::string marker =
      (section_type_ == SectionType::kLeadIn) ? "lead_in" : "lead_out";

  switch (line) {
    case 16:
      if (Has("users_code")) return Biphase("users_code");
      return None();
    case 17:
    case 18:
      return Biphase(marker);
    case 329:
      if (Has("users_code")) return Biphase("users_code");
      return None();
    case 330:
    case 331:
      return Biphase(marker);
    default:
      return None();
  }
}

// ---------------------------------------------------------------------------
// PAL CAV programme_area
//
// IEC 60856 §10.1 line assignments (field 1 / field 2 mirror):
//   Line 16/329:  picture_stop > programme_status (0.172H offset)
//   Line 17/330:  picture_number > picture_stop > chapter_number
//   Line 18/331:  picture_number > chapter_number
// ---------------------------------------------------------------------------

LineCodeAssignment LinePlacementEngine::GetPalCavProgramme(int line) const {
  // Normalise field-2 line to its field-1 equivalent for table lookup.
  // PAL field-2 active biphase lines: 329→16, 330→17, 331→18.
  const int canonical = (line >= 329) ? (line - 313) : line;

  switch (canonical) {
    case 16:
      if (Has("picture_stop")) return Biphase("picture_stop");
      if (Has("programme_status")) return Biphase("programme_status", true);
      return None();

    case 17:
      // IEC 60856: chapter cannot share lines with picture_number;
      // picture_stop has priority over chapter on this conflict line.
      if (Has("picture_number")) return Biphase("picture_number");
      if (Has("picture_stop")) return Biphase("picture_stop");
      if (Has("chapter_number")) return Biphase("chapter_number");
      return None();

    case 18:
      if (Has("chapter_number")) return Biphase("chapter_number");
      if (Has("picture_number")) return Biphase("picture_number");
      return None();

    default:
      return None();
  }
}

// ---------------------------------------------------------------------------
// PAL CLV programme_area
//
// IEC 60856 §10.1 line assignments (field 1 / field 2 mirror):
//   Line 16/329:  clv_picture_number > programme_status (0.172H offset)
//   Line 17/330:  programme_time_code > clv_code
//   Line 18/331:  programme_time_code > chapter_number
// ---------------------------------------------------------------------------

LineCodeAssignment LinePlacementEngine::GetPalClvProgramme(int line) const {
  const int canonical = (line >= 329) ? (line - 313) : line;

  switch (canonical) {
    case 16:
      if (Has("clv_picture_number")) return Biphase("clv_picture_number");
      if (Has("programme_status")) return Biphase("programme_status", true);
      return None();

    case 17:
      // IEC 60856: programme_time_code takes precedence over clv_code.
      // clv_code is only placed where no programme_time_code is present.
      if (Has("programme_time_code")) return Biphase("programme_time_code");
      if (Has("clv_code")) return Biphase("clv_code");
      return None();

    case 18:
      // chapter_number claims line 18 exclusively; programme_time_code fills
      // line 18 only as a redundant copy when no chapter is present.
      if (Has("chapter_number")) return Biphase("chapter_number");
      if (Has("programme_time_code")) return Biphase("programme_time_code");
      return None();

    default:
      return None();
  }
}

// ---------------------------------------------------------------------------
// NTSC dispatch
// ---------------------------------------------------------------------------

LineCodeAssignment LinePlacementEngine::GetNtscAssignment(
    int line, bool field_one) const {
  if (section_type_ == SectionType::kLeadIn ||
      section_type_ == SectionType::kLeadOut) {
    return GetNtscLeadInOut(line, field_one);
  }
  if (section_type_ == SectionType::kProgrammeArea) {
    if (disc_type_ == DiscType::kCAV) {
      return GetNtscCavProgramme(line, field_one);
    }
    if (disc_type_ == DiscType::kCLV) {
      return GetNtscClvProgramme(line, field_one);
    }
  }
  return None();
}

// ---------------------------------------------------------------------------
// NTSC lead-in / lead-out
//
// FM white flag placement (IEC 60857 §10.2.1, §10.2.2):
//   lead_in:  line 11 (field 1) only; line 274 (field 2) NOT used.
//   lead_out: both lines 11 and 274.
//
// 24-bit biphase:
//   Field 1: 16 → users_code, 17–18 → section marker
//   Field 2: 279 → users_code, 280–281 → section marker
// ---------------------------------------------------------------------------

LineCodeAssignment LinePlacementEngine::GetNtscLeadInOut(int line,
                                                         bool field_one) const {
  const std::string marker =
      (section_type_ == SectionType::kLeadIn) ? "lead_in" : "lead_out";

  // White flag lines (FM, NTSC only).
  if (line == 11) {
    if (Has("fm_white_flag")) return WhiteFlag();
    return None();
  }
  if (line == 274) {
    // White flag on line 274 only during lead-out (§10.2.2).
    if (section_type_ == SectionType::kLeadOut && Has("fm_white_flag")) {
      return WhiteFlag();
    }
    return None();
  }

  // Lines 10 and 273 carry no FM data in lead-in/lead-out sections.
  if (line == 10 || line == 273) {
    return None();
  }

  // 24-bit biphase lines.
  if (line == 16) {
    if (Has("users_code")) return Biphase("users_code");
    return None();
  }
  if (line == 17 || line == 18) {
    return Biphase(marker);
  }
  if (line == 279) {
    if (Has("users_code")) return Biphase("users_code");
    return None();
  }
  if (line == 280 || line == 281) {
    return Biphase(marker);
  }

  (void)field_one;
  return None();
}

// ---------------------------------------------------------------------------
// NTSC CAV programme_area
//
// FM lines (IEC 60857 §10.2):
//   Line 10/273:  fm_picture_number (40-bit FM)
//   Line 11/274:  fm_white_flag (100 IRE)
//
// 24-bit biphase (field 1 / field 2 mirror):
//   Line 16/279:  picture_stop > programme_status (0.172H offset)
//   Line 17/280:  picture_number > picture_stop > chapter_number
//   Line 18/281:  picture_number > chapter_number
// ---------------------------------------------------------------------------

LineCodeAssignment LinePlacementEngine::GetNtscCavProgramme(
    int line, bool field_one) const {
  // FM signal lines.
  if (line == 10 || line == 273) {
    if (Has("fm_picture_number")) return Fm("fm_picture_number");
    return None();
  }
  if (line == 11 || line == 274) {
    if (Has("fm_white_flag")) return WhiteFlag();
    return None();
  }

  // Normalise field-2 biphase line → field-1 canonical.
  // NTSC field-2 active biphase: 279→16, 280→17, 281→18.
  const int canonical = (line >= 279) ? (line - 263) : line;

  switch (canonical) {
    case 16:
      if (Has("picture_stop")) return Biphase("picture_stop");
      if (Has("programme_status")) return Biphase("programme_status", true);
      return None();

    case 17:
      if (Has("picture_number")) return Biphase("picture_number");
      if (Has("picture_stop")) return Biphase("picture_stop");
      if (Has("chapter_number")) return Biphase("chapter_number");
      return None();

    case 18:
      if (Has("chapter_number")) return Biphase("chapter_number");
      if (Has("picture_number")) return Biphase("picture_number");
      return None();

    default:
      (void)field_one;
      return None();
  }
}

// ---------------------------------------------------------------------------
// NTSC CLV programme_area
//
// FM lines (IEC 60857 §10.2):
//   Line 10/273:  fm_programme_time (40-bit FM)
//   Line 11/274:  fm_white_flag (100 IRE)
//
// 24-bit biphase (field 1 / field 2 mirror):
//   Line 16/279:  clv_picture_number > programme_status (0.172H offset)
//   Line 17/280:  programme_time_code > clv_code (0.172H offset, NTSC only)
//   Line 18/281:  programme_time_code > chapter_number
// ---------------------------------------------------------------------------

LineCodeAssignment LinePlacementEngine::GetNtscClvProgramme(
    int line, bool field_one) const {
  // FM signal lines.
  if (line == 10 || line == 273) {
    if (Has("fm_programme_time")) return Fm("fm_programme_time");
    return None();
  }
  if (line == 11 || line == 274) {
    if (Has("fm_white_flag")) return WhiteFlag();
    return None();
  }

  // Normalise field-2 biphase line → field-1 canonical.
  const int canonical = (line >= 279) ? (line - 263) : line;

  switch (canonical) {
    case 16:
      if (Has("clv_picture_number")) return Biphase("clv_picture_number");
      if (Has("programme_status")) return Biphase("programme_status", true);
      return None();

    case 17:
      if (Has("programme_time_code")) return Biphase("programme_time_code");
      // IEC 60857 Figure 11: NTSC clv_code uses 0.172H horizontal offset.
      if (Has("clv_code")) return Biphase("clv_code", true);
      return None();

    case 18:
      // chapter_number claims line 18 exclusively; programme_time_code fills
      // line 18 only as a redundant copy when no chapter is present.
      if (Has("chapter_number")) return Biphase("chapter_number");
      if (Has("programme_time_code")) return Biphase("programme_time_code");
      return None();

    default:
      (void)field_one;
      return None();
  }
}

}  // namespace videosynth
