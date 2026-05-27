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
#include <set>

#include <yaml-cpp/yaml.h>

namespace videosynth {

namespace {

bool ValidateAllowedKeys(const YAML::Node& node,
                        const std::set<std::string>& allowed_keys,
                        const std::string& context,
                        std::vector<std::string>* errors) {
  if (!node || !node.IsMap() || errors == nullptr) {
    return true;
  }

  for (const auto& kv : node) {
    const std::string key = kv.first.as<std::string>("");
    if (key.empty()) {
      continue;
    }
    if (allowed_keys.find(key) == allowed_keys.end()) {
      errors->push_back(context + " contains unsupported field: '" + key + "'.");
    }
  }

  return errors->empty();
}

}  // namespace

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

    const std::set<std::string> root_keys = {"project", "cvbs_presets", "sections"};
    ValidateAllowedKeys(root, root_keys, "Top-level YAML", &result.errors);
    if (!result.errors.empty()) {
      return result;
    }

    if (root["project"] && root["project"]["name"]) {
      result.project.name = root["project"]["name"].as<std::string>();
    }

    if (root["project"] && root["project"]["version"]) {
      result.project.version = root["project"]["version"].as<std::string>();
    }

    if (root["project"]) {
      const std::set<std::string> project_keys = {"name", "version", "description"};
      ValidateAllowedKeys(root["project"], project_keys, "project", &result.errors);
      if (!result.errors.empty()) {
        return result;
      }
    }

    const YAML::Node presets = root["cvbs_presets"];
    const std::set<std::string> preset_keys = {"standard", "sample_rate", "subcarrier_lock"};
    ValidateAllowedKeys(presets, preset_keys, "cvbs_presets", &result.errors);
    if (!result.errors.empty()) {
      return result;
    }

    result.project.cvbs_presets.standard =
        StandardFromString(presets["standard"].as<std::string>(""));
    result.project.cvbs_presets.sample_rate = presets["sample_rate"].as<std::string>("");
    result.project.cvbs_presets.subcarrier_lock =
        presets["subcarrier_lock"].as<bool>(false);

    for (const YAML::Node& section_node : root["sections"]) {
      if (!section_node.IsMap()) {
        result.errors.push_back("Each section entry must be a map/object.");
        return result;
      }

      const std::set<std::string> section_keys = {
          "name", "type", "pattern", "duration_frames", "line_injections", "source", "start_frame"};
      ValidateAllowedKeys(section_node, section_keys, "section", &result.errors);
      if (!result.errors.empty()) {
        return result;
      }

      Section section;
      section.name = section_node["name"].as<std::string>("");
      section.type = section_node["type"].as<std::string>("");
      section.pattern = section_node["pattern"].as<std::string>("");
      section.duration_frames = section_node["duration_frames"].as<int>(0);
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
