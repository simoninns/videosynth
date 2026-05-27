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

class ProjectValidator final : public IProjectValidator {
 public:
  ValidationResult Validate(const Project& project) override;
};

}  // namespace videosynth
