/*
 * File:        pal_pattern_generator.h
 * Module:      pal_pattern_generator
 * Purpose:     Declares PAL software-generated frame pattern generation helpers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>

#include "videosynth/frame_source.h"

namespace videosynth {

bool IsSupportedPalPattern(const std::string& pattern);

bool GeneratePalPatternFrame(const std::string& pattern,
                             FrameSourceImage* out_image,
                             std::string* error);

}  // namespace videosynth
