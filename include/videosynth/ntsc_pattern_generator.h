/*
 * File:        ntsc_pattern_generator.h
 * Module:      ntsc_pattern_generator
 * Purpose:     Declares NTSC software-generated frame pattern generation helpers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>

#include "videosynth/frame_source.h"

namespace videosynth {

bool IsSupportedNtscPattern(const std::string& pattern);

bool GenerateNtscPatternFrame(const std::string& pattern,
                              FrameSourceImage* out_image,
                              std::string* error);

}  // namespace videosynth
