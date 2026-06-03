/*
 * File:        vits_generator.cpp
 * Module:      vits
 * Purpose:     Builds synthesis plans from VITS catalog entries for future line
 * rendering.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/vits_generator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "videosynth/signal_shaping.h"

namespace videosynth {

namespace {

constexpr double kMillivoltsPerIre = 714.3 / 100.0;
constexpr double kPi = 3.14159265358979323846;

double SubcarrierFrequencyHz(Standard standard) {
  if (standard == Standard::kPal) {
    return 4433618.75;
  }
  if (standard == Standard::kNtsc) {
    return 3579545.0;
  }
  return 0.0;
}

double ToMillivolts(double value, VitsLevelsUnit unit) {
  if (unit == VitsLevelsUnit::kIre) {
    return value * kMillivoltsPerIre;
  }
  return value;
}

int TimeUsToSampleIndex(double time_us, double sample_rate_hz) {
  return static_cast<int>(std::lround((time_us * 1.0e-6) * sample_rate_hz));
}

int PrimitiveRampSamples(const VitsPlannedPrimitive& primitive,
                         double sample_rate_hz) {
  return RiseTimeToRampSamples(primitive.definition.rise_time_us * 1.0e-6,
                               sample_rate_hz);
}

double CarrierPhase(double frequency_hz, double phase_radians, int sample_index,
                    double sample_rate_hz) {
  if (frequency_hz <= 0.0 || sample_rate_hz <= 0.0) {
    return 0.0;
  }

  const double t = static_cast<double>(sample_index) / sample_rate_hz;
  return (2.0 * kPi * frequency_hz * t) + phase_radians;
}

double SinSquaredEnvelope(int relative_index, int width_samples) {
  if (relative_index < 0 || relative_index >= width_samples ||
      width_samples <= 1) {
    return 0.0;
  }

  const double x = static_cast<double>(relative_index) /
                   static_cast<double>(std::max(1, width_samples - 1));
  const double s = std::sin(kPi * x);
  return s * s;
}

bool ApplyPrimitive(const VitsPlannedPrimitive& primitive,
                    double sample_rate_hz, std::vector<SampleFixed>* samples,
                    std::string* error) {
  if (samples == nullptr) {
    if (error != nullptr) {
      *error = "Primitive rendering target buffer is null.";
    }
    return false;
  }

  const int sample_count = static_cast<int>(samples->size());
  const int unclamped_start =
      TimeUsToSampleIndex(primitive.definition.start_us, sample_rate_hz);
  const int unclamped_end =
      TimeUsToSampleIndex(primitive.definition.end_us, sample_rate_hz);
  const int start = std::max(0, std::min(sample_count, unclamped_start));
  const int end = std::max(0, std::min(sample_count, unclamped_end));
  const int width_samples = end - start;
  if (width_samples <= 0) {
    return true;
  }

  const int ramp_samples = PrimitiveRampSamples(primitive, sample_rate_hz);

  for (int sample_index = start; sample_index < end; ++sample_index) {
    const int relative_index = sample_index - start;
    const double baseline_mv = SampleFixedToMillivolts(
        (*samples)[static_cast<std::size_t>(sample_index)]);

    double target_mv = 0.0;
    switch (primitive.definition.primitive_type) {
      case VitsPrimitiveType::kColourBar:
        target_mv = primitive.level_or_amplitude_mv + primitive.dc_offset_mv;
        break;
      case VitsPrimitiveType::kBurst: {
        const double envelope =
            ShapedGateEnvelope(relative_index, width_samples, ramp_samples);
        const int oscillator_sample_index =
            primitive.definition.subcarrier_lock_multiple > 0.0
                ? sample_index
                : (sample_index - start);
        const double phase = CarrierPhase(
            primitive.resolved_frequency_hz, primitive.phase_radians,
            oscillator_sample_index, sample_rate_hz);
        target_mv = primitive.dc_offset_mv + (primitive.level_or_amplitude_mv *
                                              envelope * std::sin(phase));
        break;
      }
      case VitsPrimitiveType::kSinSquaredPulse: {
        const double envelope =
            SinSquaredEnvelope(relative_index, width_samples);
        target_mv = primitive.dc_offset_mv +
                    (primitive.level_or_amplitude_mv * envelope);
        break;
      }
      case VitsPrimitiveType::kCompositePulse: {
        const double envelope =
            SinSquaredEnvelope(relative_index, width_samples);
        const int oscillator_sample_index =
            primitive.definition.subcarrier_lock_multiple > 0.0
                ? sample_index
                : (sample_index - start);
        const double phase = CarrierPhase(
            primitive.resolved_frequency_hz, primitive.phase_radians,
            oscillator_sample_index, sample_rate_hz);
        target_mv = primitive.dc_offset_mv + (primitive.level_or_amplitude_mv *
                                              envelope * std::sin(phase));
        break;
      }
      case VitsPrimitiveType::kStaircase: {
        const int steps = std::max(1, primitive.definition.staircase_steps);
        const int clamped_relative =
            std::min(relative_index, width_samples - 1);
        const int step_index = std::min(
            steps - 1, static_cast<int>(
                           (static_cast<long long>(clamped_relative) * steps) /
                           std::max(1, width_samples)));
        const double step_level =
            primitive.level_or_amplitude_mv *
            (static_cast<double>(step_index + 1) / static_cast<double>(steps));
        target_mv = step_level + primitive.dc_offset_mv;
        break;
      }
    }

    if (primitive.definition.combine_mode == VitsCombineMode::kReplace) {
      const double gate =
          ShapedGateEnvelope(relative_index, width_samples, ramp_samples);
      const double output_mv = baseline_mv + ((target_mv - baseline_mv) * gate);
      (*samples)[static_cast<std::size_t>(sample_index)] =
          MillivoltsToSampleFixed(output_mv);
      continue;
    }

    // Add-mode colour bars/staircases carry explicit rise_time_us in the
    // catalog; apply the same gate envelope so they ramp in/out instead of hard
    // stepping.
    double add_mv = target_mv;
    if (primitive.definition.primitive_type == VitsPrimitiveType::kColourBar ||
        primitive.definition.primitive_type == VitsPrimitiveType::kStaircase) {
      const double gate =
          ShapedGateEnvelope(relative_index, width_samples, ramp_samples);
      add_mv *= gate;
    }

    (*samples)[static_cast<std::size_t>(sample_index)] +=
        MillivoltsToSampleFixed(add_mv);
  }

  return true;
}

const VitsPlannedPrimitive* FindPrimitive(const VitsSynthesisPlan& plan,
                                          const std::string& id) {
  for (const VitsPlannedPrimitive& primitive : plan.primitives) {
    if (primitive.definition.id == id) {
      return &primitive;
    }
  }
  return nullptr;
}

const VitsCompositeDefinition* FindComposite(const VitsSynthesisPlan& plan,
                                             const std::string& id) {
  for (const VitsCompositeDefinition& composite : plan.composites) {
    if (composite.id == id) {
      return &composite;
    }
  }
  return nullptr;
}

void ApplySerialCrossfadeTransitionPolicies(VitsSynthesisPlan* plan) {
  if (plan == nullptr) {
    return;
  }

  std::unordered_map<std::string, std::size_t> primitive_index;
  primitive_index.reserve(plan->primitives.size());
  for (std::size_t i = 0; i < plan->primitives.size(); ++i) {
    primitive_index[plan->primitives[i].definition.id] = i;
  }

  for (const VitsCompositeDefinition& composite : plan->composites) {
    if (composite.mode != VitsCompositeMode::kSerial ||
        composite.children.size() < 2) {
      continue;
    }

    for (std::size_t i = 0; i + 1 < composite.children.size(); ++i) {
      const auto current_it = primitive_index.find(composite.children[i]);
      const auto next_it = primitive_index.find(composite.children[i + 1]);
      if (current_it == primitive_index.end() ||
          next_it == primitive_index.end()) {
        continue;
      }

      VitsPlannedPrimitive& current = plan->primitives[current_it->second];
      VitsPlannedPrimitive& next = plan->primitives[next_it->second];

      if (current.definition.transition_out_policy !=
              VitsTransitionOutPolicy::kCrossfade ||
          current.definition.transition_out_duration_us <= 0.0) {
        continue;
      }

      if (current.definition.signal_component !=
              next.definition.signal_component ||
          current.definition.combine_mode != next.definition.combine_mode) {
        continue;
      }

      if (current.definition.continuity_group.empty() &&
          composite.continuity_group.empty()) {
        continue;
      }

      const double crossfade_us = current.definition.transition_out_duration_us;
      if (next.definition.start_us + 1e-9 < current.definition.end_us) {
        continue;
      }

      current.definition.end_us += crossfade_us;
      next.definition.start_us = std::max(
          current.definition.start_us, next.definition.start_us - crossfade_us);
    }
  }
}

void ApplyGateEnvelopeOverlapCorrections(VitsSynthesisPlan* plan) {
  if (plan == nullptr) {
    return;
  }

  constexpr double kAdjacencyTolerance = 0.1;  // microseconds

  // For each primitive with a gate envelope in replace mode, find and create
  // overlap with the next primitive on the same signal component to prevent
  // "drop to zero" artifacts.
  for (std::size_t i = 0; i < plan->primitives.size(); ++i) {
    VitsPlannedPrimitive& current = plan->primitives[i];

    if (current.definition.rise_time_us <= 0.0) {
      continue;
    }

    // Prefer explicit continuity chains when the catalog marks a construction
    // sequence.
    double best_gap = std::numeric_limits<double>::max();
    std::size_t best_index = plan->primitives.size();
    bool found_group_join = false;

    for (std::size_t j = 0; j < plan->primitives.size(); ++j) {
      if (i == j) continue;

      const VitsPlannedPrimitive& candidate = plan->primitives[j];

      // Continuity joins are component-local; Y and C chains must not
      // cross-couple.
      if (candidate.definition.signal_component !=
          current.definition.signal_component) {
        continue;
      }

      if (!current.definition.continuity_group.empty() &&
          current.definition.continuity_group ==
              candidate.definition.continuity_group &&
          j > i) {
        best_index = j;
        found_group_join = true;
        break;
      }

      if (!current.definition.continuity_group.empty() ||
          !candidate.definition.continuity_group.empty()) {
        continue;
      }

      // Check if it comes after current in time and is close enough
      const double time_gap =
          candidate.definition.start_us - current.definition.end_us;
      if (time_gap < -kAdjacencyTolerance || time_gap > kAdjacencyTolerance) {
        continue;
      }

      // Track the closest adjacent one
      if (std::fabs(time_gap) < std::fabs(best_gap)) {
        best_gap = time_gap;
        best_index = j;
      }
    }

    // If found an adjacent primitive on the same component, span the join point
    // so the handoff is synthesized across the connection instead of landing on
    // it.
    if (best_index < plan->primitives.size()) {
      VitsPlannedPrimitive& next = plan->primitives[best_index];

      // Gate-envelope overlap correction is only valid for replace-mode
      // handoffs. Add-mode burst chains use explicit transition policies and
      // must not be stretched by this pass.
      if (current.definition.combine_mode != VitsCombineMode::kReplace ||
          next.definition.combine_mode != VitsCombineMode::kReplace) {
        continue;
      }

      const double join_padding_us =
          current.definition.rise_time_us + next.definition.rise_time_us;
      current.definition.end_us += join_padding_us;
      next.definition.start_us =
          std::max(0.0, next.definition.start_us - join_padding_us);
    }
  }
}

bool ApplyNode(const VitsSynthesisPlan& plan, const std::string& id,
               double sample_rate_hz, std::vector<SampleFixed>* y_samples,
               std::vector<SampleFixed>* c_samples, std::string* error);

bool ApplyComposite(const VitsSynthesisPlan& plan,
                    const VitsCompositeDefinition& composite,
                    double sample_rate_hz, std::vector<SampleFixed>* y_samples,
                    std::vector<SampleFixed>* c_samples, std::string* error) {
  if (composite.mode == VitsCompositeMode::kSerial) {
    for (const std::string& child_id : composite.children) {
      if (!ApplyNode(plan, child_id, sample_rate_hz, y_samples, c_samples,
                     error)) {
        return false;
      }
    }
    return true;
  }

  const std::vector<SampleFixed> baseline_y = *y_samples;
  const std::vector<SampleFixed> baseline_c = *c_samples;
  std::vector<SampleFixed> aggregate_y = baseline_y;
  std::vector<SampleFixed> aggregate_c = baseline_c;

  for (const std::string& child_id : composite.children) {
    std::vector<SampleFixed> child_y = baseline_y;
    std::vector<SampleFixed> child_c = baseline_c;
    if (!ApplyNode(plan, child_id, sample_rate_hz, &child_y, &child_c, error)) {
      return false;
    }

    for (std::size_t i = 0; i < aggregate_y.size(); ++i) {
      aggregate_y[i] += child_y[i] - baseline_y[i];
      aggregate_c[i] += child_c[i] - baseline_c[i];
    }
  }

  *y_samples = std::move(aggregate_y);
  *c_samples = std::move(aggregate_c);
  return true;
}

bool ApplyNode(const VitsSynthesisPlan& plan, const std::string& id,
               double sample_rate_hz, std::vector<SampleFixed>* y_samples,
               std::vector<SampleFixed>* c_samples, std::string* error) {
  if (const VitsPlannedPrimitive* primitive = FindPrimitive(plan, id)) {
    std::vector<SampleFixed>* target =
        primitive->definition.signal_component == VitsSignalComponent::kY
            ? y_samples
            : c_samples;
    return ApplyPrimitive(*primitive, sample_rate_hz, target, error);
  }

  if (const VitsCompositeDefinition* composite = FindComposite(plan, id)) {
    return ApplyComposite(plan, *composite, sample_rate_hz, y_samples,
                          c_samples, error);
  }

  if (error != nullptr) {
    *error = "VITS synthesis plan references unknown node '" + id + "'.";
  }
  return false;
}

}  // namespace

bool VitsGenerator::BuildSynthesisPlan(const VitsDefinition& definition,
                                       VitsSynthesisPlan* out_plan,
                                       std::string* error) const {
  if (out_plan == nullptr) {
    if (error != nullptr) {
      *error = "VITS synthesis plan output pointer is null.";
    }
    return false;
  }

  out_plan->standard = definition.standard;
  out_plan->vits_type = definition.vits_type;
  out_plan->primitives.clear();
  out_plan->primitives.reserve(definition.primitives.size());
  out_plan->composites = definition.composites;
  out_plan->render_order = definition.render_order;

  const double subcarrier_hz = SubcarrierFrequencyHz(definition.standard);

  for (const VitsPrimitiveDefinition& primitive : definition.primitives) {
    VitsPlannedPrimitive planned;
    planned.definition = primitive;
    planned.level_or_amplitude_mv =
        ToMillivolts(primitive.level_or_amplitude, definition.levels_unit);
    planned.dc_offset_mv =
        ToMillivolts(primitive.dc_offset, definition.levels_unit);
    if (primitive.frequency_mhz > 0.0) {
      planned.resolved_frequency_hz = primitive.frequency_mhz * 1.0e6;
    } else if (primitive.subcarrier_lock_multiple > 0.0) {
      planned.resolved_frequency_hz =
          primitive.subcarrier_lock_multiple * subcarrier_hz;
    }
    planned.phase_radians = primitive.phase_deg * kPi / 180.0;
    out_plan->primitives.push_back(planned);
  }

  ApplySerialCrossfadeTransitionPolicies(out_plan);
  ApplyGateEnvelopeOverlapCorrections(out_plan);

  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool VitsGenerator::RenderLine(const VitsSynthesisPlan& plan,
                               double sample_rate_hz, int sample_count,
                               VitsRenderedLine* out_line,
                               std::string* error) const {
  if (out_line == nullptr) {
    if (error != nullptr) {
      *error = "VITS rendered line output pointer is null.";
    }
    return false;
  }

  if (sample_rate_hz <= 0.0 || sample_count <= 0) {
    if (error != nullptr) {
      *error =
          "VITS line rendering requires positive sample_rate_hz and "
          "sample_count.";
    }
    return false;
  }

  out_line->standard = plan.standard;
  out_line->vits_type = plan.vits_type;
  out_line->sample_rate_hz = sample_rate_hz;
  out_line->y_samples_mv.assign(static_cast<std::size_t>(sample_count),
                                MillivoltsToSampleFixed(0.0));
  out_line->c_samples_mv.assign(static_cast<std::size_t>(sample_count),
                                MillivoltsToSampleFixed(0.0));

  for (const std::string& node_id : plan.render_order) {
    if (!ApplyNode(plan, node_id, sample_rate_hz, &out_line->y_samples_mv,
                   &out_line->c_samples_mv, error)) {
      return false;
    }
  }

  if (error != nullptr) {
    error->clear();
  }
  return true;
}

}  // namespace videosynth
