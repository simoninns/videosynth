/*
 * File:        yaml_project_emitter.h
 * Module:      yaml_project_emitter
 * Purpose:     Serialises project models back to YAML project files.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>

#include "videosynth/interfaces.h"

namespace videosynth {

// Serialises a videosynth::Project to the YAML project-file schema described
// in docs-tech/design/high-level-design.md §7. Only explicitly-set optional
// blocks (noise, dropouts, audio, osd, line_injections) are emitted so
// saved files stay minimal, and field ordering is stable/canonical so diffs
// of saved files remain readable. A file emitted by this class parses back
// (via YamlProjectParser) to a Project equal to the input.
//
// Thread-safety: YamlProjectEmitter is thread-safe when the injected logger
// is thread-safe. EmitString and EmitFile hold no mutable state and may be
// called concurrently from multiple threads.
class YamlProjectEmitter {
 public:
  // Constructs a YAML project emitter.
  //
  // Args:
  //   logger: Optional logger for lifecycle reporting.
  explicit YamlProjectEmitter(ILogger* logger = nullptr);

  // Serialises the project to a YAML document string.
  //
  // Args:
  //   project: The project model to serialise.
  //
  // Returns:
  //   The YAML document text (terminated with a trailing newline).
  std::string EmitString(const Project& project) const;

  // Serialises the project and writes it to a file.
  //
  // Args:
  //   project: The project model to serialise.
  //   path: Destination file path (overwritten if it exists).
  //   error: Optional output; receives a failure description when non-null.
  //
  // Returns:
  //   true on success, false on filesystem failure.
  bool EmitFile(const Project& project, const std::string& path,
                std::string* error) const;

 private:
  ILogger* logger_;
};

}  // namespace videosynth
