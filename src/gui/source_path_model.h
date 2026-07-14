/*
 * File:        source_path_model.h
 * Module:      gui
 * Purpose:     Pure classification/composition of a section source path for the
 *              built-in / my-own source picker (no Qt, no filesystem)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>

namespace videosynth::gui {

// The section-editor source picker exposes exactly two kinds of asset:
//   - Built-in: an asset shipped with videosynth, addressed as
//     {bundled}/<type>/<raster>/<file>. Only <type> (exr/mkv) and <file> are
//     the user's to choose; <raster> follows the project's video standard.
//   - My own file: any other path — a {project}-relative path, an absolute
//     path, or another preserved logical token (e.g. {user}/…).
// SourceSelection is the widget-agnostic form of that choice, so the string
// parsing and composition can be unit-tested without a live Qt widget.
struct SourceSelection {
  bool builtin = false;   // true => built-in asset (type/file used)
  std::string type;       // built-in asset type: "exr" or "mkv"
  std::string file;       // built-in file name (no directory)
  bool relative = false;  // my-own: store relative to the project file
  std::string text;       // my-own: the displayed path or verbatim token
};

// Classifies a stored source string into the picker state. A {bundled}/… source
// becomes a built-in selection (its stored raster is discarded — the caller
// re-derives it from the project). Everything else is a my-own selection, with
// `relative` set for {project}-relative and bare relative paths and cleared for
// absolute paths and other logical tokens.
SourceSelection ParseSourceSelection(const std::string& source);

// Composes the stored source string from a selection. A built-in selection is
// always recomposed as {bundled}/<type>/<raster>/<file> using `raster` (so a
// project whose standard changed self-heals to the matching raster's asset); an
// empty file yields an empty string. A my-own selection preserves an explicit
// {token} verbatim, wraps a relative path as {project}/…, or stores an absolute
// path unchanged; an empty path yields an empty string.
std::string ComposeSource(const SourceSelection& selection,
                          const std::string& raster);

}  // namespace videosynth::gui
