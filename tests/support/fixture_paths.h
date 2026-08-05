/*
 * File:        fixture_paths.h
 * Module:      test_support
 * Purpose:     Locates the project fixture trees consumed by the test suites
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Absolute path of the committed project fixture tree (projects/), supplied by
// the build so tests do not depend on the working directory.
#ifndef VIDEOSYNTH_PROJECT_DIR
#error "VIDEOSYNTH_PROJECT_DIR must be defined by the build"
#endif

// Absolute path of the build-time derived project tree written by
// scripts/generate_test_projects.py. Empty when generation is unavailable.
#ifndef VIDEOSYNTH_GENERATED_PROJECT_DIR
#define VIDEOSYNTH_GENERATED_PROJECT_DIR ""
#endif

// Named test_support rather than testing: inside namespace videosynth an
// unqualified "testing::" would otherwise shadow GoogleTest's namespace.
namespace videosynth::test_support {

// Root of the hand-authored project fixtures.
inline std::filesystem::path ProjectFixtureRoot() {
  return std::filesystem::path(VIDEOSYNTH_PROJECT_DIR);
}

// Root of the generated variant projects; empty when the build could not run
// the generator (no Python interpreter), in which case callers cover only the
// committed tree.
inline std::filesystem::path GeneratedProjectRoot() {
  const std::string configured(VIDEOSYNTH_GENERATED_PROJECT_DIR);
  return configured.empty() ? std::filesystem::path()
                            : std::filesystem::path(configured);
}

// Absolute path of a fixture named relative to the committed tree, e.g.
// "general/pal_audio.yaml".
inline std::string FixturePath(std::string_view relative) {
  return (ProjectFixtureRoot() / relative).string();
}

// Directory holding the named fixture; project-relative paths inside a fixture
// resolve against this.
inline std::string FixtureProjectDir(std::string_view relative) {
  return std::filesystem::path(FixturePath(relative)).parent_path().string();
}

// Every project YAML the suites should exercise: the committed tree plus the
// generated variants when they are present.
inline std::vector<std::filesystem::path> AllProjectFixtures() {
  std::vector<std::filesystem::path> files;

  const auto collect = [&files](const std::filesystem::path& root) {
    if (root.empty() || !std::filesystem::is_directory(root)) {
      return;
    }
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(root)) {
      if (entry.path().extension() == ".yaml") {
        files.push_back(entry.path());
      }
    }
  };

  collect(ProjectFixtureRoot());
  collect(GeneratedProjectRoot());
  return files;
}

}  // namespace videosynth::test_support
