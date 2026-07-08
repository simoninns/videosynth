/*
 * File:        project_templates.h
 * Module:      gui
 * Purpose:     Built-in project templates for File > New
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "videosynth/model.h"

namespace videosynth::gui {

// Returns a minimal PAL project that passes structural validation
// (ProjectValidator without a source probe): one progressive section with a
// placeholder source path the user is expected to replace.
//
// Thread-safety: thread-safe (pure function).
Project MakeDefaultPalProject();

}  // namespace videosynth::gui
