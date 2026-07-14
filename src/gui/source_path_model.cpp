/*
 * File:        source_path_model.cpp
 * Module:      gui
 * Purpose:     Pure classification/composition of a section source path for the
 *              built-in / my-own source picker (no Qt, no filesystem)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "source_path_model.h"

#include <filesystem>

namespace videosynth::gui {

namespace {

constexpr char kBundledPrefix[] = "{bundled}/";
constexpr char kProjectPrefix[] = "{project}";

std::string StripLeadingSlashes(std::string value) {
  while (!value.empty() && (value.front() == '/' || value.front() == '\\')) {
    value.erase(0, 1);
  }
  return value;
}

}  // namespace

SourceSelection ParseSourceSelection(const std::string& source) {
  SourceSelection selection;

  // Built-in asset: {bundled}/<type>/<raster>/<file>. Keep only type + file;
  // the raster is the caller's to supply from the current project.
  if (source.rfind(kBundledPrefix, 0) == 0) {
    const std::string rest = source.substr(std::string(kBundledPrefix).size());
    const std::size_t first_slash = rest.find('/');
    const std::size_t last_slash = rest.rfind('/');
    selection.builtin = true;
    selection.type =
        first_slash == std::string::npos ? rest : rest.substr(0, first_slash);
    selection.file = last_slash == std::string::npos
                         ? std::string()
                         : rest.substr(last_slash + 1);
    return selection;
  }

  // My own file.
  if (source.rfind(kProjectPrefix, 0) == 0) {
    // Show the path under the project; the "relative" flag conveys the anchor.
    selection.relative = true;
    selection.text =
        StripLeadingSlashes(source.substr(std::string(kProjectPrefix).size()));
  } else if (source.empty() || (source.front() != '{' &&
                                !std::filesystem::path(source).is_absolute())) {
    // Bare relative path (or nothing yet): project-relative.
    selection.relative = true;
    selection.text = source;
  } else {
    // Absolute path or a foreign logical token ({user}/…): stored verbatim.
    selection.relative = false;
    selection.text = source;
  }
  return selection;
}

std::string ComposeSource(const SourceSelection& selection,
                          const std::string& raster) {
  if (selection.builtin) {
    if (selection.file.empty()) {
      return std::string();
    }
    return "{bundled}/" + selection.type + "/" + raster + "/" + selection.file;
  }

  if (selection.text.empty()) {
    return std::string();
  }
  if (selection.text.front() == '{') {
    return selection.text;  // Preserve an explicit token (e.g. {user}/…).
  }
  if (selection.relative) {
    return "{project}/" + StripLeadingSlashes(selection.text);
  }
  return selection.text;  // Absolute path stored verbatim.
}

}  // namespace videosynth::gui
