/*
 * File:        interfaces.h
 * Module:      interfaces
 * Purpose:     Defines core pipeline interfaces and runtime options.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "videosynth/fixed_point.h"
#include "videosynth/model.h"
#include "videosynth/results.h"

namespace videosynth {

struct RunOptions {
  std::string project_path;
  bool validate_only = false;
  std::string log_level = "info";
  std::string log_file;
};

// Thread-safety: Implementations of ILogger must be thread-safe.
// Multiple threads may call logging methods concurrently.
class ILogger {
 public:
  virtual ~ILogger() = default;
  virtual void Info(const std::string& message) = 0;
  virtual void Warning(const std::string& message) = 0;
  virtual void Error(const std::string& message) = 0;
  virtual void Debug(const std::string& message) = 0;
  virtual void Trace(const std::string& message) = 0;
};

// Thread-safety: Implementations of IProjectParser must be thread-safe.
// ParseFile may be called concurrently from multiple threads.
class IProjectParser {
 public:
  virtual ~IProjectParser() = default;
  virtual ParseResult ParseFile(const std::string& path) = 0;
};

// Thread-safety: Implementations of IProjectValidator must be thread-safe.
// Validate may be called concurrently from multiple threads.
class IProjectValidator {
 public:
  virtual ~IProjectValidator() = default;
  virtual ValidationResult Validate(const Project& project) = 0;
};

struct ProgressiveFrameSourceProfile {
  std::string container;
  std::string codec;
  std::string pixel_format;
  std::string field_order;
  std::string color_space;
  std::string color_primaries;
  std::string color_transfer;
  std::string color_range;
  int bit_depth = 0;
  int width = 0;
  int height = 0;
  double sample_aspect_ratio = 0.0;
  int crop_left = 0;
  int crop_right = 0;
  int crop_top = 0;
  int crop_bottom = 0;
  double frame_rate_hz = 0.0;
  int frame_count = 0;
};

// Thread-safety: Implementations of IProgressiveFrameSourceProbe must be
// thread-safe. Probe may be called concurrently from multiple threads.
class IProgressiveFrameSourceProbe {
 public:
  virtual ~IProgressiveFrameSourceProbe() = default;
  // Ownership: out_profile and error are output parameters. The caller owns
  // the pointed-to memory and must ensure the pointers are valid (non-null).
  // The implementation writes to these locations but does not take ownership.
  virtual bool Probe(const Section& section,
                     ProgressiveFrameSourceProfile* out_profile,
                     std::string* error) = 0;
};

struct FrameSourceImage;

// Thread-safety: Implementations of IProgressiveFrameProvider must be
// thread-safe. GenerateFrame may be called concurrently from multiple threads.
class IProgressiveFrameProvider {
 public:
  virtual ~IProgressiveFrameProvider() = default;
  // Ownership: out_image and error are output parameters. The caller owns
  // the pointed-to memory and must ensure the pointers are valid (non-null).
  // The implementation writes to these locations but does not take ownership.
  virtual bool GenerateFrame(const Section& section, int frame_index,
                             Standard standard, FrameSourceImage* out_image,
                             std::string* error) const = 0;
};

// Thread-safety: Implementations of IGenerationStage are NOT thread-safe.
// Callers must ensure sequential access. Concurrent calls to Generate,
// GenerateFrameBatch, or BuildFrameSchedule from multiple threads will result
// in undefined behavior.
class IGenerationStage {
 public:
  virtual ~IGenerationStage() = default;
  struct FrameScheduleItem {
    const Section* section = nullptr;
    int source_frame_index = 0;
    // CAV picture number for this frame (> 0 when a picture_number injection
    // code is present); 0 for sections without a picture_number code.
    // Used to derive disc-accurate colour-subcarrier phase independent of file
    // position.
    int disc_picture_number = 0;
  };

  // Ownership: out_schedule and errors are output parameters. The caller owns
  // the pointed-to vectors and must ensure the pointers are valid (non-null).
  // The implementation clears and populates these vectors but does not take
  // ownership.
  virtual bool BuildFrameSchedule(const Project& project,
                                  std::vector<FrameScheduleItem>* out_schedule,
                                  std::vector<std::string>* errors) = 0;

  // Ownership: out_y_mv, out_c_mv, and errors are output parameters. The caller
  // owns the pointed-to vectors and must ensure the pointers are valid
  // (non-null). The implementation clears and populates these vectors but does
  // not take ownership.
  virtual bool GenerateFrameBatch(
      const Project& project, const std::vector<FrameScheduleItem>& schedule,
      std::size_t start_frame, std::size_t frame_count,
      std::vector<SampleFixed>* out_y_mv, std::vector<SampleFixed>* out_c_mv,
      std::vector<std::string>* errors) = 0;

  // Ownership: out_y_mv, out_c_mv, and errors are output parameters. The caller
  // owns the pointed-to vectors and must ensure the pointers are valid
  // (non-null). The implementation clears and populates these vectors but does
  // not take ownership.
  virtual bool Generate(const Project& project,
                        std::vector<SampleFixed>* out_y_mv,
                        std::vector<SampleFixed>* out_c_mv,
                        std::vector<std::string>* errors) = 0;
};

// Thread-safety: Implementations of IOutputStage are NOT thread-safe.
// Callers must ensure sequential access. Concurrent calls to BeginWrite,
// AppendSamples, FinalizeWrite, or Write from multiple threads will result in
// undefined behavior.
class IOutputStage {
 public:
  virtual ~IOutputStage() = default;

  // Ownership: errors is an output parameter. The caller owns the pointed-to
  // vector and must ensure the pointer is valid (non-null). The implementation
  // clears and populates this vector but does not take ownership.
  virtual bool BeginWrite(const Project& project,
                          std::size_t expected_frame_count,
                          std::vector<std::string>* errors) = 0;

  // Ownership: errors is an output parameter. The caller owns the pointed-to
  // vector and must ensure the pointer is valid (non-null). The implementation
  // clears and populates this vector but does not take ownership.
  virtual bool AppendSamples(const std::vector<SampleFixed>& y_mv,
                             const std::vector<SampleFixed>& c_mv,
                             std::vector<std::string>* errors) = 0;

  // Ownership: errors is an output parameter. The caller owns the pointed-to
  // vector and must ensure the pointer is valid (non-null). The implementation
  // clears and populates this vector but does not take ownership.
  virtual bool FinalizeWrite(std::vector<std::string>* errors) = 0;

  // Ownership: errors is an output parameter. The caller owns the pointed-to
  // vector and must ensure the pointer is valid (non-null). The implementation
  // clears and populates this vector but does not take ownership.
  virtual bool Write(const Project& project,
                     const std::vector<SampleFixed>& y_mv,
                     const std::vector<SampleFixed>& c_mv,
                     std::vector<std::string>* errors) = 0;
};

}  // namespace videosynth
