/*
 * File:        path_resolution.cpp
 * Module:      model
 * Purpose:     Resolves section-source and output paths using logical asset
 *              roots ({name}/path tokens) resolved at runtime.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/path_resolution.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#ifndef VIDEOSYNTH_BUNDLED_ASSET_DIR
#define VIDEOSYNTH_BUNDLED_ASSET_DIR ""
#endif

namespace videosynth {

namespace {

// Reads an environment variable, returning nullopt when unset or empty.
std::optional<std::string> Env(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

// If `path` begins with a "{name}" token, returns the (name, rest) split where
// rest has any leading slash removed; otherwise nullopt.
std::optional<std::pair<std::string, std::string>> SplitRootToken(
    const std::string& path) {
  if (path.empty() || path.front() != '{') {
    return std::nullopt;
  }
  const std::size_t close = path.find('}');
  if (close == std::string::npos) {
    return std::nullopt;
  }
  const std::string name = path.substr(1, close - 1);
  std::string rest = path.substr(close + 1);
  if (!rest.empty() && (rest.front() == '/' || rest.front() == '\\')) {
    rest.erase(0, 1);
  }
  return std::make_pair(name, rest);
}

}  // namespace

std::vector<std::string> BuiltinRootNames() {
  return {"bundled", "user", "project"};
}

bool IsBuiltinRootName(const std::string& name) {
  for (const std::string& builtin : BuiltinRootNames()) {
    if (builtin == name) {
      return true;
    }
  }
  return false;
}

std::string ResolvePathAgainstBase(const std::string& base_dir,
                                   const std::string& path) {
  if (path.empty() || base_dir.empty()) {
    return path;
  }
  const std::filesystem::path candidate(path);
  if (candidate.is_absolute()) {
    return path;
  }
  return (std::filesystem::path(base_dir) / candidate)
      .lexically_normal()
      .string();
}

AssetRootMap DefaultAssetRoots() {
  AssetRootMap map;

  if (const std::optional<std::string> override_dir =
          Env("VIDEOSYNTH_ASSET_DIR")) {
    map.roots["bundled"] = *override_dir;
  } else if (std::string(VIDEOSYNTH_BUNDLED_ASSET_DIR).size() > 0) {
    map.roots["bundled"] = VIDEOSYNTH_BUNDLED_ASSET_DIR;
  }

  if (const std::optional<std::string> xdg = Env("XDG_DATA_HOME")) {
    map.roots["user"] =
        (std::filesystem::path(*xdg) / "videosynth" / "assets").string();
  } else if (const std::optional<std::string> home = Env("HOME")) {
    map.roots["user"] = (std::filesystem::path(*home) / ".local" / "share" /
                         "videosynth" / "assets")
                            .string();
  }

  return map;
}

std::string ResolveAssetPath(const std::string& path, const AssetRootMap& roots,
                             const std::string& project_dir,
                             bool anchor_unset) {
  if (path.empty()) {
    return path;
  }

  if (const auto token = SplitRootToken(path)) {
    const std::string& name = token->first;
    const std::string& rest = token->second;

    // "project" is always available and maps to the project directory.
    if (name == "project") {
      return rest.empty() ? project_dir
                          : ResolvePathAgainstBase(project_dir, rest);
    }
    const auto it = roots.roots.find(name);
    if (it == roots.roots.end()) {
      // Unknown root: leave unchanged so the validator can flag it clearly.
      return path;
    }
    // A relative root base is itself anchored to the project directory.
    const std::string base = ResolvePathAgainstBase(project_dir, it->second);
    // A bare "{root}" token resolves to the root's base directory itself.
    return rest.empty() ? base : ResolvePathAgainstBase(base, rest);
  }

  if (std::filesystem::path(path).is_absolute()) {
    return path;
  }
  return anchor_unset ? ResolvePathAgainstBase(project_dir, path) : path;
}

Project ResolveProjectPaths(Project project, const AssetRootMap& roots,
                            const std::string& project_dir, bool anchor_unset) {
  for (Section& section : project.sections) {
    section.source =
        ResolveAssetPath(section.source, roots, project_dir, anchor_unset);
  }
  project.output.video_path = ResolveAssetPath(project.output.video_path, roots,
                                               project_dir, anchor_unset);
  project.output.metadata_path = ResolveAssetPath(
      project.output.metadata_path, roots, project_dir, anchor_unset);
  return project;
}

}  // namespace videosynth
