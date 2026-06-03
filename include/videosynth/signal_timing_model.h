/*
 * File:        signal_timing_model.h
 * Module:      signal_timing_model
 * Purpose:     Computes per-line sync and active-picture timing primitives.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <stdexcept>
#include <vector>

#include "videosynth/model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

enum class SyncPulseKind {
  kHorizontal,
  kEqualizing,
  kVerticalSync,
};

enum class LineContentKind {
  kVbiBlanking,
  kActivePicture,
};

struct LineTimingPrimitive {
  int line_number_1based = 0;
  int field_index_1based = 0;
  SyncPulseKind sync_pulse_kind = SyncPulseKind::kHorizontal;
  LineContentKind content_kind = LineContentKind::kActivePicture;
  bool has_two_half_line_pulses = false;
  bool burst_enabled = true;
  double burst_phase_rad = 0.0;
};

inline bool IsLineInRange(int line, int start, int end) {
  return line >= start && line <= end;
}

inline int Field1LineCount(Standard standard) {
  if (standard == Standard::kPal) {
    // ITU-R BT.1700 Annex 1 Part B Table 1 item 1 defines 625 lines/frame.
    // With 2:1 interlace this model treats lines 1-312 as field 1.
    return 312;
  }
  if (standard == Standard::kNtsc) {
    // SMPTE 170M-2004 Section 11.3 defines 525 lines/frame.
    // With 2:1 interlace this model treats lines 1-262 as field 1.
    return 262;
  }
  throw std::invalid_argument(
      "Field line count requested for unknown standard");
}

inline int GetFieldIndex(Standard standard, int line_1based) {
  return (line_1based <= Field1LineCount(standard)) ? 1 : 2;
}

inline SyncPulseKind GetSyncPulseKind(Standard standard, int line_1based) {
  if (standard == Standard::kPal) {
    // ITU-R BT.1700 Annex 1 Part B (Figures 3-5 and Table 3 l/m/n) with
    // line-granular framing yields the PAL equalizing/sync regions below; mixed
    // half-line combinations are applied by the generation pulse schedule.
    if (IsLineInRange(line_1based, 4, 6) ||
        IsLineInRange(line_1based, 311, 313) ||
        IsLineInRange(line_1based, 316, 318) ||
        IsLineInRange(line_1based, 623, 625)) {
      return SyncPulseKind::kEqualizing;
    }
    if (IsLineInRange(line_1based, 1, 3) ||
        IsLineInRange(line_1based, 314, 315)) {
      return SyncPulseKind::kVerticalSync;
    }
    return SyncPulseKind::kHorizontal;
  }

  if (standard == Standard::kNtsc) {
    // SMPTE 170M-2004 Section 13.3/Table 3 defines a 9-line vertical sync block
    // per field. With 1-indexed, line-granular framing, the field-1 block is at
    // lines 1-9, and the field-2 block aligns to lines 264-272.
    if (IsLineInRange(line_1based, 1, 3) || IsLineInRange(line_1based, 7, 9) ||
        IsLineInRange(line_1based, 264, 266) ||
        IsLineInRange(line_1based, 270, 272)) {
      return SyncPulseKind::kEqualizing;
    }
    if (IsLineInRange(line_1based, 4, 6) ||
        IsLineInRange(line_1based, 267, 269)) {
      return SyncPulseKind::kVerticalSync;
    }
    return SyncPulseKind::kHorizontal;
  }

  throw std::invalid_argument("Sync pulse kind requested for unknown standard");
}

inline LineContentKind GetLineContentKind(Standard standard, int line_1based) {
  if (standard == Standard::kPal) {
    // ITU-R BT.1700 Annex 1 Part B Table 1 item 1a (576 active lines) implies
    // active picture starts at lines 23 and 335 in this line-granular 625-line
    // PAL model so all 576 source rows land on full active-picture lines.
    if (IsLineInRange(line_1based, 16, 22) ||
        IsLineInRange(line_1based, 319, 334)) {
      return LineContentKind::kVbiBlanking;
    }
    return LineContentKind::kActivePicture;
  }

  if (standard == Standard::kNtsc) {
    // SMPTE 170M-2004 Section 13.3/Table 3 defines a 20-line + 1.5 us vertical
    // blanking interval and notes line-20/282 behavior. In this line-granular
    // model, field-1 active starts at 22 and field-2 active starts at 284,
    // leaving line 283 as the field-2 transition line.
    if (IsLineInRange(line_1based, 10, 21) ||
        IsLineInRange(line_1based, 263, 283)) {
      return LineContentKind::kVbiBlanking;
    }
    return LineContentKind::kActivePicture;
  }

  throw std::invalid_argument(
      "Line content kind requested for unknown standard");
}

inline bool HasTwoHalfLinePulses(SyncPulseKind kind) {
  return kind == SyncPulseKind::kEqualizing ||
         kind == SyncPulseKind::kVerticalSync;
}

inline double BurstPhaseRad(Standard standard, int line_1based) {
  constexpr double kPi = 3.14159265358979323846;

  if (standard == Standard::kNtsc) {
    // With absolute-time subcarrier synthesis in generation_stage, the
    // line-to-line π-radian progression arises naturally from 910 samples/line
    // at 4fsc. Keep a constant burst reference phase here so no artificial
    // per-line phase discontinuity is injected at line boundaries.
    constexpr double kNtscReferencePhase = kPi / 4.0;
    (void)line_1based;
    return kNtscReferencePhase;
  }

  // ITU-R BT.1700 Annex 1 Part B Table 1 item 10f and Figure 8:
  // PAL burst phase alternates +135/-135 degrees line-to-line.
  return ((line_1based % 2) == 1) ? (3.0 * kPi / 4.0) : (-3.0 * kPi / 4.0);
}

inline bool BurstEnabledForLine(SyncPulseKind kind) {
  // Baseline model: burst on normal horizontal lines only.
  return kind == SyncPulseKind::kHorizontal;
}

inline LineTimingPrimitive BuildLineTimingPrimitive(Standard standard,
                                                    int line_1based) {
  const SyncPulseKind sync_kind = GetSyncPulseKind(standard, line_1based);
  return LineTimingPrimitive{
      .line_number_1based = line_1based,
      .field_index_1based = GetFieldIndex(standard, line_1based),
      .sync_pulse_kind = sync_kind,
      .content_kind = GetLineContentKind(standard, line_1based),
      .has_two_half_line_pulses = HasTwoHalfLinePulses(sync_kind),
      .burst_enabled = BurstEnabledForLine(sync_kind),
      .burst_phase_rad = BurstPhaseRad(standard, line_1based),
  };
}

inline std::vector<LineTimingPrimitive> BuildFrameTimingPrimitives(
    Standard standard) {
  const TimingConstants timing = GetTimingConstants(standard);

  std::vector<LineTimingPrimitive> lines;
  lines.reserve(static_cast<std::size_t>(timing.lines_per_frame));
  for (int line = 1; line <= timing.lines_per_frame; ++line) {
    lines.push_back(BuildLineTimingPrimitive(standard, line));
  }
  return lines;
}

}  // namespace videosynth
