/*
 * File:        vits_definition.h
 * Module:      vits
 * Purpose:     Declares VITS catalog data structures shared by provider and
 * generator.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>

#include "videosynth/model.h"

namespace videosynth {

enum class VitsLevelsUnit {
  kMillivolts,
  kIre,
};

enum class VitsTimingReference {
  kSyncEdge,
};

enum class VitsPrimitiveType {
  kColourBar,
  kBurst,
  kSinSquaredPulse,
  kCompositePulse,
  kStaircase,
};

enum class VitsSignalComponent {
  kY,
  kC,
};

enum class VitsCombineMode {
  kReplace,
  kAdd,
};

enum class VitsCompositeMode {
  kSerial,
  kParallel,
};

enum class VitsTransitionOutPolicy {
  kNone,
  kCrossfade,
};

struct VitsPrimitiveDefinition {
  std::string id;
  VitsPrimitiveType primitive_type = VitsPrimitiveType::kColourBar;
  VitsSignalComponent signal_component = VitsSignalComponent::kY;
  VitsCombineMode combine_mode = VitsCombineMode::kReplace;
  std::string continuity_group;
  VitsTransitionOutPolicy transition_out_policy =
      VitsTransitionOutPolicy::kNone;
  double transition_out_duration_us = 0.0;
  double start_us = 0.0;
  double end_us = 0.0;
  double rise_time_us = 0.0;
  double level_or_amplitude = 0.0;
  double dc_offset = 0.0;
  double frequency_mhz = 0.0;
  double subcarrier_lock_multiple = 0.0;
  double phase_deg = 0.0;
  int staircase_steps = 0;
};

struct VitsCompositeDefinition {
  std::string id;
  VitsCompositeMode mode = VitsCompositeMode::kSerial;
  std::vector<std::string> children;
  std::string continuity_group;
  std::string baseline_anchor;
};

struct VitsDefinition {
  Standard standard = Standard::kUnknown;
  std::string vits_type;
  int recommended_frame_line = 0;
  VitsLevelsUnit levels_unit = VitsLevelsUnit::kMillivolts;
  VitsTimingReference timing_reference = VitsTimingReference::kSyncEdge;
  double y_rise_time_us = 0.0;
  double c_rise_time_us = 0.0;
  std::vector<VitsPrimitiveDefinition> primitives;
  std::vector<VitsCompositeDefinition> composites;
  std::vector<std::string> render_order;
};

}  // namespace videosynth
