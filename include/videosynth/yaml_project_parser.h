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

class YamlProjectParser final : public IProjectParser {
 public:
  ParseResult ParseFile(const std::string& path) override;
};

}  // namespace videosynth
