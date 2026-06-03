/*
 * File:        project_validator.h
 * Module:      project_validator
 * Purpose:     Validates project constraints for supported generation profiles.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "videosynth/interfaces.h"

namespace videosynth {

// Thread-safety: ProjectValidator is NOT thread-safe. Inherits the thread-safe
// requirement from IProjectValidator but does not implement internal
// synchronization. Concurrent calls to Validate will result in undefined
// behavior.
class ProjectValidator final : public IProjectValidator {
 public:
  // Constructs a project validator.
  //
  // Args:
  //   progressive_frame_source_probe: Optional probe for validating frame
  //   sources. logger: Optional logger for error reporting.
  explicit ProjectValidator(
      IProgressiveFrameSourceProbe* progressive_frame_source_probe = nullptr,
      ILogger* logger = nullptr);

  // Validates a project configuration for supported generation profiles.
  //
  // Args:
  //   project: The project to validate.
  //
  // Returns:
  //   ValidationResult containing validation status and any errors.
  ValidationResult Validate(const Project& project) override;

 private:
  IProgressiveFrameSourceProbe* progressive_frame_source_probe_;
  ILogger* logger_;
};

}  // namespace videosynth
