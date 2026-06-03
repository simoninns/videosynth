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

// Thread-safety: Implementations of IVitsGenerator must be thread-safe.
// BuildSynthesisPlan and RenderLine may be called concurrently from multiple
// threads.
class IVitsGenerator {
 public:
  virtual ~IVitsGenerator() = default;

  // Ownership: out_plan and error are output parameters. The caller owns
  // the pointed-to memory and must ensure the pointers are valid (non-null).
  // The implementation writes to these locations but does not take ownership.
  virtual bool BuildSynthesisPlan(const VitsDefinition& definition,
                                  VitsSynthesisPlan* out_plan,
                                  std::string* error) const = 0;

  // Ownership: out_line and error are output parameters. The caller owns
  // the pointed-to memory and must ensure the pointers are valid (non-null).
  // The implementation writes to these locations but does not take ownership.
  virtual bool RenderLine(const VitsSynthesisPlan& plan, double sample_rate_hz,
                          int sample_count, VitsRenderedLine* out_line,
                          std::string* error) const = 0;
};

// Thread-safety: VitsGenerator is thread-safe for concurrent calls to
// BuildSynthesisPlan and RenderLine. All member access is read-only.
class VitsGenerator final : public IVitsGenerator {
 public:
  // Builds a synthesis plan from a VITS definition, resolving frequencies and
  // phases.
  //
  // Args:
  //   definition: The VITS definition from the catalog.
  //   out_plan: Output pointer for the resolved synthesis plan.
  //   error: Output pointer for any error message.
  //
  // Returns:
  //   true on success, false on any error.
  bool BuildSynthesisPlan(const VitsDefinition& definition,
                          VitsSynthesisPlan* out_plan,
                          std::string* error) const override;

  // Renders a single line of VITS signal from a synthesis plan.
  //
  // Args:
  //   plan: The synthesis plan built by BuildSynthesisPlan.
  //   sample_rate_hz: The output sample rate in Hz.
  //   sample_count: Number of samples to render for this line.
  //   out_line: Output pointer for the rendered line data.
  //   error: Output pointer for any error message.
  //
  // Returns:
  //   true on success, false on any error.
  bool RenderLine(const VitsSynthesisPlan& plan, double sample_rate_hz,
                  int sample_count, VitsRenderedLine* out_line,
                  std::string* error) const override;
};

}  // namespace videosynth
