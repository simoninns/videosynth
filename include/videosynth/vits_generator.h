/*
 * File:        vits_generator.h
 * Module:      vits
 * Purpose:     Declares VITS synthesis-planning interface and default
 * implementation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>

#include "videosynth/fixed_point.h"
#include "videosynth/vits_definition.h"

namespace videosynth {

struct VitsPlannedPrimitive {
  VitsPrimitiveDefinition definition;
  double level_or_amplitude_mv = 0.0;
  double dc_offset_mv = 0.0;
  double resolved_frequency_hz = 0.0;
  double phase_radians = 0.0;
};

struct VitsSynthesisPlan {
  Standard standard = Standard::kUnknown;
  std::string vits_type;
  std::vector<VitsPlannedPrimitive> primitives;
  std::vector<VitsCompositeDefinition> composites;
  std::vector<std::string> render_order;
};

struct VitsRenderedLine {
  Standard standard = Standard::kUnknown;
  std::string vits_type;
  double sample_rate_hz = 0.0;
  std::vector<SampleFixed> y_samples_mv;
  std::vector<SampleFixed> c_samples_mv;
};

class IVitsGenerator {
 public:
  virtual ~IVitsGenerator() = default;

  virtual bool BuildSynthesisPlan(const VitsDefinition& definition,
                                  VitsSynthesisPlan* out_plan,
                                  std::string* error) const = 0;

  virtual bool RenderLine(const VitsSynthesisPlan& plan, double sample_rate_hz,
                          int sample_count, VitsRenderedLine* out_line,
                          std::string* error) const = 0;
};

class VitsGenerator final : public IVitsGenerator {
 public:
  bool BuildSynthesisPlan(const VitsDefinition& definition,
                          VitsSynthesisPlan* out_plan,
                          std::string* error) const override;

  bool RenderLine(const VitsSynthesisPlan& plan, double sample_rate_hz,
                  int sample_count, VitsRenderedLine* out_line,
                  std::string* error) const override;
};

}  // namespace videosynth
