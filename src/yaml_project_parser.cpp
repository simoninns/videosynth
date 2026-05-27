/*
 * File:        yaml_project_parser.cpp
 * Module:      yaml_project_parser
 * Purpose:     Parses YAML project files into internal project models.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/yaml_project_parser.h"

#include <exception>

#include <yaml-cpp/yaml.h>

namespace videosynth {

ParseResult YamlProjectParser::ParseFile(const std::string& path) {
  ParseResult result;

  try {
    const YAML::Node root = YAML::LoadFile(path);

    if (!root["cvbs_presets"]) {
      result.errors.push_back("Missing required top-level field: cvbs_presets.");
      return result;
    }

    if (!root["sections"] || !root["sections"].IsSequence()) {
      result.errors.push_back("Missing required top-level list: sections.");
      return result;
    }

    if (root["project"] && root["project"]["name"]) {
      result.project.name = root["project"]["name"].as<std::string>();
    }

    if (root["project"] && root["project"]["version"]) {
      result.project.version = root["project"]["version"].as<std::string>();
    }

    const YAML::Node presets = root["cvbs_presets"];
    result.project.cvbs_presets.standard =
        StandardFromString(presets["standard"].as<std::string>(""));
    result.project.cvbs_presets.sample_rate = presets["sample_rate"].as<std::string>("");
    result.project.cvbs_presets.subcarrier_lock =
        presets["subcarrier_lock"].as<bool>(false);

    for (const YAML::Node& section_node : root["sections"]) {
      Section section;
      section.name = section_node["name"].as<std::string>("");
      section.type = section_node["type"].as<std::string>("");
      section.pattern = section_node["pattern"].as<std::string>("");
      result.project.sections.push_back(section);
    }

    result.ok = true;
    return result;
  } catch (const YAML::Exception& ex) {
    result.errors.push_back(std::string("YAML parsing failed: ") + ex.what());
    return result;
  } catch (const std::exception& ex) {
    result.errors.push_back(std::string("Unexpected parse error: ") + ex.what());
    return result;
  }
}

}  // namespace videosynth
