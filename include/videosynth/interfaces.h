/*
 * File:        interfaces.h
 * Module:      interfaces
 * Purpose:     Defines core pipeline interfaces and runtime options.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>

#include "videosynth/model.h"
#include "videosynth/results.h"

namespace videosynth {

struct RunOptions {
  std::string project_path;
  bool validate_only = false;
  bool verbose = false;
};

class ILogger {
 public:
  virtual ~ILogger() = default;
  virtual void Info(const std::string& message) = 0;
  virtual void Error(const std::string& message) = 0;
  virtual void Debug(const std::string& message) = 0;
};

class IProjectParser {
 public:
  virtual ~IProjectParser() = default;
  virtual ParseResult ParseFile(const std::string& path) = 0;
};

class IProjectValidator {
 public:
  virtual ~IProjectValidator() = default;
  virtual ValidationResult Validate(const Project& project) = 0;
};

struct ProgressiveSourceProfile {
  std::string container;
  std::string codec;
  std::string pixel_format;
  int bit_depth = 0;
  int width = 0;
  int height = 0;
  double frame_rate_hz = 0.0;
  int frame_count = 0;
};

class IProgressiveSourceProbe {
 public:
  virtual ~IProgressiveSourceProbe() = default;
  virtual bool Probe(const Section& section,
                     ProgressiveSourceProfile* out_profile,
                     std::string* error) = 0;
};

struct FrameSourceImage;

class IProgressiveFrameProvider {
 public:
  virtual ~IProgressiveFrameProvider() = default;
  virtual bool GenerateFrame(const Section& section,
                             int frame_index,
                             Standard standard,
                             FrameSourceImage* out_image,
                             std::string* error) const = 0;
};

class IGenerationStage {
 public:
  virtual ~IGenerationStage() = default;
  virtual bool Generate(const Project& project,
                        std::vector<double>* out_y_mv,
                        std::vector<double>* out_c_mv,
                        std::vector<std::string>* errors) = 0;
};

class IOutputStage {
 public:
  virtual ~IOutputStage() = default;
  virtual bool Write(const Project& project,
                     const std::vector<double>& y_mv,
                     const std::vector<double>& c_mv,
                     std::vector<std::string>* errors) = 0;
};

}  // namespace videosynth
