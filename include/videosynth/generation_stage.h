/*
 * File:        generation_stage.h
 * Module:      generation_stage
 * Purpose:     Generates CVBS-domain Y and C sample buffers from frame-based
 * source data.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <memory>

#include "videosynth/biphase_injection_manager.h"
#include "videosynth/interfaces.h"
#include "videosynth/osd_renderer.h"
#include "videosynth/osd_token_resolver.h"
#include "videosynth/progressive_frame_source.h"
#include "videosynth/vits_definition_provider.h"
#include "videosynth/vits_generator.h"

namespace videosynth {

// Thread-safety: BuildFrameSchedule and Generate are NOT thread-safe and must
// not run concurrently with any other method (they mutate the biphase
// enrichment state). After BuildFrameSchedule has returned successfully,
// GenerateFrameBatch is safe to call concurrently from multiple threads for
// disjoint frame ranges with distinct output buffers: it reads only the
// immutable per-item FrameEnrichment payload, the internally-synchronised
// progressive frame source cache and synthesis resource cache, and const
// collaborators. The resource cache hands each worker its own set of
// frame-invariant resources, so no synthesis state is shared between threads.
class GenerationStage final : public IGenerationStage {
 public:
  explicit GenerationStage(
      ILogger* logger = nullptr,
      const IVitsDefinitionProvider* vits_definition_provider = nullptr,
      const IVitsGenerator* vits_generator = nullptr);

  ~GenerationStage() override;

  // Builds a schedule mapping project sections to output frames, then runs
  // the sequential enrichment pass: every FrameScheduleItem receives a
  // FrameEnrichment payload (resolved VBI code words, colour context, OSD
  // token strings) so GenerateFrameBatch is a pure function of
  // (project, enriched schedule item) and frames can be synthesised in any
  // order.
  //
  // Args:
  //   project: The validated project configuration.
  //   out_schedule: Output vector populated with FrameScheduleItem entries.
  //   errors: Output vector for any error messages.
  //
  // Returns:
  //   true on success, false on any error.
  bool BuildFrameSchedule(const Project& project,
                          std::vector<FrameScheduleItem>* out_schedule,
                          std::vector<std::string>* errors) override;

  // Generates a batch of frames from the project's progressive frame sources.
  //
  // Schedule items must carry their enrichment payload when the section uses
  // laserdisc line injections or OSD overlays; hand-built schedules without
  // enrichment are only accepted for sections that use neither.
  //
  // Args:
  //   project: The validated project configuration.
  //   schedule: The frame schedule built by BuildFrameSchedule.
  //   start_frame: Index of the first frame to generate in this batch.
  //   frame_count: Number of frames to generate.
  //   out_y_mv: Output vector for luma samples (Y channel).
  //   out_c_mv: Output vector for chroma samples (C channel).
  //   errors: Output vector for any error messages.
  //
  // Returns:
  //   true on success, false on any error.
  bool GenerateFrameBatch(const Project& project,
                          const std::vector<FrameScheduleItem>& schedule,
                          std::size_t start_frame, std::size_t frame_count,
                          std::vector<SampleFixed>* out_y_mv,
                          std::vector<SampleFixed>* out_c_mv,
                          std::vector<std::string>* errors) override;

  // Convenience wrapper that builds the schedule and generates all frames.
  //
  // Args:
  //   project: The validated project configuration.
  //   out_y_mv: Output vector for luma samples (Y channel).
  //   out_c_mv: Output vector for chroma samples (C channel).
  //   errors: Output vector for any error messages.
  //
  // Returns:
  //   true on success, false on any error.
  bool Generate(const Project& project, std::vector<SampleFixed>* out_y_mv,
                std::vector<SampleFixed>* out_c_mv,
                std::vector<std::string>* errors) override;

 private:
  // Frame-invariant synthesis resources (sampled timing context, chroma
  // encoder, rendered VITS lines, VBI waveform renderer) and the per-worker
  // cache that owns them. Both are defined in generation_stage.cpp; a worker
  // builds its set once and reuses it for every frame it synthesises.
  struct SynthesisResources;
  class SynthesisResourceCache;

  ILogger* logger_;
  std::unique_ptr<SynthesisResourceCache> resource_cache_;
  ProgressiveFrameSource progressive_source_;
  VitsDefinitionProvider default_vits_definition_provider_;
  VitsGenerator default_vits_generator_;
  const IVitsDefinitionProvider* vits_definition_provider_;
  const IVitsGenerator* vits_generator_;
  // Advanced only during the BuildFrameSchedule enrichment pass; not touched
  // by GenerateFrameBatch.
  BiphaseInjectionManager biphase_manager_;
  OsdRenderer osd_renderer_;
  OsdTokenResolver osd_token_resolver_;
};

}  // namespace videosynth
