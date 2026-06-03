/*
 * File:        yaml_project_parser.h
 * Module:      yaml_project_parser
 * Purpose:     Parses YAML project files into internal project models.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>

#include "videosynth/interfaces.h"

namespace videosynth {

// Thread-safety: YamlProjectParser is NOT thread-safe. Inherits the thread-safe
// requirement from IProjectParser but does not implement internal
// synchronization. Concurrent calls to ParseFile will result in undefined
// behavior.
class YamlProjectParser final : public IProjectParser {
 public:
  // Constructs a YAML project parser.
  //
  // Args:
  //   logger: Optional logger for error reporting.
  explicit YamlProjectParser(ILogger* logger = nullptr);

  // Parses a YAML project file into a Project structure.
  //
  // Args:
  //   path: Path to the YAML project file.
  //
  // Returns:
  //   ParseResult containing the parsed project and any errors.
  ParseResult ParseFile(const std::string& path) override;

 private:
  ILogger* logger_;
};

}  // namespace videosynth
