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

YamlProjectParser::YamlProjectParser(ILogger* logger) : logger_(logger) {}

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

bool ParseDurationFrames(const YAML::Node& section_node,
                         Section* section,
                         std::vector<std::string>* errors) {
  if (section == nullptr || errors == nullptr) {
    return false;
  }

  if (!section_node["duration_frames"]) {
    errors->push_back("section is missing required field: 'duration_frames'.");
    return false;
  }

  const YAML::Node duration_node = section_node["duration_frames"];
  if (!duration_node.IsScalar()) {
    errors->push_back("section field 'duration_frames' must be a scalar integer or 'all'.");
    return false;
  }

  const std::string scalar = duration_node.as<std::string>("");
  if (scalar == "all") {
    section->duration_frames_all = true;
    section->duration_frames = 0;
    return true;
  }

  try {
    const int duration_frames = duration_node.as<int>();
    section->duration_frames_all = false;
    section->duration_frames = duration_frames;
    return true;
  } catch (const YAML::Exception&) {
    errors->push_back("section field 'duration_frames' must be an integer or the string 'all'.");
    return false;
  }
}

}  // namespace

ParseResult YamlProjectParser::ParseFile(const std::string& path) {
  ParseResult result;

  if (logger_ != nullptr) {
    logger_->Info("Parsing project file: " + path);
  }

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

    if (!root["output"] || !root["output"].IsMap()) {
      result.errors.push_back("Missing required top-level map: output.");
      return result;
    }

    const std::set<std::string> root_keys = {"project", "cvbs_presets", "output", "sections"};
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
    const std::set<std::string> preset_keys = {
      "video_standard_preset",
      "sample_encoding_preset",
      "signal_state_preset",
      "ntsc_black_setup_ire"};
    ValidateAllowedKeys(presets, preset_keys, "cvbs_presets", &result.errors);
    if (!result.errors.empty()) {
      return result;
    }

    result.project.cvbs_presets.video_standard_preset =
      StandardFromString(presets["video_standard_preset"].as<std::string>(""));

    result.project.cvbs_presets.sample_encoding_preset =
      presets["sample_encoding_preset"].as<std::string>(
        result.project.cvbs_presets.sample_encoding_preset);
    result.project.cvbs_presets.signal_state_preset =
      presets["signal_state_preset"].as<std::string>(
        result.project.cvbs_presets.signal_state_preset);
    if (presets["ntsc_black_setup_ire"]) {
      result.project.cvbs_presets.ntsc_black_setup_ire =
          presets["ntsc_black_setup_ire"].as<double>();
      result.project.cvbs_presets.ntsc_black_setup_ire_specified = true;
    }

    const YAML::Node output = root["output"];
    const std::set<std::string> output_keys = {"video_path", "metadata_path"};
    ValidateAllowedKeys(output, output_keys, "output", &result.errors);
    if (!result.errors.empty()) {
      return result;
    }

    result.project.output.video_path = output["video_path"].as<std::string>("");
    result.project.output.metadata_path = output["metadata_path"].as<std::string>("");

    for (const YAML::Node& section_node : root["sections"]) {
      if (!section_node.IsMap()) {
        result.errors.push_back("Each section entry must be a map/object.");
        return result;
      }

      const std::set<std::string> section_keys = {
          "name", "type", "pattern", "duration_frames", "line_injections", "source",
          "source_pixel_format", "start_frame"};
      ValidateAllowedKeys(section_node, section_keys, "section", &result.errors);
      if (!result.errors.empty()) {
        return result;
      }

      Section section;
      section.name = section_node["name"].as<std::string>("");
      section.type = section_node["type"].as<std::string>("");
      section.pattern = section_node["pattern"].as<std::string>("");
      section.source = section_node["source"].as<std::string>("");
      section.source_pixel_format = section_node["source_pixel_format"].as<std::string>("");
      section.start_frame = section_node["start_frame"].as<int>(0);
      if (!ParseDurationFrames(section_node, &section, &result.errors)) {
        return result;
      }
      result.project.sections.push_back(section);
    }

    result.ok = true;
    if (logger_ != nullptr) {
      logger_->Debug("Parsed project file with " + std::to_string(result.project.sections.size()) +
                     " section(s).");
    }
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
