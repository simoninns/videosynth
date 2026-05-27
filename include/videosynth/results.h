/*
 * File:        results.h
 * Module:      results
 * Purpose:     Defines parse and validation result containers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>

#include "videosynth/model.h"

namespace videosynth {

struct ParseResult {
  bool ok = false;
  Project project;
  std::vector<std::string> errors;
};

struct ValidationResult {
  bool is_valid = false;
  std::vector<std::string> errors;
};

}  // namespace videosynth
